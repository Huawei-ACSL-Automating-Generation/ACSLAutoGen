// src/SpecGenerator/looopInfoPlugins.cpp

#include "specGenerator.h"
#include "stringTemplate.h"
#include "macros.h"
#include "state.h"

using namespace std;
using namespace clang;

class SetLoopEntryPlugin : public LoopInfoPlugin {
  public:
    SetLoopEntryPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    bool parse(const ProgramState &pre,
               const Expr *,
               const Stmt *,
               const Stmt *,
               LoopInfo &loopInfo) const override {
        auto symbolicState = pre.clone();

        symbolicState->resymbolize();
        loopInfo.symbolicLoopEntry_ = std::move(symbolicState);
        return true;
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(SetLoopEntryPlugin, "setLoopEntry");

// Preprocess simple patterns of regions.
class SetPatternsPlugin : public LoopInfoPlugin {
  public:
    SetPatternsPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    bool parse(const ProgramState &loopEntry,
               const Expr *cond,
               const Stmt *inc,
               const Stmt *body,
               LoopInfo &loopInfo) const override {
        if (loopInfo.symbolicLoopEntry_ == nullptr ||
            loopInfo.symbolicLoopEntry_->getPaths().size() != 1) {
            ERROR("SymbolicLoopEntry_ has something wrong, check the SetLoopEntryPlugin?");
        }

        auto loopCurrent = loopInfo.symbolicLoopEntry_->clone();

        using pattern = LoopInfo::pattern;

        loopCurrent->step(cond);
        loopCurrent->step(body);
        loopCurrent->step(inc);

        auto getPatternsFromPath = [&](const Path &currentEntry) {
            unordered_map<Address, optional<const pattern>, AddressHash> patterns;
            auto &preMS = loopInfo.symbolicLoopEntry_->getPaths()[0]->getMemoryState();
            for (auto &[addr, value] : currentEntry.getMemoryState()) {
                if (preMS.find(addr) == preMS.end())
                    continue; // local variable
                if (*preMS.find(addr)->second == *value)
                    continue; // unchanged
                if (value->getType() == SymbolicExpr::ExprType::SymbolAddress) {
                    UNIMPLEMENT("Need Address::toLinearExpr");
                }
                // toLinearExpr hasn't been finished, use 'try' to avoid unexpected error.
                try {
                    auto entryExpr   = preMS.find(addr)->second->toLinearExpr();
                    auto currentExpr = value->toLinearExpr();

                    if (auto diff = currentExpr - entryExpr;
                        diff.all_homogeneous_terms_are_zero()) {
                        auto step                          = diff.inhomogeneous_term().get_si();
                        unique_ptr<SymbolicExpr> initValue = nullptr;

                        // Get the only initial value.
                        bool isTooComplex = false;
                        for (auto &path : loopEntry.getPaths()) {
                            if (isTooComplex)
                                break;
                            // Bad complexity, may need a wrapper to wrap the symbolicExpr
                            // completely.
                            for (auto &[addrInPre, valueInPre] : path->getMemoryState()) {
                                if (addrInPre != addr)
                                    continue;
                                if (initValue) {
                                    if (*initValue != *valueInPre) {
                                        isTooComplex = true;
                                        break;
                                    }
                                } else {
                                    initValue = valueInPre->clone();
                                }
                            }
                        }
                        if (isTooComplex) {
                            patterns.emplace(addr, nullopt);
                        } else {
                            patterns.emplace(addr, pattern{std::move(initValue), step});
                        }
                    } else {
                        patterns.emplace(addr, nullopt);
                    }
                } catch (...) { patterns.emplace(addr, nullopt); }
            }
            return patterns;
        }; // getPatternsFromPath end

        unordered_map<Address, optional<const pattern>, AddressHash> patterns;

        for (auto &path : loopCurrent->getPaths()) {
            switch (path->getPathState()) {
                using enum Path::PathState;
                case Break:
                case Continue:
                case Return: TODO();
                case Step: {
                    auto currentPatterns = getPatternsFromPath(*path);
                    if (patterns.empty())
                        patterns = std::move(currentPatterns);
                    else {
                        auto isEqual = [](const optional<const LoopInfo::pattern> &LHS,
                                          const optional<const LoopInfo::pattern> &RHS) {
                            if (LHS == nullopt && RHS == nullopt)
                                return true;
                            if (LHS && RHS) {
                                if (*(*LHS).initialValue_ != *(*RHS).initialValue_)
                                    UNREACHABLE();
                                if ((*LHS).step_ == (*RHS).step_)
                                    return true;
                            }
                            return false;
                        }; // isEqual end

                        // Is this addr has same pattern on every step-path?
                        for (auto &[addr, pattern] : patterns) {
                            if (auto it = currentPatterns.find(addr);
                                it == currentPatterns.end() || !isEqual(it->second, pattern))
                                patterns[addr] = nullopt;
                        }
                        for (auto &[addr, pattern] : currentPatterns) {
                            if (auto it = patterns.find(addr);
                                it == patterns.end() || !isEqual(it->second, pattern))
                                patterns[addr] = nullopt;
                        }
                    }
                    break;
                }
                default: UNREACHABLE();
            }
        }

        loopInfo.patternsMap_ = std::move(patterns);
        // TODO: may do another round to improve rubustness.
        return true;
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(SetPatternsPlugin, "setPatterns");

class SetIndexPlugin : public LoopInfoPlugin {
  public:
    SetIndexPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    bool parse(const ProgramState &preState,
               const Expr *cond,
               const Stmt *inc,
               const Stmt *body,
               LoopInfo &loopInfo) const override {
        auto sameAddressBetweenEveryPaths = [&](const Expr *expr) -> optional<unique_ptr<Address>> {
            LValueTarget lValue = (const VarDecl *)nullptr;
            for (auto &path : preState.getPaths()) {
                if (auto declValue = get_if<const VarDecl *>(&lValue);
                    declValue && *declValue == nullptr) {
                    // lValue is empty
                    lValue = path->extractLValue(expr);
                    continue;
                }

                auto nowLValue = path->extractLValue(expr);
                if (auto addrValue = get_if<unique_ptr<Address>>(&lValue),
                    nowAddrValue   = get_if<unique_ptr<Address>>(&nowLValue);
                    addrValue && nowAddrValue && **addrValue != **nowAddrValue) {
                    // lValue and nowLValue are both unique_ptr<Address> and not
                    // equal.
                    return nullopt;
                } else if (lValue != nowLValue) {
                    // lValue and nowLValue have distinct type or both VarDecl* and
                    // different.
                    return nullopt;
                }
            }
            if (auto declLValue = get_if<const VarDecl *>(&lValue)) {
                auto &varAddr = preState.getPaths()[0]->getVarAddr();
                if (auto it = varAddr.find(*declLValue); it != varAddr.end()) {
                    return unique_ptr<Address>(
                        static_cast<Address *>(it->second->clone().release()));
                } else {
                    ERROR("A varDecl* has no Address mapped, something must goes "
                          "wrong.");
                }
            } else if (auto addrLValue = get_if<unique_ptr<Address>>(&lValue)) {
                return std::move(*addrLValue);
            } else {
                ERROR("Variant does not contain a value, something goes wrong.");
            }
        }; // sameAddressBetweenEveryPaths end

        auto hasPattern = [&](const Address &addr) {
            for (auto &[a, _] : loopInfo.patternsMap_) {
                if (addr == a)
                    return true;
            }
            return false;
        }; // hasPattern end

        if (auto binExpr = dyn_cast<BinaryOperator>(cond->IgnoreParenImpCasts())) {
            auto sameValueBetweenEveryPaths =
                [&](const Expr *expr) -> optional<unique_ptr<SymbolicExpr>> {
                unique_ptr<SymbolicExpr> value;
                for (auto &path : preState.getPaths()) {
                    if (value == nullptr) {
                        // value is empty
                        auto [_, valueVector] = path->evalExpr(expr);
                        if (valueVector.size() != 1)
                            return nullopt;
                        value = std::move(valueVector[0]);
                        continue;
                    }

                    auto [_, valueVector] = path->evalExpr(expr);
                    if (valueVector.size() != 1)
                        return nullopt;

                    if (*value != *valueVector[0])
                        return nullopt;
                }
                return value;
            }; // sameValueBetweenEveryPaths end

            auto indexExpr = binExpr->getLHS()->IgnoreParenImpCasts();
            auto boundExpr = binExpr->getRHS()->IgnoreParenImpCasts();

            unique_ptr<Address> indexAddr;
            unique_ptr<SymbolicExpr> boundValue;

            if (auto addr = sameAddressBetweenEveryPaths(indexExpr)) {
                if (!hasPattern(**addr)) {
                    INFO("Index has no parseable pattern.");
                    return false;
                }
                indexAddr = std::move(*addr);
            } else {
                INFO("Same expr in different path points to different location!");
                return false;
            }

            if (auto value = sameValueBetweenEveryPaths(boundExpr)) {
                boundValue = std::move(*value);
            } else {
                INFO("Same expr in different path is evaluated to different value!");
                return false;
            }

            // TODO: more operators
            switch (binExpr->getOpcode()) {
                case BinaryOperator::Opcode::BO_LT:
                    boundValue = make_unique<BinaryOpExpr>(std::move(boundValue),
                                                           BinaryOpExpr::Operator::Subtract,
                                                           make_unique<LiteralExpr>((int64_t)1));
                    break;
                case BinaryOperator::Opcode::BO_GT:
                    boundValue = make_unique<BinaryOpExpr>(std::move(boundValue),
                                                           BinaryOpExpr::Operator::Add,
                                                           make_unique<LiteralExpr>((int64_t)1));
                    break;

                case BinaryOperator::Opcode::BO_LE:
                case BinaryOperator::Opcode::BO_GE:
                case BinaryOperator::Opcode::BO_NE: break;
                default:
                    // too complex
                    INFO("Loop's condition expr is too complex! Unimplemented binary "
                         "operator.");
                    return false;
            }

            loopInfo.index_      = std::move(indexAddr);
            loopInfo.indexBound_ = std::move(boundValue);
            return true;
        } else if (auto unaryExpr = dyn_cast<UnaryOperator>(cond->IgnoreParenImpCasts())) {
            // TODO: more operators
            if (unaryExpr->getOpcode() != UnaryOperatorKind::UO_Deref) {
                INFO("Loop's condition expr is too complex! Unimplemented unary "
                     "operator.");
                return false;
            }

            unique_ptr<Address> indexAddr;
            if (auto addr = sameAddressBetweenEveryPaths(unaryExpr)) {
                if (!hasPattern(**addr)) {
                    INFO("Index has no parseable pattern.");
                    return false;
                }
                indexAddr = std::move(*addr);
            } else {
                INFO("Same expr in different path points to different location!");
                return false;
            }

            loopInfo.index_      = std::move(indexAddr);
            loopInfo.indexBound_ = make_unique<LiteralExpr>((int64_t)0);
            return true;
        } else if (auto refExpr = dyn_cast<DeclRefExpr>(cond->IgnoreParenImpCasts())) {
            if (preState.getPaths().empty()) {
                ERROR("Pre-state has no path, something goes wrong.");
            }

            auto varDecl = dyn_cast<VarDecl>(refExpr->getDecl())
                               ? dyn_cast<VarDecl>(refExpr->getDecl())->getCanonicalDecl()
                               : nullptr;

            if (varDecl == nullptr) {
                INFO("Parsing loop's index vaibale has failed. Does loop's "
                     "condition expr have a "
                     "variable?");
                return false;
            }

            unique_ptr<Address> indexAddr;
            if (auto it = preState.getPaths()[0]->getVarAddr().find(varDecl);
                it != preState.getPaths()[0]->getVarAddr().end()) {
                if (!hasPattern(*it->second)) {
                    INFO("Index has no parseable pattern.");
                    return false;
                }
                indexAddr =
                    unique_ptr<Address>(static_cast<Address *>(it->second->clone().release()));
            } else {
                ERROR("A varDecl* has no Address mapped, something must goes wrong.");
            }

            loopInfo.index_      = std::move(indexAddr);
            loopInfo.indexBound_ = make_unique<LiteralExpr>((int64_t)0);
            return true;
        }

        INFO("Loop's condition expr is too complex.");
        return false;
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(SetIndexPlugin, "setIndex");