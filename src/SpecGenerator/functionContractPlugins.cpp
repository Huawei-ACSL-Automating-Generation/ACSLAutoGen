/**
 * @file functionContractPlugins.cpp
 * @brief Implements function-level ACSL contract plugins (assigns, behaviors, poststate).
 */
#include <iterator>
#include <memory>
#include <optional>
#include <unordered_set>

#include "specGenerator.h"
#include "macros.h"
#include "state.h"
#include "utils.h"
#include "Symbolic/expr.h"
#include "Symbolic/aggregateExpr.h"
#include <llvm/Support/Casting.h>

namespace acslg::spec_generator {
    namespace symb = acslg::analyzer::symbolic;

    namespace {
        const std::string IND1 = "  ";
        const std::string IND2 = "    ";
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
                        if (!is_symbol_addr(addr))
                            continue;
                        // INFO(addr.get().dump());
                        // INFO(value->dump());

                        // If the current address corresponds to a pointer targeting a
                        // structure, the associated handling is deliberately omitted. This
                        // omission is justified by the design of the flat() traversal: the
                        // fields of the structure are enumerated and processed individually.
                        // Thus, treating the pointer itself would introduce redundancy.
                        if (is_symbol_addr(addr) && postPath->is_point_to_structure(addr))
                            continue;

                        // Track the address only if the value differs between pre/post states.
                        if (!postPath->isUnchanged(addr, *pre.getPaths().front()))
                            auto [_, ok] = assignedAddrs.try_emplace(addr.hash(), std::move(addr));
                    }
                }
            }

            auto oldPoint = pre.getStartPoint();
            std::unordered_set<symb::SourcePoint> allUsedPoints;
            for (auto &[_, addr] : assignedAddrs) {
                auto acslExpected =
                    addr.get().getACSLOfValue({.predefinedLabels = {{oldPoint, "Old"}}}, oldPoint);
                if (!acslExpected) {
                    WARN("Value of {" + addr.get().dump() + "} getACSL failed.");
                    continue;
                }
                auto &[addrStr, usedPoints] = acslExpected.value();
                spec += addrStr + ", ";
                allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                     std::make_move_iterator(usedPoints.end()));
            }

            if (spec.empty())
                return std::pair{IND1 + std::string("assigns \\nothing;\n"),
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
            int idx = 0;

            for (auto &pathPtr : post.getPaths()) {
                const auto &path = *pathPtr;
                if (path.getPathState() != analyzer::Path::PathState::Return)
                    continue;

                std::unordered_map<size_t, const symb::AddressBox> assignedAddrs;
                for (auto &&[a, v] : path.getMemoryState().flat()) {
                    auto fromRoot = a.get().getFromRoot();
                    if (fromRoot == std::nullopt)
                        continue;
                    if (!prePath->getVarAddr().contains(fromRoot.value()))
                        continue;
                    if (!is_symbol_addr(a))
                        continue;
                    if (is_symbol_addr(a) && path.is_point_to_structure(a))
                        continue;
                    if (!path.isUnchanged(a, *pre.getPaths().front()))
                        // The address was modified on this path; record it for the assigns
                        // clause of this behavior.
                        (void)assignedAddrs.try_emplace(a.hash(), std::move(a));
                }

                std::string assignsSpec;
                for (auto &[_, a] : assignedAddrs) {
                    auto acslExpected =
                        a.get().getACSLOfValue({.predefinedLabels = {{oldPoint, "Old"}}}, oldPoint);
                    if (!acslExpected) {
                        WARN("Value of {" + a.get().dump() + "} getACSL failed.");
                        continue;
                    }
                    auto &[addrStr, usedPoints] = acslExpected.value();
                    assignsSpec += addrStr + ", ";
                    allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                         std::make_move_iterator(usedPoints.end()));
                }
                if (assignsSpec.empty())
                    assignsSpec = "\\nothing";
                else
                    assignsSpec.erase(assignsSpec.size() - 2);

                std::vector<std::string> ensures;

                // result
                if (auto &ret = path.getReturnExpr()) {
                    if (auto expected = ret.value()->simplifiedExpr()->getACSL(
                            {.predefinedLabels = {{oldPoint, "Old"}}})) {
                        auto &[spec, usedPoints] = expected.value();
                        if (usedPoints.empty())
                            if (!llvm::isa<symb::OverRangeExpr>(*ret.value()))
                                ensures.push_back("\\result == (" + spec + ")");
                            else
                                ensures.push_back(spec);
                        else {
                            // todo: maintain the write information, so can we know two source
                            // point are *same*. no way to do it right now :)
                            auto wrongExpected = ret.value()->simplifiedExpr()->getACSL(
                                {.noStateLabelFunctionAt = true});
                            assert(wrongExpected);
                            if (!llvm::isa<symb::OverRangeExpr>(*ret.value()))
                                ensures.push_back("\\result == (" + wrongExpected.value().first +
                                                  ")");
                            else
                                ensures.push_back(wrongExpected.value().first);
                        }
                    }
                }

                // Memory equations
                for (auto &&[addr, value] : path.getMemoryState().flat()) {
                    auto fromRoot = addr.get().getFromRoot();
                    if (fromRoot == std::nullopt)
                        continue;
                    if (!prePath->getVarAddr().contains(fromRoot.value()))
                        continue;
                    if (!is_symbol_addr(addr))
                        continue;
                    if (is_symbol_addr(addr) && llvm::isa<symb::Structure>(value.get()))
                        continue;

                    auto lhsOpt =
                        addr.get().getACSLOfValue({.predefinedLabels = {{oldPoint, "Old"}}});
                    if (!lhsOpt)
                        continue;

                    auto rhsOpt =
                        value->simplifiedExpr()->getACSL({.predefinedLabels = {{oldPoint, "Old"}}});
                    if (!rhsOpt)
                        continue;

                    // Wrap RHS to avoid precedence surprises in ACSL (e.g., bitwise ops vs ==).
                    ensures.push_back(lhsOpt.value().first + " == (" + rhsOpt.value().first + ")");
                    allUsedPoints.insert(std::make_move_iterator(lhsOpt.value().second.begin()),
                                         std::make_move_iterator(lhsOpt.value().second.end()));
                    allUsedPoints.insert(std::make_move_iterator(rhsOpt.value().second.begin()),
                                         std::make_move_iterator(rhsOpt.value().second.end()));
                }

                auto [assumesSpec, requiresSpec] = joinConj(path.getPathConditions(), oldPoint);
                if (ensures.empty() && assumesSpec.empty() && requiresSpec.empty() &&
                    assignsSpec == "\\nothing")
                    continue;

                std::string bname = "b" + std::to_string(idx++);
                std::string block;
                block += IND1 + "behavior " + bname + ":\n";
                if (!assumesSpec.empty())
                    block += IND2 + "assumes " + assumesSpec + ";\n";
                if (!requiresSpec.empty())
                    block += IND2 + "requires " + requiresSpec + ";\n";
                block += IND2 + "assigns " + assignsSpec + ";\n";
                for (auto &e : ensures)
                    block += IND2 + "ensures " + e + ";\n";

                behaviors.push_back(std::move(block));
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
                                                            symb::SourcePoint oldPoint) {
            std::string assumeStr;
            std::string requireStr;
            for (auto &cond : conds) {
                if (cond->isUnknown())
                    continue;
                auto simplified = cond->simplifiedExpr();
                auto rf = simplified->getACSL({.predefinedLabels = {{oldPoint, "Old"}}}, oldPoint);
                if (!rf || rf.value().first.empty())
                    continue;
                if (!rf.value().second.empty())
                    continue;
                auto &target =
                    llvm::isa<symb::OverRangeExpr>(simplified.get()) ? assumeStr : requireStr;
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
