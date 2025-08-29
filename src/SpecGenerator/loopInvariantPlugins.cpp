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
    tuple<optional<string>, bool, vector<unordered_map<Address, unique_ptr<SymbolicExpr>, AddressHash, AddressEqual>>> generate(
        const ProgramState &,
        const ProgramState &,
        const clang::Expr *,
        const clang::Stmt *,
        const clang::Stmt *,
        const LoopInfo &loopInfo) const override {
        if (loopInfo.symbolicLoopEntry_ == nullptr ||
            loopInfo.symbolicLoopEntry_->getPaths().size() != 1) {
            ERROR("SymbolicLoopEntry_ is in an invalid state");
        }

        if (!((loopInfo.index_ && loopInfo.indexBound_) ||
              (loopInfo.index_ == nullptr && loopInfo.indexBound_ == nullptr))) {
            ERROR("Index_ is in an invalid state");
        }
        for (const auto &[addr, pattern] : loopInfo.patternsMap_) {
            if (pattern && (*pattern).initialValue_ == nullptr)
                ERROR("PatternsMap_ is in an invalid state");
        }
        ostringstream oss;
        oss << "SymbolicLoopEntry: " << loopInfo.symbolicLoopEntry_->dump() << endl;
        oss << "index's address: " << (loopInfo.index_ ? loopInfo.index_->regularForm() : "NULL")
            << endl;
        oss << "index's bound: "
            << (loopInfo.indexBound_ ? loopInfo.indexBound_->simplifiedExpr()->regularForm()
                                     : "NULL")
            << endl;
        oss << "patterns: " << endl;

        for (auto &[addr, pattern] : loopInfo.patternsMap_) {
            oss << "address: " << addr.regularForm() << "\t";
            oss << "pattern: ";
            if (pattern) {
                oss << "{ initial value="
                    << ((*pattern).initialValue_
                            ? (*pattern).initialValue_->simplifiedExpr()->regularForm()
                            : "NULL")
                    << ", step=" << (*pattern).step_ << " }" << endl;
            } else {
                oss << "Value has changed in loop, but pattern is too complex to preprocess."
                    << endl;
            }
        }
        INFO(oss.str());
        return make_tuple(
            nullopt, true,
            vector<unordered_map<Address, unique_ptr<SymbolicExpr>, AddressHash, AddressEqual>>{});
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(CheckAndDumpLoopInfoPlugin, "checkAndDumpLoopInfo");

// class LinearInvariantPlugin : public LoopInvariantPlugin {
//   public:
//     LinearInvariantPlugin(const string &ID) : id_(ID) {}
//     string_view id() const override { return id_; }
//     tuple<optional<string>, bool, vector<unordered_map<Address, unique_ptr<SymbolicExpr>,
//     AddressHash, AddressEqual>>> generate(
//         const ProgramState &loopEntry,
//         const clang::Expr *cond,
//         const clang::Stmt *inc,
//         const clang::Stmt *body,
//         const LoopInfo &loopInfo) const override {
//         if (loopInfo.symbolicLoopEntry_ == nullptr ||
//             loopInfo.symbolicLoopEntry_->getPaths().size() != 1) {
//             ERROR("SymbolicLoopEntry_ is in an invalid state");
//         }

//         using mem_map_vector = vector<
//             unordered_map<Address, unique_ptr<SymbolicExpr>, AddressHash, AddressEqual>>;

//         // Ban this plugin when pointer/array exist, temporarily...
//         for (auto &[_, value] : loopEntry.getPaths()[0]->getMemoryState()) {
//             if (value->getType() == SymbolicExpr::ExprType::SymbolAddress) {
//                 return make_tuple(nullopt, true, mem_map_vector{});
//             }
//         }

//         auto symbolicState = loopInfo.symbolicLoopEntry_->clone();

//         auto exprs = symbolicState->stepExpr(cond);
//         int len    = exprs.size();
//         std::vector<std::unique_ptr<SymbolicExpr>> loopCond;
//         for (int i = 0; i < len; ++i)
//             loopCond.push_back(std::move(exprs[i]));
//         if (loopCond.empty())
//             return make_tuple(nullopt, true, mem_map_vector{});

//         symbolicState->step(body);
//         if (inc)
//             symbolicState->step(inc);

//         const auto &paths = symbolicState->getPaths();

//         auto invsAndPaths = buildLoopInvariant(std::move(loopCond), paths, loopEntry);

//         if (invsAndPaths.size() != 1)
//             UNIMPLEMENT("Only one path now");

//         string spec;
//         vector<unique_ptr<Path>> postStates;
//         for (auto &[inv, path] : invsAndPaths) {
//             // TODO: use behavior
//             if (inv != nullopt) {
//                 spec += *inv;
//                 spec += '\n';
//             }
//             postStates.emplace_back(std::move(path));
//         }
//         if (!spec.empty()) {
//             // remove '\n'
//             spec.pop_back();
//         }

//         if (spec.empty())
//             return make_tuple(nullopt, true, std::move(postStates));
//         return make_tuple(std::move(spec), true, std::move(postStates));
//     }

//   private:
//     string id_;
// };
// REGISTER_ACSL_PLUGIN(LinearInvariantPlugin, "StInGXPlugin");

class LoopAssignsPlugin : public LoopInvariantPlugin {
  public:
    LoopAssignsPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    tuple<optional<string>, bool, vector<unordered_map<Address, unique_ptr<SymbolicExpr>, AddressHash, AddressEqual>>> generate(
        const ProgramState &preState,
        const ProgramState &,
        const clang::Expr *cond,
        const clang::Stmt *inc,
        const clang::Stmt *body,
        const LoopInfo &loopInfo) const override {
        auto loopCurrent = loopInfo.symbolicLoopEntry_->clone();
        loopCurrent->step(cond);
        loopCurrent->step(body);
        loopCurrent->step(inc);

        using mem_map = unordered_map<Address, unique_ptr<SymbolicExpr>, AddressHash, AddressEqual>;
        using mem_map_vector =
            vector<unordered_map<Address, unique_ptr<SymbolicExpr>, AddressHash, AddressEqual>>;
        auto &entryMS = loopInfo.symbolicLoopEntry_->getPaths().at(0)->getMemoryState();

        string spec;
        vector<Address> assignedAddrs;
        mem_map_vector postState;

        // This plugin does not produce branches.
        postState.emplace_back();

        auto isLocal = [&](const Address &addr) {
            auto root        = addr.getFromRoot();
            auto &varAddrMap = preState.getPaths().at(0)->getVarAddr();
            if (!varAddrMap.contains(root))
                return true;
            return false;
        }; // isLocal end

        auto tryGetAsRange = [&](const Address &addr) -> optional<Address> {
            if (!addr.isOffseted())
                return nullopt;
            auto &from = addr.getFrom();
            if (auto range = std::visit(
                    [&](auto &&arg) -> optional<Address> {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            ERROR("Invalid state");
                        } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                            ERROR("Invalid state.");
                        } else if constexpr (std::is_same_v<
                                                 T, not_null<std::unique_ptr<const Address>>>) {
                            if (auto it = loopInfo.patternsMap_.find(*arg);
                                it != loopInfo.patternsMap_.end()) {
                                auto &pattern = it->second;
                                if (pattern == nullopt)
                                    TODO();
                                if (pattern.value().step_ != 1)
                                    TODO();
                                auto result = addr;
                                result.setOffset(pattern.value().initialValue_->clone());
                                result.setLength(loopInfo.loopCount_->simplifiedExpr());
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
            if (auto var = dynamic_cast<const Symbolic::Variable *>(offset.get())) {
                if (auto fromAddr =
                        std::get_if<not_null<std::unique_ptr<const Address>>>(&var->getFrom())) {
                    if (auto it = loopInfo.patternsMap_.find(**fromAddr);
                        it != loopInfo.patternsMap_.end()) {
                        auto &pattern = it->second;
                        if (pattern == nullopt)
                            TODO();
                        if (pattern.value().step_ != 1)
                            TODO();
                        auto result = addr;
                        result.setOffset(pattern.value().initialValue_->clone());
                        result.setLength(loopInfo.loopCount_->simplifiedExpr());
                        return result;
                    }
                }
            }
            return nullopt;
        }; // tryGetAsRange end

        for (auto &[addr, pattern] : loopInfo.patternsMap_) {
            using enum BinaryOpExpr::Operator;
            if (isLocal(addr))
                continue;
            if (auto range = tryGetAsRange(addr)) {
                if (pattern) {
                    postState.at(0)[range.value()] =
                        make_unique<BinaryOpExpr>(pattern.value().initialValue_->clone(), Add,
                                                  make_unique<LiteralExpr>(pattern.value().step_));
                } else {
                    postState.at(0)[range.value()] = UnknownExpr::makeUnknown();
                }
                assignedAddrs.push_back(std::move(range.value()));
            } else {
                if (pattern) {
                    postState.at(0)[addr] = make_unique<BinaryOpExpr>(
                        pattern.value().initialValue_->clone(), Add,
                        make_unique<BinaryOpExpr>(make_unique<LiteralExpr>(pattern.value().step_),
                                                  Multiply, loopInfo.loopCount_->clone()));
                } else {
                    postState.at(0)[addr] = UnknownExpr::makeUnknown();
                }
                assignedAddrs.push_back(addr);
            }
        }

        for (auto &addr : assignedAddrs) {
            auto valueForm = addr.regularFormOfValue();
            spec += valueForm + ", ";
        }

        if (spec.empty())
            return make_tuple(R"(loop assigns \nothing;)", true, mem_map_vector{});
        else
            return make_tuple("loop assigns " + spec.substr(0, spec.length() - 2) + ";", true,
                              std::move(postState));
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(LoopAssignsPlugin, "loopAssigns");

class ParadigmMaxMinPlugin : public LoopInvariantPlugin {
  public:
    ParadigmMaxMinPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    tuple<optional<string>, bool, vector<unordered_map<Address, unique_ptr<SymbolicExpr>, AddressHash, AddressEqual>>> generate(
        const ProgramState &,
        const ProgramState &,
        const clang::Expr *,
        const clang::Stmt *,
        const clang::Stmt *body,
        const LoopInfo &loopInfo) const override {
        using mem_map_vector =
            vector<unordered_map<Address, unique_ptr<SymbolicExpr>, AddressHash, AddressEqual>>;
        // Only work when loop is 1-step.
        int64_t indexStep;
        if (loopInfo.index_ == nullptr)
            ERROR("Index_ is in an invalid state");
        if (auto it = loopInfo.patternsMap_.find(*loopInfo.index_);
            it != loopInfo.patternsMap_.end()) {
            if (it->second == nullopt)
                ERROR("PatternsMap_ is in an invalid state");
            if ((*it->second).step_ != 1 && (*it->second).step_ != -1)
                return make_tuple(nullopt, true, mem_map_vector{});
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

            param_index = loopInfo.index_->regularFormOfValue();
            param_n     = loopInfo.indexBound_->regularForm();

            using enum SymbolicExpr::ExprType;
            if (loopInfo.indexBound_->getType() == Variable) {
                param_n = loopInfo.indexBound_->regularForm();
            }

            auto getAddress = [&](const clang::Expr *expr) -> unique_ptr<Address> {
                if (expr == nullptr)
                    return nullptr;
                try {
                    // May pass some strange expr to extractAddress.
                    return loopInfo.symbolicLoopEntry_->getPaths()[0]->extractAddress(expr);
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
                    if (idxAddr == nullptr || *idxAddr != *loopInfo.index_)
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
                            rhsAddr == nullptr || *rhsAddr != *loopInfo.index_)
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
                        if (auto it = loopInfo.patternsMap_.find(*addr);
                            it == loopInfo.patternsMap_.end() || it->second == nullopt ||
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
                if (loopInfo.symbolicLoopEntry_ == nullptr ||
                    loopInfo.symbolicLoopEntry_->getPaths().size() != 1)
                    ERROR("SymbolicLoopEntry_ is in an invaild state");
                if (!loopInfo.symbolicLoopEntry_->getPaths()[0]->getVarAddr().contains(varDecl))
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
                        if (loopInfo.indexBound_->getType() == Variable)
                            specTemplate = maxOnLeft ? FIND_MAX_LOOP_WITH_VAR_BOUND
                                                     : FIND_MIN_LOOP_WITH_VAR_BOUND;
                        else
                            specTemplate = maxOnLeft ? FIND_MAX_LOOP_WITH_OTHER_BOUND
                                                     : FIND_MIN_LOOP_WITH_OTHER_BOUND;
                        break;
                    case BO_GE:
                    case BO_GT:
                        if (loopInfo.indexBound_->getType() == Variable)
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
                if (loopInfo.symbolicLoopEntry_ == nullptr ||
                    loopInfo.symbolicLoopEntry_->getPaths().size() != 1)
                    ERROR("SymbolicLoopEntry_ is in an invaild state");
                auto symbolState = loopInfo.symbolicLoopEntry_->clone();
                symbolState->step(thenStmt);
                for (auto &path : symbolState->getPaths()) {
                    unique_ptr<Symbolic::Variable> maxVar{nullptr};
                    if (auto maxValue = path->getVarState(maxDecl);
                        maxValue && maxValue->getType() == SymbolicExpr::ExprType::Variable) {
                        maxVar = unique_ptr<Symbolic::Variable>(
                            static_cast<Symbolic::Variable *>(maxValue.release()));
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
                if (loopInfo.symbolicLoopEntry_ == nullptr ||
                    loopInfo.symbolicLoopEntry_->getPaths().size() != 1)
                    ERROR("SymbolicLoopEntry_ is in an invaild state");
                auto symbolState = loopInfo.symbolicLoopEntry_->clone();
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
            return make_tuple(nullopt, true, mem_map_vector{});
        else {
            spec.pop_back(); // earse \n
            return make_tuple(spec, true, mem_map_vector{});
        }
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(ParadigmMaxMinPlugin, "paradigmMaxMin");