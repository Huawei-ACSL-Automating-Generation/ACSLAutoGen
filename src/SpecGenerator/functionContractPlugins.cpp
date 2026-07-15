/**
 * @file functionContractPlugins.cpp
 * @brief Implements function-level ACSL contract plugins (assigns, behaviors, poststate).
 */
#include <iterator>
#include <optional>
#include <unordered_set>

#include "specGenerator.h"
#include "macros.h"
#include "state.h"
#include "utils.h"
#include "Symbolic/expr.h"
#include "Symbolic/aggregateExpr.h"
#include <clang/AST/Decl.h>
#include <llvm/Support/Casting.h>

namespace acslg::spec_generator {
    namespace symb = acslg::analyzer::symbolic;

    namespace {
        const std::string IND1 = "  ";
        const std::string IND2 = "    ";

        symb::Expr simplifyExpr(symb::ExprHandle expr) {
            auto &factory = symb::ExprFactoryScope::current();
            return symb::Expr{factory, symb::simplifiedExprHandle(factory, *expr)};
        }

        // Frama-C does not resolve ACSL logic labels derived from internal C labels
        // (e.g. `After_CompoundStmt_xxxx`) inside function contracts. Such terms lead to
        // `annot-error: logic label ... not found` and abort WP. We conservatively drop them
        // from function-contract-level assigns; if that makes the assigns empty while we
        // did observe modifications, we fall back to `\everything`.
        bool hasDisallowedFunctionContractLabel(std::string_view acsl) {
            // Only filter state-label references. `Old` and other predefined labels are fine.
            if (acsl.find("\\at(") == std::string_view::npos) {
                return false;
            }
            return acsl.find("After_CompoundStmt_") != std::string_view::npos;
        }

        void collectReferencedVarDecls(symb::ExprHandle expr,
                                       std::unordered_set<const clang::VarDecl *> &out) {
            if (symb::LiteralExprView::tryFrom(expr)) {
                return;
            }
            if (auto sv = symb::SymbolValueView::tryFrom(expr)) {
                if (auto from = sv->fromRoot())
                    out.insert(from.value().get());
                return;
            }
            if (auto va = symb::VariableAddressView::tryFrom(expr)) {
                out.insert(va->declaration().get());
                return;
            }
            if (auto sa = symb::SymbolAddressView::tryFrom(expr)) {
                if (auto from = sa->fromRoot())
                    out.insert(from.value().get());
                collectReferencedVarDecls(sa->offset(), out);
                if (auto length = sa->length())
                    collectReferencedVarDecls(*length, out);
                return;
            }
            if (auto fa = symb::FieldAddressView::tryFrom(expr)) {
                if (auto from = fa->fromRoot())
                    out.insert(from.value().get());
                return;
            }
            if (auto bin = symb::BinaryExprView::tryFrom(expr)) {
                collectReferencedVarDecls(bin->left(), out);
                collectReferencedVarDecls(bin->right(), out);
                return;
            }
            if (auto unary = symb::UnaryExprView::tryFrom(expr)) {
                collectReferencedVarDecls(unary->operand(), out);
                return;
            }

            // Unknown / range / quantifier-like nodes: best-effort ignore here (filter will
            // conservatively treat failures elsewhere as dropped).
        }

        bool referencesNonContractVisibleLocals(symb::ExprHandle expr,
                                                const clang::FunctionDecl *fd) {
            (void)fd; // currently unused, but kept for future global/param policy tuning.
            std::unordered_set<const clang::VarDecl *> decls;
            collectReferencedVarDecls(expr, decls);
            for (const auto *vd : decls) {
                if (!vd)
                    continue;
                if (llvm::isa<clang::ParmVarDecl>(vd))
                    continue;
                // Function contracts cannot refer to locals (including static locals).
                if (vd->isLocalVarDecl())
                    return true;
            }
            return false;
        }
    } // namespace

    /**
     * @class TopAssignsPlugin
     * @brief Emits a top-level `assigns` clause based on differences between pre and post states.
     */
    class TopAssignsPlugin : public FunctionContractPlugin {
      public:
        TopAssignsPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        /**
         * @brief Compute the set of modified addresses and render an assigns clause.
         */
        GenResultType generate(const analyzer::ProgramState &pre,
                               const analyzer::ProgramState &post) const override {
            std::string spec;
            std::unordered_map<size_t, const symb::AddressBox> assignedAddrs;

            // Function's pre-state should have exactly one path.
            if (auto &paths = pre.getPaths(); paths.size() == 1) {
                auto &prePath = paths[0];

                for (auto &postPath : post.getPaths()) {
                    // INFO("path");
                    for (auto &&[addr, value] : postPath->getMemoryState().flat()) {
                        auto fromRoot = addr.get().getFromRoot();
                        if (fromRoot == std::nullopt)
                            continue;
                        if (!prePath->getVarAddr().contains(fromRoot.value()))
                            continue;
                        if (!is_symbol_addr(addr.handle()))
                            continue;
                        // INFO(addr.get().dump());
                        // INFO(value->dump());

                        // If the current address corresponds to a pointer targeting a
                        // structure, the associated handling is deliberately omitted. This
                        // omission is justified by the design of the flat() traversal: the
                        // fields of the structure are enumerated and processed individually.
                        // Thus, treating the pointer itself would introduce redundancy.
                        if (is_symbol_addr(addr.handle()) &&
                            postPath->is_point_to_structure(addr.handle()))
                            continue;

                        // Track the address only if the value differs between pre/post states.
                        if (!postPath->isUnchanged(addr.handle(), *pre.getPaths().front()))
                            auto [_, ok] = assignedAddrs.try_emplace(addr.hash(), std::move(addr));
                    }
                }
            }

            auto oldPoint = pre.getStartPoint();
            auto *FD      = pre.getFunction()->getFunctionDecl();
            std::unordered_set<symb::SourcePoint> allUsedPoints;
            bool droppedOrFailed = false;
            for (auto &[_, addr] : assignedAddrs) {
                if (referencesNonContractVisibleLocals(addr.handle().asExpr(), FD)) {
                    droppedOrFailed = true;
                    continue;
                }
                auto acslExpected = addr.get().getACSLOfValue(
                    {.noStateLabelFunctionAt = true, .predefinedLabels = {{oldPoint, "Old"}}},
                    oldPoint);
                if (!acslExpected) {
                    WARN("Value of {" + addr.get().dump() + "} getACSL failed.");
                    droppedOrFailed = true;
                    continue;
                }
                auto &[addrStr, usedPoints] = acslExpected.value();
                if (hasDisallowedFunctionContractLabel(addrStr)) {
                    droppedOrFailed = true;
                    continue;
                }
                spec += addrStr + ", ";
                allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                     std::make_move_iterator(usedPoints.end()));
            }

            if (spec.empty())
                return std::pair{IND1 + std::string(assignedAddrs.empty() && !droppedOrFailed
                                                        ? "assigns \\nothing;\n"
                                                        : "assigns \\everything;\n"),
                                 std::unordered_set<symb::SourcePoint>{}};
            else
                return std::pair{IND1 + std::string("assigns ") +
                                     spec.substr(0, spec.length() - 2) + ";\n",
                                 std::move(allUsedPoints)};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(TopAssignsPlugin, "assigns");

    /**
     * @class DetailBehaviorPlugin
     * @brief Emits behavior blocks capturing path-specific assigns/ensures for return paths.
     */
    class DetailBehaviorPlugin : public FunctionContractPlugin {
      public:
        DetailBehaviorPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }

        GenResultType generate(const analyzer::ProgramState &pre,
                               const analyzer::ProgramState &post) const override {
            auto &prePath = pre.getPaths().front();
            auto oldPoint = post.getStartPoint();
            std::unordered_set<symb::SourcePoint> allUsedPoints{};

            std::vector<std::string> behaviors;
            std::unordered_set<std::string> seenBehaviors;
            int idx = 0;

            for (auto &pathPtr : post.getPaths()) {
                const auto &path = *pathPtr;
                if (path.getPathState() != analyzer::Path::PathState::Return)
                    continue;

                std::optional<symb::SymbolAddrBaseInfo> retBase;
                if (auto &ret = path.getReturnExpr()) {
                    if (auto retAddr = symb::SymbolAddressView::tryFrom(ret.value()))
                        retBase = retAddr->baseInfo();
                }

                auto isRetBaseAddr = [&](const symb::AddressBox &addr) -> bool {
                    if (!retBase)
                        return false;
                    auto sa = symb::SymbolAddressView::tryFrom(addr.handle());
                    if (!sa)
                        return false;
                    return sa->baseInfo() == retBase.value();
                };

                auto getResultBaseACSL = [&](const symb::AddressBox &addr,
                                             const symb::SymbolicExpr::GetACSLConfig &config,
                                             std::optional<symb::SourcePoint> currentPoint)
                    -> std::optional<std::pair<std::string, std::unordered_set<symb::SourcePoint>>> {
                    if (!isRetBaseAddr(addr))
                        return std::nullopt;
                    auto sa = symb::SymbolAddressView::tryFrom(addr.handle());
                    if (!sa)
                        return std::nullopt;

                    std::unordered_set<symb::SourcePoint> usedPoints;
                    auto offsetExpected = sa->offset()->getACSL(config, currentPoint);
                    if (!offsetExpected)
                        return std::nullopt;
                    auto [offsetStr, offsetPts] = offsetExpected.value();
                    usedPoints.insert(std::make_move_iterator(offsetPts.begin()),
                                      std::make_move_iterator(offsetPts.end()));

                    auto length = sa->length();
                    if (!length) {
                        std::string addrStr;
                        if (offsetStr == "0" && config.useDerefWithZeroOffset)
                            addrStr = "*\\result";
                        else
                            addrStr = "\\result[" + offsetStr + "]";
                        return std::pair{std::move(addrStr), std::move(usedPoints)};
                    }

                    auto lenExpected = (*length)->getACSL(config, currentPoint);
                    if (!lenExpected)
                        return std::nullopt;
                    auto [lenStr, lenPts] = lenExpected.value();
                    usedPoints.insert(std::make_move_iterator(lenPts.begin()),
                                      std::make_move_iterator(lenPts.end()));

                    std::string rightBound = "(" + offsetStr + " + " + lenStr + " - 1)";
                    std::string addrStr    = "\\result[" + offsetStr + " .. " + rightBound + "]";
                    return std::pair{std::move(addrStr), std::move(usedPoints)};
                };

                std::unordered_map<size_t, const symb::AddressBox> assignedAddrs;
                for (auto &&[a, v] : path.getMemoryState().flat()) {
                    auto fromRoot = a.get().getFromRoot();
                    if (fromRoot != std::nullopt) {
                        if (!prePath->getVarAddr().contains(fromRoot.value()))
                            continue;
                        if (!is_symbol_addr(a.handle()))
                            continue;
                    } else if (!isRetBaseAddr(a)) {
                        continue;
                    }
                    if (is_symbol_addr(a.handle()) && path.is_point_to_structure(a.handle()))
                        continue;
                    if (!path.isUnchanged(a.handle(), *pre.getPaths().front()))
                        // The address was modified on this path; record it for the assigns
                        // clause of this behavior.
                        (void)assignedAddrs.try_emplace(a.hash(), std::move(a));
                }

                std::string assignsSpec;
                bool droppedOrFailed = false;
                auto *FD             = pre.getFunction()->getFunctionDecl();
                for (auto &[_, a] : assignedAddrs) {
                    if (referencesNonContractVisibleLocals(a.handle().asExpr(), FD)) {
                        droppedOrFailed = true;
                        continue;
                    }
                    symb::SymbolicExpr::GetACSLConfig cfg{.noStateLabelFunctionAt = true,
                                                          .predefinedLabels = {{oldPoint, "Old"}}};
                    auto acslExpected = a.get().getACSLOfValue(cfg, oldPoint);
                    if (!acslExpected) {
                        if (acslExpected.error() == symb::SymbolicExpr::GetACSLError::HeapAddress) {
                            if (auto fallback = getResultBaseACSL(a, cfg, oldPoint)) {
                                if (hasDisallowedFunctionContractLabel(fallback.value().first)) {
                                    droppedOrFailed = true;
                                    continue;
                                }
                                assignsSpec += fallback.value().first + ", ";
                                allUsedPoints.insert(
                                    std::make_move_iterator(fallback.value().second.begin()),
                                    std::make_move_iterator(fallback.value().second.end()));
                                continue;
                            }
                        }
                        WARN("Value of {" + a.get().dump() + "} getACSL failed.");
                        droppedOrFailed = true;
                        continue;
                    }
                    auto &[addrStr, usedPoints] = acslExpected.value();
                    if (hasDisallowedFunctionContractLabel(addrStr)) {
                        droppedOrFailed = true;
                        continue;
                    }
                    assignsSpec += addrStr + ", ";
                    allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                         std::make_move_iterator(usedPoints.end()));
                }
                if (assignsSpec.empty())
                    assignsSpec =
                        (assignedAddrs.empty() && !droppedOrFailed) ? "\\nothing" : "\\everything";
                else
                    assignsSpec.erase(assignsSpec.size() - 2);

                std::vector<std::string> ensures;

                // result
                if (auto &ret = path.getReturnExpr()) {
                    auto simplifiedRet = simplifyExpr(ret.value());
                    if (!referencesNonContractVisibleLocals(simplifiedRet.handle(), FD))
                        if (auto expected =
                                simplifiedRet->getACSL({.noStateLabelFunctionAt = true,
                                                        .predefinedLabels = {{oldPoint, "Old"}}})) {
                            auto &[spec, usedPoints] = expected.value();
                            if (usedPoints.empty())
                                if (!ret.value()->isOverRange())
                                    ensures.push_back("\\result == (" + spec + ")");
                                else
                                    ensures.push_back(spec);
                            else {
                                // todo: maintain the write information, so can we know two source
                                // point are *same*. no way to do it right now :)
                                auto wrongExpected =
                                    simplifiedRet->getACSL({.noStateLabelFunctionAt = true});
                                assert(wrongExpected);
                                if (!ret.value()->isOverRange())
                                    ensures.push_back("\\result == (" +
                                                      wrongExpected.value().first + ")");
                                else
                                    ensures.push_back(wrongExpected.value().first);
                            }
                        }
                }

                // Memory equations
                for (auto &&[addr, value] : path.getMemoryState().flat()) {
                    auto fromRoot = addr.get().getFromRoot();
                    if (fromRoot != std::nullopt) {
                        if (!prePath->getVarAddr().contains(fromRoot.value()))
                            continue;
                        if (!is_symbol_addr(addr.handle()))
                            continue;
                    } else if (!isRetBaseAddr(addr)) {
                        continue;
                    }
                    if (is_symbol_addr(addr.handle()) && value->isStructure()) {
                        if (isRetBaseAddr(addr)) {
                            symb::StructureView st{value};
                            auto &info      = st.info();
                            size_t idxField = 0;
                            for (auto field : info.definition_->fields()) {
                                std::string fieldName = field->getNameAsString();
                                auto simplifiedField  = simplifyExpr(st.field(idxField));
                                ++idxField;
                                if (fieldName.empty())
                                    continue;

                                if (referencesNonContractVisibleLocals(simplifiedField.handle(),
                                                                       FD))
                                    continue;
                                symb::SymbolicExpr::GetACSLConfig cfg{
                                    .noStateLabelFunctionAt = true,
                                    .predefinedLabels       = {{oldPoint, "Old"}}};
                                auto rhsOpt = simplifiedField->getACSL(cfg);
                                if (!rhsOpt)
                                    continue;
                                ensures.push_back("\\result->" + fieldName + " == (" +
                                                  rhsOpt.value().first + ")");
                                allUsedPoints.insert(
                                    std::make_move_iterator(rhsOpt.value().second.begin()),
                                    std::make_move_iterator(rhsOpt.value().second.end()));
                            }
                        }
                        continue;
                    }

                    symb::SymbolicExpr::GetACSLConfig cfg{.noStateLabelFunctionAt = true,
                                                          .predefinedLabels = {{oldPoint, "Old"}}};
                    if (referencesNonContractVisibleLocals(addr.handle().asExpr(), FD))
                        continue;
                    auto simplifiedRhs = simplifyExpr(value);
                    if (referencesNonContractVisibleLocals(simplifiedRhs.handle(), FD))
                        continue;
                    auto lhsOpt = addr.get().getACSLOfValue(cfg);
                    if (!lhsOpt) {
                        if (lhsOpt.error() == symb::SymbolicExpr::GetACSLError::HeapAddress) {
                            if (auto fallback = getResultBaseACSL(addr, cfg, oldPoint)) {
                                auto rhsOpt = simplifiedRhs->getACSL(cfg);
                                if (!rhsOpt)
                                    continue;
                                ensures.push_back(fallback.value().first + " == (" +
                                                  rhsOpt.value().first + ")");
                                allUsedPoints.insert(
                                    std::make_move_iterator(fallback.value().second.begin()),
                                    std::make_move_iterator(fallback.value().second.end()));
                                allUsedPoints.insert(
                                    std::make_move_iterator(rhsOpt.value().second.begin()),
                                    std::make_move_iterator(rhsOpt.value().second.end()));
                                continue;
                            }
                        }
                        continue;
                    }

                    auto rhsOpt = simplifiedRhs->getACSL(cfg);
                    if (!rhsOpt)
                        continue;

                    // Wrap RHS to avoid precedence surprises in ACSL (e.g., bitwise ops vs ==).
                    ensures.push_back(lhsOpt.value().first + " == (" + rhsOpt.value().first + ")");
                    allUsedPoints.insert(std::make_move_iterator(lhsOpt.value().second.begin()),
                                         std::make_move_iterator(lhsOpt.value().second.end()));
                    allUsedPoints.insert(std::make_move_iterator(rhsOpt.value().second.begin()),
                                         std::make_move_iterator(rhsOpt.value().second.end()));
                }

                if (auto &ret = path.getReturnExpr()) {
                    if (auto retSt = symb::StructureView::tryFrom(ret.value())) {
                        auto &info      = retSt->info();
                        size_t idxField = 0;
                        for (auto field : info.definition_->fields()) {
                            std::string fieldName = field->getNameAsString();
                            if (fieldName.empty()) {
                                ++idxField;
                                continue;
                            }
                            auto simplifiedField = simplifyExpr(retSt->field(idxField));
                            if (referencesNonContractVisibleLocals(simplifiedField.handle(), FD)) {
                                ++idxField;
                                continue;
                            }
                            auto fieldExpected = simplifiedField->getACSL(
                                {.noStateLabelFunctionAt = true, .predefinedLabels = {}});
                            if (!fieldExpected) {
                                ++idxField;
                                continue;
                            }
                            ensures.push_back("\\result." + fieldName + " == (" +
                                              fieldExpected.value().first + ")");
                            allUsedPoints.insert(
                                std::make_move_iterator(fieldExpected.value().second.begin()),
                                std::make_move_iterator(fieldExpected.value().second.end()));
                            ++idxField;
                        }
                    }
                }

                auto [assumesSpec, requiresSpec] = joinConj(path.getPathConditions(), oldPoint, FD);
                if (ensures.empty() && assumesSpec.empty() && requiresSpec.empty() &&
                    assignsSpec == "\\nothing")
                    continue;

                std::string body;
                if (!assumesSpec.empty())
                    body += IND2 + "assumes " + assumesSpec + ";\n";
                if (!requiresSpec.empty())
                    body += IND2 + "requires " + requiresSpec + ";\n";
                body += IND2 + "assigns " + assignsSpec + ";\n";
                for (auto &e : ensures)
                    body += IND2 + "ensures " + e + ";\n";

                if (seenBehaviors.emplace(body).second) {
                    std::string bname = "b" + std::to_string(idx++);
                    std::string block;
                    block += IND1 + "behavior " + bname + ":\n";
                    block += body;
                    behaviors.push_back(std::move(block));
                }
            }

            if (behaviors.empty())
                return std::pair{std::nullopt, std::unordered_set<symb::SourcePoint>{}};

            std::string out;
            for (auto &b : behaviors)
                out += b;

            std::vector<std::string> names;
            for (int i = 0; i < (int)behaviors.size(); ++i)
                names.push_back("b" + std::to_string(i));
            // out += IND1 + "complete behaviors " + joinCSV(names) + ";\n";
            return std::pair{out, std::move(allUsedPoints)};
        }

      private:
        std::string id_;

        static std::pair<std::string, std::string> joinConj(const analyzer::PathConditions &conds,
                                                            symb::SourcePoint oldPoint,
                                                            const clang::FunctionDecl *FD) {
            std::string assumeStr;
            std::string requireStr;
            for (auto &cond : conds) {
                if (cond->isUnknown())
                    continue;
                auto simplified = simplifyExpr(cond);
                if (referencesNonContractVisibleLocals(simplified.handle(), FD))
                    continue;
                auto rf = simplified->getACSL({.predefinedLabels = {{oldPoint, "Old"}}}, oldPoint);
                if (!rf || rf.value().first.empty())
                    continue;
                // TODO: Conditions involving heap-allocated pointers may require SourcePoint
                // labels; we currently drop them because we cannot express usedPoints in requires.
                if (!rf.value().second.empty())
                    continue;
                auto &target = simplified->isOverRange() ? assumeStr : requireStr;
                if (!target.empty())
                    target += " && ";
                target += rf.value().first;
            }
            return {assumeStr, requireStr};
        }

        static std::string joinCSV(const std::vector<std::string> &v) {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i) {
                if (i)
                    s += ", ";
                s += v[i];
            }
            return s;
        }
    };

    REGISTER_ACSL_PLUGIN(DetailBehaviorPlugin, "poststate");
} // namespace acslg::spec_generator
