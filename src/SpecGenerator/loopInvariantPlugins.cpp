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
    bool needSubstituteAddress() const override { return false; }
    bool needSubstituteExpr() const override { return false; }
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
    case BO_##Name: opStr = #Spelling;
#include <clang/AST/OperationKinds.def>
            }
            INFO("`op_`: " + opStr);
            INFO("`indexBound_`: " + indexInfo.indexBound_->dump());
            INFO("`loopCount_`: " + indexInfo.loopCount_->dump());
            INFO("`indexPattern_`: " + indexInfo.indexPattern_.dump());
        } else {
            INFO("indexInfo_ isn't set.");
        }

        if (loopInfo.patternInfo_) {
            auto &patternInfo = loopInfo.patternInfo_.value();
            INFO("patternInfo_ is set.");
            for (auto &[addr, pattern] : patternInfo.patternsMap_) {
                INFO("address: " + addr.dump());
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
    bool needSubstituteAddress() const override { return true; }
    bool needSubstituteExpr() const override { return true; }
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

        using mem_map_vector =
            vector<unordered_map<Address, unique_ptr<SymbolicExpr>, AddressHash>>;

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
    bool needSubstituteAddress() const override { return true; }
    bool needSubstituteExpr() const override { return false; }
    tuple<optional<string>, bool, vector<PostInfo>> generate(
        const ProgramState &preState,
        const ProgramState &,
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
        vector<Address> assignedAddrs;
        vector<PostInfo> postInfo;

        // This plugin does not produce branches.
        postInfo.emplace_back();
        auto &memoryMap = postInfo.at(0).memoryMap_;

        auto isLocal = [&](const Address &addr) {
            auto root = addr.getFromRoot();
            if (root == nullptr)
                TODO();
            auto &varAddrMap = preState.getPaths().at(0)->getVarAddr();
            if (!varAddrMap.contains(root))
                return true;
            return false;
        }; // isLocal end

        auto tryGetAsRange = [&](const Address &addr) -> optional<Address> {
            if (!addr.isOffseted())
                return nullopt;
            auto &from = addr.getFrom();
            // If the base address itself is x-step, then there is no need to check the
            // offset (or to check it for reliability).
            if (auto range = std::visit(
                    [&](auto &&arg) -> optional<Address> {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            ERROR("Invalid state");
                        } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                            ERROR("Invalid state.");
                        } else if constexpr (std::is_same_v<
                                                 T, not_null<std::unique_ptr<const Address>>>) {
                            if (auto it = patternInfo.patternsMap_.find(*arg);
                                it != patternInfo.patternsMap_.end()) {
                                auto &pattern = it->second;
                                if (pattern == nullopt)
                                    TODO();
                                if (pattern.value().step_ != 1)
                                    TODO();
                                auto result = addr;
                                result.setOffset(make_unique<LiteralExpr>(Address::ZERO_OFFSET));
                                result.setLength(indexInfo.loopCount_->simplifiedExpr());
                                return result;
                            }
                        } else if constexpr (std::is_same_v<
                                                 T, std::pair<
                                                        not_null<shared_ptr<const Structure::Info>>,
                                                        const size_t>>) {
                            TODO();
                        }
                        return nullopt;
                    },
                    from))
                return range;
            auto offset = addr.getOffset();
            // Is offset x-step?
            if (auto var = dynamic_cast<const Symbolic::Variable *>(offset.get())) {
                if (auto fromAddr =
                        std::get_if<not_null<std::unique_ptr<const Address>>>(&var->getFrom())) {
                    if (auto it = patternInfo.patternsMap_.find(**fromAddr);
                        it != patternInfo.patternsMap_.end()) {
                        auto &pattern = it->second;
                        if (pattern == nullopt)
                            TODO();
                        if (pattern.value().step_ != 1)
                            TODO();
                        auto result = addr;
                        result.setOffset(pattern.value().initialValue_->clone());
                        result.setLength(indexInfo.loopCount_->simplifiedExpr());
                        return result;
                    }
                }
            }
            return nullopt;
        }; // tryGetAsRange end

        for (auto &[addr, pattern] : patternInfo.patternsMap_) {
            using enum BinaryOpExpr::Operator;
            if (isLocal(addr))
                continue;
            if (auto range = tryGetAsRange(addr)) {
                if (pattern) {
                    auto [_, ok] = memoryMap.emplace(range.value(), UnknownExpr::makeUnknown());
                    // todo
                    // make_unique<BinaryOpExpr>(pattern.value().initialValue_->clone(), Add,
                    //                           make_unique<LiteralExpr>(pattern.value().step_)));
                    if (!ok)
                        UNREACHABLE();
                } else {
                    auto [_, ok] = memoryMap.emplace(range.value(), UnknownExpr::makeUnknown());
                    if (!ok)
                        UNREACHABLE();
                }
                assignedAddrs.push_back(std::move(range.value()));
            } else {
                if (pattern) {
                    auto [_, ok] = memoryMap.emplace(
                        addr, make_unique<BinaryOpExpr>(
                                  pattern.value().initialValue_->clone(), Add,
                                  make_unique<BinaryOpExpr>(
                                      make_unique<LiteralExpr>(pattern.value().step_), Multiply,
                                      indexInfo.loopCount_->clone())));
                    if (!ok)
                        UNREACHABLE();
                } else {
                    auto [_, ok] = memoryMap.emplace(addr, UnknownExpr::makeUnknown());
                    if (!ok)
                        UNREACHABLE();
                }
                assignedAddrs.push_back(addr);
            }
        }

        for (auto &addr : assignedAddrs) {
            auto valueForm = addr.regularFormOfValue();
            spec += valueForm + ", ";
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
    bool needSubstituteAddress() const override { return true; }
    bool needSubstituteExpr() const override { return false; }
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

        // Hook that deals with every if.
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
            if (indexInfo.indexBound_->getType() == Variable) {
                param_n = indexInfo.indexBound_->regularForm();
            }

            auto getAddress = [&](const clang::Expr *expr) -> unique_ptr<Address> {
                if (expr == nullptr)
                    return nullptr;
                try {
                    // May pass some strange expr to extractAddress.
                    return loopEntryInfo.symbolicLoopEntry_->getPaths()[0]->extractAddress(expr);
                } catch (...) { return nullptr; }
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
                    if (idxAddr == nullptr || *idxAddr != *indexInfo.indexAddr_)
                        return false;

                    if (auto addr = getAddress(arraySub->getBase())) {
                        param_array = addr->regularFormOfValue();
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
                            rhsAddr == nullptr || *rhsAddr != *indexInfo.indexAddr_)
                            return false;
                        if (auto addr = getAddress(bin->getLHS())) {
                            param_array = addr->regularFormOfValue();
                            return true;
                        } else {
                            return false;
                        }

                    } else if (auto declRef = dyn_cast_if_present<clang::DeclRefExpr>(
                                   unary->getSubExpr()->IgnoreParenImpCasts())) {
                        // *it

                        auto addr = getAddress(declRef);
                        if (addr == nullptr)
                            return false;
                        // Does this variable step same as loop?
                        if (auto it = patternInfo.patternsMap_.find(*addr);
                            it == patternInfo.patternsMap_.end() || it->second == nullopt ||
                            (*it->second).step_ != indexStep)
                            return false;
                        param_array = addr->regularFormOfValue();
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
                return;

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
                        if (auto maxVarFrom =
                                get_if<not_null<unique_ptr<const Address>>>(&maxVar->getFrom())) {
                            if (**maxVarFrom != *elementAddr)
                                return;
                        } else
                            TODO();
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