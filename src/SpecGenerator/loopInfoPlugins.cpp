// src/SpecGenerator/looopInfoPlugins.cpp

#include "specGenerator.h"
#include "macros.h"
#include "state.h"

namespace acslg::spec_generator {
    namespace symb = acslg::analyzer::symbolic;

    class SetEntryAndCurrentPlugin : public LoopInfoPlugin {
      public:
        SetEntryAndCurrentPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        bool parse(const analyzer::ProgramState &,
                   const analyzer::ProgramState &loopEntry,
                   LoopInfo &loopInfo) const override {
            auto symbolicState = loopEntry.clone();

            auto loopEntryPoint = symb::SourcePoint::fromStmtBefore(
                loopInfo.loopStmt, symbolicState->getContext().getSourceManager(),
                symbolicState->getContext().getLangOptions());

            symbolicState->resymbolize(std::move(loopEntryPoint));
            auto symbolicLoopEntry = symbolicState->clone();

            symbolicState->step(loopInfo.condExpr);
            symbolicState->step(loopInfo.bodyStmt);
            symbolicState->step(loopInfo.incStmt);

            auto [loopCurrent, inactiveState] = symbolicState->splitActiveInactive();

            loopInfo.entryAndCurrentInfo.emplace(
                LoopInfo::EntryAndCurrentInfo{.symbolicLoopEntry   = std::move(symbolicLoopEntry),
                                              .symbolicLoopCurrent = std::move(loopCurrent),
                                              .inactivePaths = std::move(inactiveState->getPaths()),
                                              .loopEntryPoint = loopEntryPoint});
            return true;
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(SetEntryAndCurrentPlugin, "SetEntryAndCurrent");

    // Preprocess simple patterns of regions.
    class SetPatternsPlugin : public LoopInfoPlugin {
      public:
        SetPatternsPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        bool parse(const analyzer::ProgramState &,
                   const analyzer::ProgramState &loopEntry,
                   LoopInfo &loopInfo) const override {
            if (loopInfo.entryAndCurrentInfo == std::nullopt)
                ERROR("Dependencies are not met.");

            auto &entryAndCurrentInfo = loopInfo.entryAndCurrentInfo.value();

            if (entryAndCurrentInfo.symbolicLoopEntry->getPaths().size() != 1) {
                ERROR("symbolicLoopEntry has something wrong, check the SetLoopEntryPlugin?");
            }

            auto &loopCurrent = entryAndCurrentInfo.symbolicLoopCurrent;

            using Pattern = LoopInfo::Pattern;

            auto getPatternsFromPath = [&](const analyzer::Path &currentEntry) {
                symb::AddressBoxMap<std::optional<const Pattern>> patterns;
                auto &preVA = entryAndCurrentInfo.symbolicLoopEntry->getPaths().at(0)->getVarAddr();
                auto &preMS =
                    entryAndCurrentInfo.symbolicLoopEntry->getPaths().at(0)->getMemoryState();
                for (auto &&[addr, currentExpr] : currentEntry.getMemoryState().flat()) {
                    if (auto rootDecl = addr.get().getFromRoot();
                        rootDecl == std::nullopt || !preVA.contains(rootDecl.value()))
                        continue; // from local variable
                    if (auto preValue = preMS.read(addr)) {
                        if (*preValue.value() == *currentExpr)
                            continue; // unchanged
                    } else {
                        if (currentEntry.isUnchanged(addr))
                            continue; // unchanged
                    }
                    std::optional<utils::not_null<std::unique_ptr<symb::SymbolicExpr>>> entryExpr;
                    if (auto preValue = preMS.read(addr)) {
                        entryExpr = preValue.value()->clone();
                    } else {
                        auto [hashAddrMap, _] =
                            symb::SymbolicExpr::collectUsedSymbols(*currentExpr);
                        if (hashAddrMap.size() != 1) {
                            patterns.emplace(addr, std::nullopt);
                            continue;
                        }
                        if (isFrom(*hashAddrMap.begin()->second->toSymbolicExpr(), addr,
                                   entryAndCurrentInfo.symbolicLoopEntry->getStartPoint())) {
                            entryExpr = hashAddrMap.begin()->second->toSymbolicExpr()->clone();
                        } else {
                            patterns.emplace(addr, std::nullopt);
                            continue;
                        }
                    }

                    if (entryExpr == std::nullopt)
                        UNREACHABLE();
                    auto [_, hashIdMap] =
                        symb::SymbolicExpr::collectUsedSymbols(*currentExpr, *entryExpr.value());
                    if (auto diff = currentExpr->toLinearExpr(hashIdMap) -
                                    entryExpr.value()->toLinearExpr(hashIdMap);
                        diff.all_homogeneous_terms_are_zero()) {
                        auto step = diff.inhomogeneous_term().get_si();
                        patterns.emplace(
                            addr, Pattern{entryExpr.value()->clone().into_underlying(), step});
                    } else {
                        patterns.emplace(addr, std::nullopt);
                    }
                }
                return patterns;
            }; // getPatternsFromPath end

            auto isEqual = [](const std::optional<const Pattern> &LHS,
                              const std::optional<const Pattern> &RHS) {
                if (LHS == std::nullopt && RHS == std::nullopt)
                    return true;
                if (LHS && RHS) {
                    if (*(*LHS).initialValue != *(*RHS).initialValue)
                        UNREACHABLE();
                    if ((*LHS).step == (*RHS).step)
                        return true;
                }
                return false;
            }; // isEqual end

            symb::AddressBoxMap<std::optional<const Pattern>> normalExitPatternsMap;

            for (auto &path : loopCurrent->getPaths()) {
                switch (path->getPathState()) {
                    using enum analyzer::Path::PathState;
                    case Break:
                    case Continue:
                    case Return: UNREACHABLE();
                    case Step: {
                        auto currentPatterns = getPatternsFromPath(*path);
                        if (normalExitPatternsMap.empty())
                            normalExitPatternsMap = std::move(currentPatterns);
                        else {
                            // Is this addr has same pattern on every step-path?
                            for (auto &[addr, pattern] : normalExitPatternsMap) {
                                if (auto it = currentPatterns.find(addr);
                                    it == currentPatterns.end() || !isEqual(it->second, pattern))
                                    normalExitPatternsMap[addr] = std::nullopt;
                            }
                            for (auto &[addr, pattern] : currentPatterns) {
                                if (auto it = normalExitPatternsMap.find(addr);
                                    it == normalExitPatternsMap.end() ||
                                    !isEqual(it->second, pattern))
                                    normalExitPatternsMap[addr] = std::nullopt;
                            }
                        }
                        break;
                    }
                    default: UNREACHABLE();
                }
            }

            std::vector<analyzer::symbolic::AddressBoxMap<std::optional<const Pattern>>>
                interruptedPathPatternsMaps;
            for (auto &path : entryAndCurrentInfo.inactivePaths) {
                interruptedPathPatternsMaps.push_back(getPatternsFromPath(*path));
            }
            assert(interruptedPathPatternsMaps.size() == entryAndCurrentInfo.inactivePaths.size());

            symb::AddressBoxMap<std::optional<const Pattern>> allPatternsMap =
                normalExitPatternsMap;
            for (auto &interruptedPathPatternsMap : interruptedPathPatternsMaps) {
                // Is this addr has same pattern on every path?
                for (auto &[addr, pattern] : allPatternsMap) {
                    if (auto it = interruptedPathPatternsMap.find(addr);
                        it == interruptedPathPatternsMap.end() || !isEqual(it->second, pattern))
                        allPatternsMap[addr] = std::nullopt;
                }
                for (auto &[addr, pattern] : interruptedPathPatternsMap) {
                    if (auto it = allPatternsMap.find(addr);
                        it == allPatternsMap.end() || !isEqual(it->second, pattern))
                        allPatternsMap[addr] = std::nullopt;
                }
            }

            loopInfo.patternInfo.emplace(LoopInfo::PatternInfo{
                .normalExitPatternsMap       = std::move(normalExitPatternsMap),
                .interruptedPathPatternsMaps = std::move(interruptedPathPatternsMaps),
                .allPatternsMap              = std::move(allPatternsMap)});
            // TODO: may do another round to improve rubustness.
            return true;
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(SetPatternsPlugin, "setPatterns");

    class SetIndexPlugin : public LoopInfoPlugin {
      public:
        SetIndexPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        bool parse(const analyzer::ProgramState &preState,
                   const analyzer::ProgramState &loopEntry,
                   LoopInfo &loopInfo) const override {
            if (loopInfo.entryAndCurrentInfo == std::nullopt ||
                loopInfo.patternInfo == std::nullopt)
                ERROR("Dependencies are not met.");

            auto &entryAndCurrentInfo = loopInfo.entryAndCurrentInfo.value();
            auto &patternInfo         = loopInfo.patternInfo.value();

            if (entryAndCurrentInfo.symbolicLoopEntry->getPaths().size() != 1) {
                ERROR("symbolicLoopEntry has something wrong, check the SetLoopEntryPlugin?");
            }
            auto &entryPath = entryAndCurrentInfo.symbolicLoopEntry->getPaths().at(0);

            auto sameAddressBetweenEveryPaths = [&](const clang::Expr *expr)
                -> std::optional<utils::not_null<std::unique_ptr<symb::Address>>> {
                auto lValue = std::optional<utils::not_null<std::unique_ptr<symb::Address>>>{};
                for (auto &path : loopEntry.getPaths()) {
                    if (lValue == std::nullopt) {
                        lValue.emplace(path->extractLValue(expr));
                        continue;
                    }

                    auto nowLValue = path->extractLValue(expr);
                    if (*lValue.value() != *nowLValue) {
                        // lValue and nowLValue are both std::unique_ptr<Address> and not
                        // equal.
                        return std::nullopt;
                    }
                }
                if (lValue == std::nullopt)
                    UNREACHABLE();
                return std::move(lValue);
            }; // sameAddressBetweenEveryPaths end

            auto hasPattern =
                [&](const symb::Address &addr) -> std::optional<const LoopInfo::Pattern> {
                if (auto it = patternInfo.normalExitPatternsMap.find(addr);
                    it != patternInfo.normalExitPatternsMap.end())
                    return it->second;
                return std::nullopt;
            }; // hasPattern end

            auto unchangedAfterOneRound = [&](const clang::Expr *expr) -> bool {
                auto state            = entryAndCurrentInfo.symbolicLoopCurrent->clone();
                auto [_, valueVector] = entryPath->evalExpr(expr);
                if (valueVector.size() != 1)
                    ERROR("Do not support branch at here");
                auto preValue = std::move(valueVector[0]);

                for (auto &path : state->getPaths()) {
                    tie(std::ignore, valueVector) = path->evalExpr(expr);
                    if (valueVector.size() != 1)
                        ERROR("Do not support branch at here");
                    auto currentValue = std::move(valueVector[0]);
                    if (*preValue != *currentValue)
                        return false;
                }
                return true;
            }; // unchangedAfterOneRound end

            // Split a boolean condition expression by built-in logical AND (&&).
            auto splitByAnd = [](const clang::Expr *cond) -> std::vector<const clang::Expr *> {
                std::vector<const clang::Expr *> clauses;

                // recursive lambda via function
                std::function<void(const clang::Expr *)> split = [&](const clang::Expr *e) {
                    if (!e)
                        return;
                    // Normalize: drop parens and implicit casts
                    const clang::Expr *core = e->IgnoreParenImpCasts();

                    // Built-in && is represented by BinaryOperator with opcode BO_LAnd
                    if (const auto *BO = llvm::dyn_cast<clang::BinaryOperator>(core)) {
                        if (BO->getOpcode() == clang::BinaryOperatorKind::BO_LAnd) {
                            split(BO->getLHS());
                            split(BO->getRHS());
                            return;
                        }
                    }

                    // Any other form (including overloaded operator&&) is a single clause
                    clauses.push_back(core);
                };

                split(cond);
                return clauses;
            }; // splitByAnd end

            // Check whether an expression is a "simple index condition".
            auto isSimpleIndexCond = [](const clang::Expr *e) -> bool {
                if (!e)
                    return false;

                const clang::Expr *core = e->IgnoreParenImpCasts();

                auto asDeclRefVar = [](const clang::Expr *x) -> const clang::VarDecl * {
                    if (const auto *dre =
                            llvm::dyn_cast<clang::DeclRefExpr>(x->IgnoreParenImpCasts()))
                        if (llvm::isa<clang::VarDecl>(dre->getDecl()))
                            return llvm::cast<clang::VarDecl>(dre->getDecl());
                    return nullptr;
                };

                // 1) Bare variable: i
                if (asDeclRefVar(core))
                    return true;

                // 2) Deref: *i
                if (const auto *uo = llvm::dyn_cast<clang::UnaryOperator>(core)) {
                    if (uo->getOpcode() == clang::UnaryOperatorKind::UO_Deref) {
                        if (asDeclRefVar(uo->getSubExpr()))
                            return true;
                    }
                }

                // 3) Relational comparison: i < n, n > i, etc.
                if (const auto *bo = llvm::dyn_cast<clang::BinaryOperator>(core)) {
                    auto op = bo->getOpcode();
                    switch (op) {
                        using enum clang::BinaryOperatorKind;
                        case BO_LT:
                        case BO_LE:
                        case BO_GT:
                        case BO_GE:
                        case BO_EQ:
                        case BO_NE: {
                            const clang::Expr *l = bo->getLHS()->IgnoreParenImpCasts();
                            const clang::Expr *r = bo->getRHS()->IgnoreParenImpCasts();
                            if (asDeclRefVar(l) || asDeclRefVar(r))
                                return true;
                            break;
                        }
                        default: break;
                    }
                }

                return false;
            }; // isSimpleIndexCond end

            auto condCNF = splitByAnd(loopInfo.condExpr);
            const clang::Expr *indexCond{};
            for (auto &clause : condCNF) {
                if (isSimpleIndexCond(clause)) {
                    indexCond = clause;
                    break;
                }
            }
            if (indexCond == nullptr)
                return false; // Too complex

            std::vector<const clang::Expr *> extraConds{};
            extraConds.reserve(condCNF.size() - 1);
            for (auto &clause : condCNF) {
                if (clause == indexCond)
                    continue;
                extraConds.push_back(clause);
            }

            std::optional<utils::not_null<const clang::Expr *>> indexExpr;
            std::optional<utils::not_null<std::unique_ptr<symb::Address>>> indexRealAddr;
            std::optional<utils::not_null<std::unique_ptr<symb::Address>>> indexSymbolicAddr;
            std::optional<utils::not_null<std::unique_ptr<symb::SymbolicExpr>>> indexValue;
            std::optional<clang::BinaryOperator::Opcode> opCode;
            std::optional<utils::not_null<std::unique_ptr<symb::SymbolicExpr>>> boundValue;
            std::optional<utils::not_null<std::unique_ptr<symb::SymbolicExpr>>> preciseLoopCount;
            std::optional<utils::not_null<std::unique_ptr<symb::SymbolicExpr>>> maxLoopCount;
            std::optional<LoopInfo::Pattern> indexPattern;
            std::optional<bool> isLocal;

            if (auto binExpr =
                    llvm::dyn_cast<clang::BinaryOperator>(indexCond->IgnoreParenImpCasts())) {
                // Yes, assume it's on the left.
                auto index = binExpr->getLHS()->IgnoreParenImpCasts();
                auto bound = binExpr->getRHS()->IgnoreParenImpCasts();

                indexExpr = index;

                using enum clang::BinaryOperator::Opcode;

                if (auto addr = sameAddressBetweenEveryPaths(index)) {
                    if (auto pattern = hasPattern(**addr); pattern == std::nullopt) {
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

                        if (isLocal == std::nullopt)
                            UNREACHABLE();
                    }
                    indexRealAddr = std::move(*addr);
                } else {
                    INFO("Same expr in different analyzer::Path points to different location!");
                    return false;
                }

                auto [_, values] =
                    entryAndCurrentInfo.symbolicLoopEntry->getPaths().at(0)->evalExpr(index);
                if (values.size() != 1)
                    UNREACHABLE();
                indexValue = std::move(values.at(0));

                indexSymbolicAddr =
                    entryAndCurrentInfo.symbolicLoopEntry->getPaths().at(0)->extractLValue(index);

                if (unchangedAfterOneRound(bound)) {
                    auto evalResult = entryPath->evalExpr(bound);
                    if (evalResult.second.size() != 1)
                        ERROR("This location does not support control flow branches.");
                    boundValue = std::move(evalResult.second.front());
                } else {
                    INFO("Bound expr is changed after one round.");
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

                using enum symb::BinaryOpExpr::Operator;
                if (indexPattern == std::nullopt || boundValue == std::nullopt ||
                    opCode == std::nullopt)
                    UNREACHABLE();

                // abs(n - i + step - 1)
                maxLoopCount = indexPattern.value().step > 0
                                   ? std::make_unique<symb::BinaryOpExpr>(
                                         std::make_unique<symb::BinaryOpExpr>(
                                             boundValue.value()->clone(), Add,
                                             std::make_unique<symb::LiteralExpr>(
                                                 indexPattern.value().step - 1)),
                                         Subtract, indexPattern.value().initialValue->clone())
                                   : std::make_unique<symb::BinaryOpExpr>(
                                         indexPattern.value().initialValue->clone(), Subtract,
                                         std::make_unique<symb::BinaryOpExpr>(
                                             boundValue.value()->clone(), Add,
                                             std::make_unique<symb::LiteralExpr>(
                                                 indexPattern.value().step + 1)));
                if (opCode.value() == BO_LE || opCode.value() == BO_GE)
                    maxLoopCount = std::make_unique<symb::BinaryOpExpr>(
                        std::move(maxLoopCount.value()), Add,
                        std::make_unique<symb::LiteralExpr>(1));

                if (std::abs(indexPattern.value().step) == 1 && extraConds.empty() &&
                    entryAndCurrentInfo.inactivePaths.empty()) {
                    preciseLoopCount = maxLoopCount.value()->clone();
                } else {
                    preciseLoopCount = symb::UnknownExpr::makeUnknown().into_underlying();
                }
            } else if (auto unaryExpr =
                           dyn_cast<clang::UnaryOperator>(indexCond->IgnoreParenImpCasts())) {
                // TODO: more operators
                if (unaryExpr->getOpcode() != clang::UnaryOperatorKind::UO_Deref) {
                    INFO("Loop's condition expr is too complex! Unimplemented unary "
                         "operator.");
                    return false;
                }

                indexExpr = unaryExpr;
                if (auto addr = sameAddressBetweenEveryPaths(unaryExpr)) {
                    if (auto pattern = hasPattern(**addr); pattern == std::nullopt) {
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

                        if (isLocal == std::nullopt)
                            UNREACHABLE();
                    }
                    indexRealAddr = std::move(*addr);
                } else {
                    INFO("Same expr in different analyzer::Path points to different location!");
                    return false;
                }

                auto [_, values] =
                    entryAndCurrentInfo.symbolicLoopEntry->getPaths().at(0)->evalExpr(unaryExpr);
                if (values.size() != 1)
                    UNREACHABLE();
                indexValue = std::move(values.at(0));

                indexSymbolicAddr =
                    entryAndCurrentInfo.symbolicLoopEntry->getPaths().at(0)->extractLValue(
                        unaryExpr);

                opCode     = clang::BinaryOperatorKind::BO_NE;
                boundValue = std::make_unique<symb::LiteralExpr>((int64_t)0);

                using enum symb::BinaryOpExpr::Operator;
                if (indexPattern == std::nullopt || boundValue == std::nullopt)
                    UNREACHABLE();

                // abs(n - i + step - 1)
                maxLoopCount = indexPattern.value().step > 0
                                   ? std::make_unique<symb::BinaryOpExpr>(
                                         std::make_unique<symb::BinaryOpExpr>(
                                             boundValue.value()->clone(), Add,
                                             std::make_unique<symb::LiteralExpr>(
                                                 indexPattern.value().step - 1)),
                                         Subtract, indexPattern.value().initialValue->clone())
                                   : std::make_unique<symb::BinaryOpExpr>(
                                         indexPattern.value().initialValue->clone(), Subtract,
                                         std::make_unique<symb::BinaryOpExpr>(
                                             boundValue.value()->clone(), Add,
                                             std::make_unique<symb::LiteralExpr>(
                                                 indexPattern.value().step + 1)));
                if (std::abs(indexPattern.value().step) == 1 && extraConds.empty() &&
                    entryAndCurrentInfo.inactivePaths.empty()) {
                    preciseLoopCount = maxLoopCount.value()->clone();
                } else {
                    preciseLoopCount = symb::UnknownExpr::makeUnknown().into_underlying();
                }
            } else if (auto refExpr =
                           dyn_cast<clang::DeclRefExpr>(indexCond->IgnoreParenImpCasts())) {
                if (loopEntry.getPaths().empty()) {
                    ERROR("Pre-state has no analyzer::Path, something goes wrong.");
                }

                auto varDecl =
                    dyn_cast<clang::VarDecl>(refExpr->getDecl())
                        ? dyn_cast<clang::VarDecl>(refExpr->getDecl())->getCanonicalDecl()
                        : nullptr;

                if (varDecl == nullptr) {
                    INFO("Parsing loop's index vaibale has failed. Does loop's "
                         "condition expr have a "
                         "variable?");
                    return false;
                }

                indexExpr = refExpr;

                if (auto it = loopEntry.getPaths()[0]->getVarAddr().find(varDecl);
                    it != loopEntry.getPaths()[0]->getVarAddr().end()) {
                    if (auto pattern = hasPattern(*it->second); pattern == std::nullopt) {
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

                        if (isLocal == std::nullopt)
                            UNREACHABLE();
                    }
                    indexRealAddr = it->second->addressClone();
                } else {
                    ERROR("A varDecl* has no Address mapped, something must goes wrong.");
                }

                auto [_, values] =
                    entryAndCurrentInfo.symbolicLoopEntry->getPaths().at(0)->evalExpr(refExpr);
                if (values.size() != 1)
                    UNREACHABLE();
                indexValue = std::move(values.at(0));

                indexSymbolicAddr =
                    entryAndCurrentInfo.symbolicLoopEntry->getPaths().at(0)->extractLValue(refExpr);

                opCode     = clang::BinaryOperatorKind::BO_NE;
                boundValue = std::make_unique<symb::LiteralExpr>((int64_t)0);

                using enum symb::BinaryOpExpr::Operator;
                if (indexPattern == std::nullopt || boundValue == std::nullopt)
                    UNREACHABLE();

                // abs(n - i + step - 1)
                maxLoopCount = indexPattern.value().step > 0
                                   ? std::make_unique<symb::BinaryOpExpr>(
                                         std::make_unique<symb::BinaryOpExpr>(
                                             boundValue.value()->clone(), Add,
                                             std::make_unique<symb::LiteralExpr>(
                                                 indexPattern.value().step - 1)),
                                         Subtract, indexPattern.value().initialValue->clone())
                                   : std::make_unique<symb::BinaryOpExpr>(
                                         indexPattern.value().initialValue->clone(), Subtract,
                                         std::make_unique<symb::BinaryOpExpr>(
                                             boundValue.value()->clone(), Add,
                                             std::make_unique<symb::LiteralExpr>(
                                                 indexPattern.value().step + 1)));
                if (std::abs(indexPattern.value().step) == 1 && extraConds.empty() &&
                    entryAndCurrentInfo.inactivePaths.empty()) {
                    preciseLoopCount = maxLoopCount.value()->clone();
                } else {
                    preciseLoopCount = symb::UnknownExpr::makeUnknown().into_underlying();
                }
            } else {
                INFO("Loop's condition expr is too complex.");
                return false;
            }

            // 	Check and assign in bulk
            if (indexExpr == std::nullopt || indexRealAddr == std::nullopt ||
                indexSymbolicAddr == std::nullopt || indexValue == std::nullopt ||
                opCode == std::nullopt || boundValue == std::nullopt ||
                preciseLoopCount == std::nullopt || maxLoopCount == std::nullopt ||
                indexPattern == std::nullopt || isLocal == std::nullopt)
                UNREACHABLE();
            loopInfo.indexInfo =
                LoopInfo::IndexInfo{.indexExpr          = std::move(indexExpr.value()),
                                    .indexRealAddr      = std::move(indexRealAddr.value()),
                                    .indexSymbolicAddr  = std::move(indexSymbolicAddr.value()),
                                    .indexSymbolicValue = std::move(indexValue.value()),
                                    .op                 = std::move(opCode.value()),
                                    .indexBound         = std::move(boundValue.value()),
                                    .preciseLoopCount   = std::move(preciseLoopCount.value()),
                                    .maxLoopCount       = std::move(maxLoopCount.value()),
                                    .indexPattern       = std::move(indexPattern.value()),
                                    .isLocal            = std::move(isLocal.value())};
            loopInfo.extraCondConjuncts = std::move(extraConds);
            return true;
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(SetIndexPlugin, "setIndex");
} // namespace acslg::spec_generator