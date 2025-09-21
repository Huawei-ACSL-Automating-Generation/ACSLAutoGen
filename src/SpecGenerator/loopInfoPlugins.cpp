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
    bool parse(const ProgramState &,
               const ProgramState &loopEntry,
               const Expr *,
               const Stmt *,
               const Stmt *,
               LoopInfo &loopInfo) const override {
        auto symbolicState = loopEntry.clone();

        symbolicState->resymbolize();
        loopInfo.loopEntryInfo_.emplace(std::move(symbolicState));
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
    bool parse(const ProgramState &,
               const ProgramState &loopEntry,
               const Expr *cond,
               const Stmt *inc,
               const Stmt *body,
               LoopInfo &loopInfo) const override {
        if (loopInfo.loopEntryInfo_ == nullopt)
            ERROR("Dependencies are not met.");

        auto &loopEntryInfo = loopInfo.loopEntryInfo_.value();

        if (loopEntryInfo.symbolicLoopEntry_->getPaths().size() != 1) {
            ERROR("SymbolicLoopEntry_ has something wrong, check the SetLoopEntryPlugin?");
        }

        auto loopCurrent = loopEntryInfo.symbolicLoopEntry_->clone();

        using Pattern = LoopInfo::Pattern;

        loopCurrent->step(cond);
        loopCurrent->step(body);
        loopCurrent->step(inc);

        auto getPatternsFromPath = [&](const Path &currentEntry) {
            AddressBoxMap<optional<const Pattern>> patterns;
            auto &preVA = loopEntryInfo.symbolicLoopEntry_->getPaths().at(0)->getVarAddr();
            auto &preMS = loopEntryInfo.symbolicLoopEntry_->getPaths().at(0)->getMemoryState();
            for (auto &&[addr, currentExpr] : currentEntry.getMemoryState().flat()) {
                if (auto rootDecl = addr.get().getFromRoot();
                    rootDecl == nullopt || !preVA.contains(rootDecl.value()))
                    continue; // local variable
                if (auto preValue = preMS.read(addr)) {
                    if (*preValue.value() == *currentExpr)
                        continue; // unchanged
                } else {
                    if (currentEntry.isUnchanged(addr))
                        continue; // unchanged
                }
                optional<not_null<unique_ptr<SymbolicExpr>>> entryExpr;
                if (auto preValue = preMS.read(addr)) {
                    entryExpr = preValue.value()->clone();
                } else {
                    auto [hashAddrMap, _] = SymbolicExpr::collectUsedVarsAndAddrs(*currentExpr);
                    if (hashAddrMap.size() != 1) {
                        patterns.emplace(addr, nullopt);
                        continue;
                    }
                    if (visit([&](auto &&arg) -> bool { return isFrom(addr, *arg); },
                              hashAddrMap.begin()->second)) {
                        entryExpr = visit([](auto &&arg) { return arg->clone(); },
                                          hashAddrMap.begin()->second);
                    } else {
                        patterns.emplace(addr, nullopt);
                        continue;
                    }
                }

                if (entryExpr == nullopt)
                    UNREACHABLE();
                auto [hashAddrMap, hashIdMap] =
                    SymbolicExpr::collectUsedVarsAndAddrs(*currentExpr, *entryExpr.value());
                if (auto diff = currentExpr->toLinearExpr(hashIdMap) -
                                entryExpr.value()->toLinearExpr(hashIdMap);
                    diff.all_homogeneous_terms_are_zero()) {
                    auto step = diff.inhomogeneous_term().get_si();
                    patterns.emplace(addr,
                                     Pattern{entryExpr.value()->clone().into_underlying(), step});
                } else {
                    patterns.emplace(addr, nullopt);
                }
            }
            return patterns;
        }; // getPatternsFromPath end

        AddressBoxMap<optional<const Pattern>> patterns;

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
                        auto isEqual = [](const optional<const Pattern> &LHS,
                                          const optional<const Pattern> &RHS) {
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

        loopInfo.patternInfo_.emplace(std::move(patterns));
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
               const ProgramState &loopEntry,
               const Expr *cond,
               const Stmt *inc,
               const Stmt *body,
               LoopInfo &loopInfo) const override {
        if (loopInfo.loopEntryInfo_ == nullopt || loopInfo.patternInfo_ == nullopt)
            ERROR("Dependencies are not met.");

        auto &loopEntryInfo = loopInfo.loopEntryInfo_.value();
        auto &patternInfo   = loopInfo.patternInfo_.value();

        if (loopEntryInfo.symbolicLoopEntry_->getPaths().size() != 1) {
            ERROR("SymbolicLoopEntry_ has something wrong, check the SetLoopEntryPlugin?");
        }
        auto &entryPath = loopEntryInfo.symbolicLoopEntry_->getPaths().at(0);

        auto sameAddressBetweenEveryPaths =
            [&](const Expr *expr) -> optional<not_null<unique_ptr<Address>>> {
            auto lValue = optional<not_null<unique_ptr<Address>>>{};
            for (auto &path : loopEntry.getPaths()) {
                if (lValue == nullopt) {
                    lValue.emplace(path->extractLValue(expr));
                    continue;
                }

                auto nowLValue = path->extractLValue(expr);
                if (*lValue.value() != *nowLValue) {
                    // lValue and nowLValue are both unique_ptr<Address> and not
                    // equal.
                    return nullopt;
                }
            }
            if (lValue == nullopt)
                UNREACHABLE();
            return std::move(lValue);
        }; // sameAddressBetweenEveryPaths end

        auto hasPattern = [&](const Address &addr) -> optional<const LoopInfo::Pattern> {
            if (auto it = patternInfo.patternsMap_.find(addr); it != patternInfo.patternsMap_.end())
                return it->second;
            return nullopt;
        }; // hasPattern end

        auto unchangedAfterOneRound = [&](const Expr *expr) -> bool {
            auto state = loopEntryInfo.symbolicLoopEntry_->clone();
            state->step(cond);
            state->step(body);
            state->step(inc);
            auto [_, valueVector] = entryPath->evalExpr(expr);
            if (valueVector.size() != 1)
                ERROR("Do not support branch at here");
            auto preValue = std::move(valueVector[0]);

            for (auto &path : state->getPaths()) {
                std::tie(ignore, valueVector) = path->evalExpr(expr);
                if (valueVector.size() != 1)
                    ERROR("Do not support branch at here");
                auto currentValue = std::move(valueVector[0]);
                if (*preValue != *currentValue)
                    return false;
            }
            return true;
        }; // unchangedAfterOneRound end

        optional<not_null<unique_ptr<Address>>> indexAddr;
        optional<not_null<unique_ptr<Symbolic::SymbolicExpr>>> indexValue;
        optional<BinaryOperator::Opcode> opCode;
        optional<not_null<unique_ptr<SymbolicExpr>>> boundValue;
        optional<not_null<unique_ptr<SymbolicExpr>>> preciseLoopCount;
        optional<not_null<unique_ptr<SymbolicExpr>>> maxLoopCount;
        optional<LoopInfo::Pattern> indexPattern;
        optional<bool> isLocal;

        if (auto binExpr = dyn_cast<BinaryOperator>(cond->IgnoreParenImpCasts())) {
            auto sameValueBetweenEveryPaths =
                [&](const Expr *expr) -> optional<not_null<unique_ptr<SymbolicExpr>>> {
                optional<not_null<unique_ptr<SymbolicExpr>>> value;
                for (auto &path : loopEntry.getPaths()) {
                    if (value == nullopt) {
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

                    if (*value.value() != *valueVector[0])
                        return nullopt;
                }
                return value;
            }; // sameValueBetweenEveryPaths end

            auto indexExpr = binExpr->getLHS()->IgnoreParenImpCasts();
            auto boundExpr = binExpr->getRHS()->IgnoreParenImpCasts();

            using enum BinaryOperator::Opcode;

            if (auto addr = sameAddressBetweenEveryPaths(indexExpr)) {
                if (auto pattern = hasPattern(**addr); pattern == nullopt) {
                    INFO("Index has no parseable pattern.");
                    return false;
                } else {
                    indexPattern = std::move(pattern.value());

                    for (auto &prePath : preState.getPaths()) {
                        if (prePath->isActive()) {
                            if (prePath->getMemoryState().contains(*addr.value()))
                                isLocal = false;
                            else
                                isLocal = true;
                            break;
                        }
                    }

                    if (isLocal == nullopt)
                        UNREACHABLE();
                }
                indexAddr = std::move(*addr);
            } else {
                INFO("Same expr in different path points to different location!");
                return false;
            }

            auto [_, values] =
                loopEntryInfo.symbolicLoopEntry_->getPaths().at(0)->evalExpr(indexExpr);
            if (values.size() != 1)
                UNREACHABLE();
            indexValue = std::move(values.at(0));

            if (auto value = sameValueBetweenEveryPaths(boundExpr);
                value && unchangedAfterOneRound(boundExpr)) {
                boundValue = std::move(*value);
            } else {
                INFO("Boound expr is evaluated to different value in different path or changed "
                     "after one round.");
                return false;
            }

            // TODO: more operators
            switch (binExpr->getOpcode()) {
                case BO_LT:
                case BO_GT:
                case BO_LE:
                case BO_GE:
                case BO_NE: opCode = binExpr->getOpcode(); break;
                default:
                    // too complex
                    INFO("Loop's condition expr is too complex! Unimplemented binary "
                         "operator.");
                    return false;
            }

            using enum BinaryOpExpr::Operator;
            if (indexPattern == nullopt || boundValue == nullopt || opCode == nullopt)
                UNREACHABLE();

            // abs(n - i + step - 1)
            maxLoopCount = indexPattern.value().step_ > 0
                               ? make_unique<BinaryOpExpr>(
                                     make_unique<BinaryOpExpr>(
                                         boundValue.value()->clone(), Add,
                                         make_unique<LiteralExpr>(indexPattern.value().step_ - 1)),
                                     Subtract, indexPattern.value().initialValue_->clone())
                               : make_unique<BinaryOpExpr>(
                                     indexPattern.value().initialValue_->clone(), Subtract,
                                     make_unique<BinaryOpExpr>(
                                         boundValue.value()->clone(), Add,
                                         make_unique<LiteralExpr>(indexPattern.value().step_ + 1)));
            if (opCode.value() == BO_LE || opCode.value() == BO_GE)
                maxLoopCount = make_unique<BinaryOpExpr>(std::move(maxLoopCount.value()), Add,
                                                         make_unique<LiteralExpr>(1));

            if (abs(indexPattern.value().step_) == 1) {
                preciseLoopCount = maxLoopCount.value()->clone();
            } else {
                preciseLoopCount = UnknownExpr::makeUnknown().into_underlying();
            }
        } else if (auto unaryExpr = dyn_cast<UnaryOperator>(cond->IgnoreParenImpCasts())) {
            // TODO: more operators
            if (unaryExpr->getOpcode() != UnaryOperatorKind::UO_Deref) {
                INFO("Loop's condition expr is too complex! Unimplemented unary "
                     "operator.");
                return false;
            }

            if (auto addr = sameAddressBetweenEveryPaths(unaryExpr)) {
                if (auto pattern = hasPattern(**addr); pattern == nullopt) {
                    INFO("Index has no parseable pattern.");
                    return false;
                } else {
                    indexPattern = std::move(pattern.value());

                    for (auto &prePath : preState.getPaths()) {
                        if (prePath->isActive()) {
                            if (prePath->getMemoryState().contains(*addr.value()))
                                isLocal = false;
                            else
                                isLocal = true;
                            break;
                        }
                    }

                    if (isLocal == nullopt)
                        UNREACHABLE();
                }
                indexAddr = std::move(*addr);
            } else {
                INFO("Same expr in different path points to different location!");
                return false;
            }

            auto [_, values] =
                loopEntryInfo.symbolicLoopEntry_->getPaths().at(0)->evalExpr(unaryExpr);
            if (values.size() != 1)
                UNREACHABLE();
            indexValue = std::move(values.at(0));

            opCode     = BO_NE;
            boundValue = make_unique<LiteralExpr>((int64_t)0);

            using enum BinaryOpExpr::Operator;
            if (indexPattern == nullopt || boundValue == nullopt)
                UNREACHABLE();

            // abs(n - i + step - 1)
            maxLoopCount = indexPattern.value().step_ > 0
                               ? make_unique<BinaryOpExpr>(
                                     make_unique<BinaryOpExpr>(
                                         boundValue.value()->clone(), Add,
                                         make_unique<LiteralExpr>(indexPattern.value().step_ - 1)),
                                     Subtract, indexPattern.value().initialValue_->clone())
                               : make_unique<BinaryOpExpr>(
                                     indexPattern.value().initialValue_->clone(), Subtract,
                                     make_unique<BinaryOpExpr>(
                                         boundValue.value()->clone(), Add,
                                         make_unique<LiteralExpr>(indexPattern.value().step_ + 1)));
            if (abs(indexPattern.value().step_) == 1) {
                preciseLoopCount = maxLoopCount.value()->clone();
            } else {
                preciseLoopCount = UnknownExpr::makeUnknown().into_underlying();
            }
        } else if (auto refExpr = dyn_cast<DeclRefExpr>(cond->IgnoreParenImpCasts())) {
            if (loopEntry.getPaths().empty()) {
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

            if (auto it = loopEntry.getPaths()[0]->getVarAddr().find(varDecl);
                it != loopEntry.getPaths()[0]->getVarAddr().end()) {
                if (auto pattern = hasPattern(*it->second); pattern == nullopt) {
                    INFO("Index has no parseable pattern.");
                    return false;
                } else {
                    indexPattern = std::move(pattern.value());

                    for (auto &prePath : preState.getPaths()) {
                        if (prePath->isActive()) {
                            if (prePath->getMemoryState().contains(*it->second))
                                isLocal = false;
                            else
                                isLocal = true;
                            break;
                        }
                    }

                    if (isLocal == nullopt)
                        UNREACHABLE();
                }
                indexAddr = it->second->addressClone();
            } else {
                ERROR("A varDecl* has no Address mapped, something must goes wrong.");
            }

            auto [_, values] =
                loopEntryInfo.symbolicLoopEntry_->getPaths().at(0)->evalExpr(refExpr);
            if (values.size() != 1)
                UNREACHABLE();
            indexValue = std::move(values.at(0));

            opCode     = BO_NE;
            boundValue = make_unique<LiteralExpr>((int64_t)0);

            using enum BinaryOpExpr::Operator;
            if (indexPattern == nullopt || boundValue == nullopt)
                UNREACHABLE();

            // abs(n - i + step - 1)
            maxLoopCount = indexPattern.value().step_ > 0
                               ? make_unique<BinaryOpExpr>(
                                     make_unique<BinaryOpExpr>(
                                         boundValue.value()->clone(), Add,
                                         make_unique<LiteralExpr>(indexPattern.value().step_ - 1)),
                                     Subtract, indexPattern.value().initialValue_->clone())
                               : make_unique<BinaryOpExpr>(
                                     indexPattern.value().initialValue_->clone(), Subtract,
                                     make_unique<BinaryOpExpr>(
                                         boundValue.value()->clone(), Add,
                                         make_unique<LiteralExpr>(indexPattern.value().step_ + 1)));
            if (abs(indexPattern.value().step_) == 1) {
                preciseLoopCount = maxLoopCount.value()->clone();
            } else {
                preciseLoopCount = UnknownExpr::makeUnknown().into_underlying();
            }
        } else {
            INFO("Loop's condition expr is too complex.");
            return false;
        }

        // 	Check and assign in bulk
        if (indexAddr == nullopt || indexValue == nullopt || opCode == nullopt ||
            boundValue == nullopt || preciseLoopCount == nullopt || maxLoopCount == nullopt ||
            indexPattern == nullopt || isLocal == nullopt)
            UNREACHABLE();
        loopInfo.indexInfo_ =
            LoopInfo::IndexInfo{.indexAddr_          = std::move(indexAddr.value()),
                                .indexSymbolicValue_ = std::move(indexValue.value()),
                                .op_                 = std::move(opCode.value()),
                                .indexBound_         = std::move(boundValue.value()),
                                .preciseLoopCount_   = std::move(preciseLoopCount.value()),
                                .maxLoopCount_       = std::move(maxLoopCount.value()),
                                .indexPattern_       = std::move(indexPattern.value()),
                                .isLocal_            = std::move(isLocal.value())};
        if (loopInfo.indexInfo_.value().preciseLoopCount_->isUnknown()) {
            loopInfo.isIncompleteLoop_ = true;
            return false;
        }
        return true;
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(SetIndexPlugin, "setIndex");