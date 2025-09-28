// src/SpecGenerator/loopInvariantPlugins.cpp

#include "macros.h"
#include "specGenerator.h"
#include <memory>
#include "state.h"
#include "loopInvTemplates.h"
#include "utils.h"

using namespace std;
using namespace clang;

class CheckAndDumpLoopInfoPlugin : public LoopInvariantPlugin {
  public:
    CheckAndDumpLoopInfoPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    tuple<optional<string>, bool, vector<PostInfo>> generate(
        const ProgramState &,
        const ProgramState &,
        const clang::Expr *,
        const clang::Stmt *,
        const clang::Stmt *,
        const LoopInfo &loopInfo) const override {
        if (loopInfo.loopEntryInfo_) {
            auto &loopEntryInfo = loopInfo.loopEntryInfo_.value();
            if (loopEntryInfo.symbolicLoopEntry_->getPaths().size() != 1) {
                ERROR("`symbolicLoopEntry_` is in an invalid state");
            }
            INFO("`loopEntryInfo_` is set.");
            INFO(loopEntryInfo.symbolicLoopEntry_->dump());
        } else {
            INFO("`loopEntryInfo_` isn't set.");
        }

        if (loopInfo.indexInfo_) {
            auto &indexInfo = loopInfo.indexInfo_.value();
            INFO("`loopEntryInfo_` is set.");
            INFO("`indexAddr_`: " + indexInfo.indexAddr_->dump());
            INFO("`indexSymbolicValue_`: " + indexInfo.indexSymbolicValue_->dump());
            string opStr;
            switch (indexInfo.op_) {
#define BINARY_OPERATION(Name, Spelling)                                                           \
    case BO_##Name: opStr = #Spelling; break;
#include <clang/AST/OperationKinds.def>
                default: UNREACHABLE();
            }
            INFO("`op_`: " + opStr);
            INFO("`indexBound_`: " + indexInfo.indexBound_->dump());
            INFO("`preciseLoopCount_`: " + indexInfo.preciseLoopCount_->dump());
            INFO("`maxLoopCount_`: " + indexInfo.maxLoopCount_->dump());
            INFO("`indexPattern_`: " + indexInfo.indexPattern_.dump());
        } else {
            INFO("indexInfo_ isn't set.");
        }

        if (loopInfo.patternInfo_) {
            auto &patternInfo = loopInfo.patternInfo_.value();
            INFO("patternInfo_ is set.");
            for (auto &[addr, pattern] : patternInfo.patternsMap_) {
                INFO("address: " + addr.get().dump());
                if (pattern)
                    INFO("pattern: " + pattern.value().dump());
                else
                    INFO("pattern: nullopt(too complex)");
            }
        } else {
            INFO("patternInfo_ isn't set.");
        }

        return make_tuple(nullopt, true, vector<PostInfo>{});
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(CheckAndDumpLoopInfoPlugin, "checkAndDumpLoopInfo");

class LinearInvariantPlugin : public LoopInvariantPlugin {
  public:
    LinearInvariantPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    tuple<optional<string>, bool, vector<PostInfo>> generate(
        const ProgramState &,
        const ProgramState &,
        const clang::Expr *cond,
        const clang::Stmt *inc,
        const clang::Stmt *body,
        const LoopInfo &loopInfo) const override {
        if (loopInfo.loopEntryInfo_ == nullopt || loopInfo.indexInfo_ == nullopt)
            ERROR("Dependencies are not met.");

        auto &loopEntryInfo = loopInfo.loopEntryInfo_.value();
        auto &indexInfo     = loopInfo.indexInfo_.value();

        if (loopEntryInfo.symbolicLoopEntry_->getPaths().size() != 1) {
            ERROR("SymbolicLoopEntry_ has something wrong, check the SetLoopEntryPlugin?");
        }

        using mem_map_vector = vector<AddressBoxMap<unique_ptr<SymbolicExpr>>>;

        unique_ptr<SymbolicExpr> loopCond;
        auto lhs = indexInfo.indexSymbolicValue_->clone();
        auto rhs = indexInfo.indexBound_->clone();
        switch (indexInfo.op_) {
            using enum clang::BinaryOperatorKind;
            using enum BinaryOpExpr::Operator;
            case BO_LT: {
                auto newRHS = std::make_unique<Symbolic::BinaryOpExpr>(
                    rhs->clone(), Subtract, std::make_unique<Symbolic::LiteralExpr>(1));
                loopCond = std::make_unique<Symbolic::BinaryOpExpr>(lhs->clone(), LessEqual,
                                                                    std::move(newRHS));
                break;
            }
            case BO_GT: {
                auto newRHS = std::make_unique<Symbolic::BinaryOpExpr>(
                    rhs->clone(), Add, std::make_unique<Symbolic::LiteralExpr>(1));
                loopCond = std::make_unique<Symbolic::BinaryOpExpr>(lhs->clone(), GreaterEqual,
                                                                    std::move(newRHS));
                break;
            }
            case BO_LE:
                loopCond = std::make_unique<Symbolic::BinaryOpExpr>(lhs->clone(), LessEqual,
                                                                    std::move(rhs));
                break;
            case BO_GE:
                loopCond = std::make_unique<Symbolic::BinaryOpExpr>(lhs->clone(), GreaterEqual,
                                                                    std::move(rhs));
                break;
            case BO_NE: {
                if (indexInfo.indexPattern_.step_ < 0) {
                    auto rhsPlus1 = std::make_unique<Symbolic::BinaryOpExpr>(
                        rhs->clone(), Add, std::make_unique<Symbolic::LiteralExpr>(1));
                    auto geExpr = std::make_unique<Symbolic::BinaryOpExpr>(
                        lhs->clone(), GreaterEqual, std::move(rhsPlus1));
                    loopCond = std::move(geExpr);
                } else if (indexInfo.indexPattern_.step_ > 0) {
                    auto rhsMinus1 = std::make_unique<Symbolic::BinaryOpExpr>(
                        rhs->clone(), Subtract, std::make_unique<Symbolic::LiteralExpr>(1));
                    auto leExpr = std::make_unique<Symbolic::BinaryOpExpr>(lhs->clone(), LessEqual,
                                                                           std::move(rhsMinus1));
                    loopCond    = std::move(leExpr);
                } else {
                    UNREACHABLE();
                }
                break;
            }
            default: ERROR("Unexpected operator, check `setIndexPlugin` may solve this problem.");
        }
        DEBUG(loopCond->dump());

        auto loopEntry   = loopEntryInfo.symbolicLoopEntry_->clone();
        auto loopCurrent = loopEntryInfo.symbolicLoopEntry_->clone();

        loopCurrent->step(cond);
        loopCurrent->step(body);
        if (inc)
            loopCurrent->step(inc);

        auto &paths = loopCurrent->getPaths();

        auto invsAndPaths = buildLoopInvariant(std::move(loopCond), *loopEntry, *loopCurrent);

        if (invsAndPaths.size() != 1)
            UNIMPLEMENT("Only support one path now");

        string spec;
        vector<PostInfo> postStates;
        for (auto &[inv, postInfo] : invsAndPaths) {
            // TODO: use behavior
            if (inv != nullopt) {
                spec += *inv;
                spec += '\n';
            }
            postStates.emplace_back(std::move(postInfo.first), std::move(postInfo.second));
        }
        if (!spec.empty()) {
            // remove '\n'
            spec.pop_back();
        }

        if (spec.empty())
            return make_tuple(nullopt, true, std::move(postStates));
        return make_tuple(std::move(spec), true, std::move(postStates));
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(LinearInvariantPlugin, "StInGXPlugin");

// todo: deal with complex range
class LoopAssignsPlugin : public LoopInvariantPlugin {
  public:
    LoopAssignsPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    tuple<optional<string>, bool, vector<PostInfo>> generate(
        const ProgramState &preState,
        const ProgramState &loopEntry,
        const clang::Expr *cond,
        const clang::Stmt *inc,
        const clang::Stmt *body,
        const LoopInfo &loopInfo) const override {
        if (loopInfo.loopEntryInfo_ == nullopt || loopInfo.indexInfo_ == nullopt ||
            loopInfo.patternInfo_ == nullopt)
            ERROR("Dependencies are not met.");

        auto &loopEntryInfo = loopInfo.loopEntryInfo_.value();
        auto &indexInfo     = loopInfo.indexInfo_.value();
        auto &patternInfo   = loopInfo.patternInfo_.value();

        if (loopEntryInfo.symbolicLoopEntry_->getPaths().size() != 1) {
            ERROR("SymbolicLoopEntry_ has something wrong, check the SetLoopEntryPlugin?");
        }

        auto loopCurrent = loopEntryInfo.symbolicLoopEntry_->clone();
        loopCurrent->step(cond);
        loopCurrent->step(body);
        loopCurrent->step(inc);

        auto &entryMS = loopEntryInfo.symbolicLoopEntry_->getPaths().at(0)->getMemoryState();

        string spec;
        vector<AddressBox> assignedAddrs;
        vector<PostInfo> postInfo;

        // This plugin does not produce branches.
        postInfo.emplace_back();
        auto &memoryMap = postInfo.at(0).memoryMap_;

        auto isLocal = [&](const Address &addr) {
            auto root = addr.getFromRoot();
            if (root == nullopt)
                TODO();
            auto &varAddrMap = preState.getPaths().at(0)->getVarAddr();
            if (!varAddrMap.contains(root.value()))
                return true;
            return false;
        }; // isLocal end

        auto tryGetAsRange = [&](const Address &addr) -> optional<SymbolAddress> {
            if (addr.getAddressType() != Address::AddressType::SymbolAddr)
                return nullopt;
            auto symbolAddr = dynamic_cast<const SymbolAddress &>(addr);
            auto from       = symbolAddr.getFrom();
            // If the base address itself is x-step, then there is no need to check the
            // offset (or to check it for reliability).
            if (auto range = std::visit(
                    [&](auto &&arg) -> optional<SymbolAddress> {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            ERROR("Invalid state");
                        } else if constexpr (std::is_same_v<
                                                 T, not_null<std::unique_ptr<const Address>>>) {
                            if (auto it = patternInfo.patternsMap_.find(*arg);
                                it != patternInfo.patternsMap_.end()) {
                                auto &pattern = it->second;
                                if (pattern == nullopt)
                                    TODO();
                                auto result = symbolAddr;
                                result.setOffset(
                                    make_unique<LiteralExpr>(SymbolAddress::ZERO_OFFSET));
                                if (!indexInfo.preciseLoopCount_->isUnknown())
                                    result.setLength(indexInfo.preciseLoopCount_->simplifiedExpr());
                                else
                                    result.setLength(indexInfo.maxLoopCount_->simplifiedExpr());
                                return result;
                            }
                        }
                        return nullopt;
                    },
                    from))
                return range;
            auto offset = symbolAddr.getOffset();
            // Is offset x-step?
            if (auto var = dynamic_cast<const Symbolic::Variable *>(offset.get())) {
                return std::visit(
                    [&](auto &&arg) -> optional<SymbolAddress> {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            TODO();
                        } else if constexpr (std::is_same_v<
                                                 T, not_null<std::unique_ptr<const Address>>>) {
                            if (auto it = patternInfo.patternsMap_.find(*arg);
                                it != patternInfo.patternsMap_.end()) {
                                auto &pattern = it->second;
                                if (pattern == nullopt)
                                    TODO();
                                auto result = symbolAddr;
                                result.setOffset(pattern.value().initialValue_->clone());
                                if (!indexInfo.preciseLoopCount_->isUnknown())
                                    result.setLength(indexInfo.preciseLoopCount_->simplifiedExpr());
                                else
                                    result.setLength(indexInfo.maxLoopCount_->simplifiedExpr());
                                return result;
                            } else {
                                TODO();
                            }
                        }
                    },
                    var->getFrom());
            }
            return nullopt;
        }; // tryGetAsRange end

        for (auto &[addr, pattern] : patternInfo.patternsMap_) {
            using enum BinaryOpExpr::Operator;
            if (isLocal(addr))
                continue;
            if (auto range = tryGetAsRange(addr)) {
                if (pattern) {
                    auto [_, ok] = memoryMap.emplace(range.value(),
                                                     UnknownExpr::makeUnknown().into_underlying());
                    // todo
                    // make_unique<BinaryOpExpr>(pattern.value().initialValue_->clone(), Add,
                    //                           make_unique<LiteralExpr>(pattern.value().step_)));
                    if (!ok && !indexInfo.preciseLoopCount_->isUnknown())
                        UNREACHABLE();
                } else {
                    auto [_, ok] = memoryMap.emplace(range.value(),
                                                     UnknownExpr::makeUnknown().into_underlying());
                    if (!ok && !indexInfo.preciseLoopCount_->isUnknown())
                        UNREACHABLE();
                }
                assignedAddrs.emplace_back(std::move(range.value()));
            } else {
                if (pattern) {
                    auto [_, ok] = memoryMap.emplace(
                        addr, make_unique<BinaryOpExpr>(
                                  pattern.value().initialValue_->clone(), Add,
                                  make_unique<BinaryOpExpr>(
                                      make_unique<LiteralExpr>(pattern.value().step_), Multiply,
                                      indexInfo.preciseLoopCount_->clone())));
                    if (!ok)
                        UNREACHABLE();
                } else {
                    auto [_, ok] =
                        memoryMap.emplace(addr, UnknownExpr::makeUnknown().into_underlying());
                    if (!ok)
                        UNREACHABLE();
                }
                assignedAddrs.push_back(addr);
            }
        }

        unordered_set<size_t> solvedAddrsHashs {};
        for (auto &addr : assignedAddrs) {
            for (auto &path : loopEntry.getPaths()) {
                auto concreteAddr = getSubstitutedAddr(addr, *path);
                if(solvedAddrsHashs.contains(concreteAddr->hash()))
                    continue;
                solvedAddrsHashs.insert(concreteAddr->hash());
                if (concreteAddr->getAddressType() == Address::AddressType::SymbolAddr) {
                    auto &symbolAddr         = dynamic_cast<SymbolAddress &>(addr.get());
                    auto &symbolConcreteAddr = dynamic_cast<SymbolAddress &>(*concreteAddr);
                    if (symbolConcreteAddr.getOffset()->isUnknown()) {
                        auto valueForm = addr.get().regularFormOfValue("\\at(", ", LoopEntry)");
                        if (valueForm == nullopt) {
                            WARN("Value of {" + addr.get().dump() + "} has not regular form");
                            continue;
                        }
                        spec += valueForm.value() + ", ";
                        continue;
                    }
                }
                auto valueForm = concreteAddr->regularFormOfValue();
                if (valueForm == nullopt) {
                    WARN("Value of {" + concreteAddr->dump() + "} has not regular form");
                    continue;
                }
                spec += valueForm.value() + ", ";
            }
        }

        if (spec.empty())
            return make_tuple(R"(loop assigns \nothing;)", true, vector<PostInfo>{});
        else
            return make_tuple("loop assigns " + spec.substr(0, spec.length() - 2) + ";", true,
                              std::move(postInfo));
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(LoopAssignsPlugin, "loopAssigns");

class ParadigmMaxMinPlugin : public LoopInvariantPlugin {
  public:
    ParadigmMaxMinPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    tuple<optional<string>, bool, vector<PostInfo>> generate(
        const ProgramState &,
        const ProgramState &,
        const clang::Expr *,
        const clang::Stmt *,
        const clang::Stmt *body,
        const LoopInfo &loopInfo) const override {
        if (loopInfo.loopEntryInfo_ == nullopt || loopInfo.indexInfo_ == nullopt ||
            loopInfo.patternInfo_ == nullopt)
            ERROR("Dependencies are not met.");

        auto &loopEntryInfo = loopInfo.loopEntryInfo_.value();
        auto &indexInfo     = loopInfo.indexInfo_.value();
        auto &patternInfo   = loopInfo.patternInfo_.value();

        if (loopEntryInfo.symbolicLoopEntry_->getPaths().size() != 1) {
            ERROR("SymbolicLoopEntry_ has something wrong, check the SetLoopEntryPlugin?");
        }

        // Only work when loop is 1-step.
        int64_t indexStep;
        if (auto it = patternInfo.patternsMap_.find(*indexInfo.indexAddr_);
            it != patternInfo.patternsMap_.end()) {
            if (it->second == nullopt)
                ERROR("PatternsMap_ is in an invalid state");
            if ((*it->second).step_ != 1 && (*it->second).step_ != -1)
                return make_tuple(nullopt, true, vector<PostInfo>{});
            else
                indexStep = (*it->second).step_;
        } else {
            ERROR("PatternsMap_ is in an invalid state");
        }

        string spec;

        // Hook which deals with every if.
        auto ifVisitor = StmtVisitor{[&](const clang::IfStmt *s) {
            if (s == nullptr)
                return;

            optional<StringTemplate> specTemplate{nullopt};
            // Parameters for template filling, see StringTemplate for more information.
            optional<string> param_n{nullopt}, param_array{nullopt}, param_index{nullopt},
                param_m{nullopt};

            param_index = indexInfo.indexAddr_->regularFormOfValue();
            param_n     = indexInfo.indexBound_->regularForm();

            using enum SymbolicExpr::ExprType;

            auto getAddress =
                [&](const clang::Expr *expr) -> optional<not_null<unique_ptr<Symbolic::Address>>> {
                if (expr == nullptr)
                    return nullopt;
                try {
                    // May pass some strange expr to extractAddress.
                    return loopEntryInfo.symbolicLoopEntry_->getPaths().at(0)->extractLValue(expr);
                } catch (...) { return nullopt; }
            }; // getAddress end

            // Is expr 'p[i]', *(p+i) or *it where it == p+i?
            auto parseIndexedArray = [&](const clang::Expr *expr) {
                if (expr == nullptr)
                    return false;

                if (auto arraySub = dyn_cast_if_present<clang::ArraySubscriptExpr>(
                        expr->IgnoreParenImpCasts())) {
                    // p[i]
                    auto idxAddr = getAddress(arraySub->getIdx());
                    // Is 'i' loop's index?
                    if (idxAddr == nullopt || *idxAddr.value() != *indexInfo.indexAddr_)
                        return false;

                    if (auto addr = getAddress(arraySub->getBase())) {
                        auto baseStr = addr.value()->regularFormOfValue();
                        if (baseStr == nullopt) {
                            WARN("Value of {" + addr.value()->dump() + "} has no regular form");
                            return false;
                        }
                        param_array = baseStr.value();
                        return true;
                    } else
                        return false;
                } else if (auto unary = dyn_cast_if_present<clang::UnaryOperator>(
                               expr->IgnoreParenImpCasts());
                           unary && unary->getOpcode() == UO_Deref) {
                    if (auto bin = dyn_cast_if_present<clang::BinaryOperator>(
                            unary->getSubExpr()->IgnoreParenImpCasts());
                        bin && bin->getOpcode() == BO_Add) {
                        // *(p+i)

                        // Is 'i' loop's index?
                        if (auto rhsAddr = getAddress(bin->getRHS());
                            rhsAddr == nullopt || *rhsAddr.value() != *indexInfo.indexAddr_)
                            return false;
                        if (auto addr = getAddress(bin->getLHS())) {
                            auto baseStr = addr.value()->regularFormOfValue();
                            if (baseStr == nullopt) {
                                WARN("Value of {" + addr.value()->dump() + "} has no regular form");
                                return false;
                            }
                            param_array = baseStr.value();
                            return true;
                        } else {
                            return false;
                        }

                    } else if (auto declRef = dyn_cast_if_present<clang::DeclRefExpr>(
                                   unary->getSubExpr()->IgnoreParenImpCasts())) {
                        // *it

                        auto addr = getAddress(declRef);
                        if (addr == nullopt)
                            return false;
                        // Does this variable step same as loop?
                        if (auto it = patternInfo.patternsMap_.find(*addr.value());
                            it == patternInfo.patternsMap_.end() || it->second == nullopt ||
                            (*it->second).step_ != indexStep)
                            return false;
                        auto baseStr = addr.value()->regularFormOfValue();
                        if (baseStr == nullopt) {
                            WARN("Value of {" + addr.value()->dump() + "} has no regular form");
                            return false;
                        }
                        param_array = baseStr.value();
                        return true;
                    }
                } else {
                    return false;
                }
                UNREACHABLE();
            }; // parseIndexedArray end

            auto isLocal = [&](const clang::VarDecl *varDecl) {
                if (!loopEntryInfo.symbolicLoopEntry_->getPaths().at(0)->getVarAddr().contains(
                        varDecl))
                    return true;
                return false;
            }; // isLocal end

            const clang::VarDecl *maxDecl{nullptr}; // max
            clang::Expr *elementExpr{nullptr};      // p[i]

            // Does if's condition has form 'max < p[i]' or 'p[i] > max'?
            auto ifCond = s->getCond();
            if (auto bin =
                    dyn_cast_if_present<clang::BinaryOperator>(ifCond->IgnoreParenImpCasts())) {
                using enum clang::BinaryOperatorKind;
                using enum clang::UnaryOperatorKind;

                clang::DeclRefExpr *maxExpr{nullptr};
                bool maxOnLeft = true;

                // Where is 'max'?
                if (auto declRef = dyn_cast_if_present<clang::DeclRefExpr>(
                        bin->getLHS()->IgnoreParenImpCasts())) {
                    maxExpr     = declRef;
                    elementExpr = bin->getRHS()->IgnoreParenImpCasts();
                } else if (auto declRef = dyn_cast_if_present<clang::DeclRefExpr>(
                               bin->getRHS()->IgnoreParenImpCasts())) {
                    maxExpr     = declRef;
                    elementExpr = bin->getLHS()->IgnoreParenImpCasts();
                    maxOnLeft   = false;
                } else {
                    return;
                }

                // Operator is '<', '<=', '>' or '>='.
                switch (bin->getOpcode()) {
                    case BO_LE:
                    case BO_LT:
                        if (indexInfo.indexBound_->getType() == Variable)
                            specTemplate = maxOnLeft ? FIND_MAX_LOOP_WITH_VAR_BOUND
                                                     : FIND_MIN_LOOP_WITH_VAR_BOUND;
                        else
                            specTemplate = maxOnLeft ? FIND_MAX_LOOP_WITH_OTHER_BOUND
                                                     : FIND_MIN_LOOP_WITH_OTHER_BOUND;
                        break;
                    case BO_GE:
                    case BO_GT:
                        if (indexInfo.indexBound_->getType() == Variable)
                            specTemplate = maxOnLeft ? FIND_MIN_LOOP_WITH_VAR_BOUND
                                                     : FIND_MAX_LOOP_WITH_VAR_BOUND;
                        else
                            specTemplate = maxOnLeft ? FIND_MIN_LOOP_WITH_OTHER_BOUND
                                                     : FIND_MAX_LOOP_WITH_OTHER_BOUND;
                        break;
                    default: return;
                }
                DEBUG("Operator matched.");

                // Verify 'max'.
                if (auto var = dyn_cast<VarDecl>(maxExpr->getDecl());
                    var && var->getCanonicalDecl()) {
                    maxDecl = var->getCanonicalDecl();
                    if (isLocal(maxDecl))
                        return;
                    param_m = maxDecl->getNameAsString();
                } else {
                    return;
                }
                DEBUG("Max matched.");

                // Deal with p[i].
                if (!parseIndexedArray(elementExpr))
                    return;
                DEBUG("p[i] matched.");
            }

            if (specTemplate == nullopt || param_n == nullopt || param_array == nullopt ||
                param_index == nullopt || param_m == nullopt)
                UNREACHABLE();

            // Verify 'then' of if.
            if (auto thenStmt = s->getThen()) {
                auto symbolState = loopEntryInfo.symbolicLoopEntry_->clone();
                symbolState->step(thenStmt);
                for (auto &path : symbolState->getPaths()) {
                    unique_ptr<Symbolic::Variable> maxVar{nullptr};
                    if (auto maxValue = path->getVarState(maxDecl);
                        maxValue->getType() == SymbolicExpr::ExprType::Variable) {
                        maxVar = unique_ptr<Symbolic::Variable>(static_cast<Symbolic::Variable *>(
                            std::move(maxValue).into_underlying().release()));
                    } else {
                        return;
                    }

                    if (auto elementAddr = getAddress(elementExpr)) {
                        if (!std::visit(
                                [&](auto &&arg) -> bool {
                                    using T = std::decay_t<decltype(arg)>;
                                    if constexpr (std::is_same_v<T, std::monostate>) {
                                        TODO();
                                    } else if constexpr (std::is_same_v<
                                                             T, not_null<std::unique_ptr<
                                                                    const Symbolic::Address>>>) {
                                        return *arg == *elementAddr.value();
                                    }
                                },
                                maxVar->getFrom())) {
                            return;
                        }
                    } else {
                        return;
                    }
                }
            } else {
                return;
            }
            DEBUG("Then Verified.");

            // Verify 'else' of if.
            if (auto elseStmt = s->getElse()) {
                auto symbolState = loopEntryInfo.symbolicLoopEntry_->clone();
                symbolState->step(elseStmt);
                for (auto &path : symbolState->getPaths()) {
                    if (auto varAddrIt = path->getVarAddr().find(maxDecl);
                        varAddrIt != path->getVarAddr().end()) {
                        auto &maxAddr = varAddrIt->second;
                        if (!path->isUnchanged(*maxAddr))
                            return;
                    } else {
                        ERROR("Can't find maxDecl after step, something must be wrong.");
                    }
                }
            }
            DEBUG("Else Verified.");

            // Pretty sure we have found a 'find_max' loop.
            spec += (*specTemplate)
                        .to_string(NameMap{{"n", *param_n},
                                           {"array", *param_array},
                                           {"index", *param_index},
                                           {"m", *param_m}}) +
                    "\n";
        }}; // ifVisitor end
        ifVisitor.runOn(body);

        if (spec.empty())
            return make_tuple(nullopt, true, vector<PostInfo>{});
        else {
            spec.pop_back(); // earse \n
            return make_tuple(spec, true, vector<PostInfo>{});
        }
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(ParadigmMaxMinPlugin, "paradigmMaxMin");

class LoopVariantPlugin : public LoopInvariantPlugin {
  public:
    LoopVariantPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    tuple<optional<string>, bool, vector<PostInfo>> generate(
        const ProgramState &,
        const ProgramState &,
        const clang::Expr *,
        const clang::Stmt *,
        const clang::Stmt *,
        const LoopInfo &loopInfo) const override {
        if (loopInfo.indexInfo_ == nullopt)
            ERROR("Dependencies are not met.");

        auto &indexInfo = loopInfo.indexInfo_.value();

        // Yes, the expression of the loop variant is maxLoopCount. :)
        auto regForm = indexInfo.maxLoopCount_->simplifiedExpr()->regularForm();
        if (regForm == nullopt) {
            WARN("Variant {" + indexInfo.maxLoopCount_->simplifiedExpr()->dump() +
                 "} has no regular form.");
            return make_tuple(nullopt, true, vector<PostInfo>{});
        }
        auto spec = "loop variant " + regForm.value() + ";";
        return make_tuple(std::move(spec), true, vector<PostInfo>{});
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(LoopVariantPlugin, "loopVariant");