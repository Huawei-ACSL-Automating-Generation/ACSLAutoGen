/**
 * @file loopInvariantPlugins.cpp
 * @brief Implements path-sensitive and path-insensitive loop invariant plugins.
 */
#include <cstddef>
#include <iterator>
#include <llvm/Support/Casting.h>
#include <memory>
#include <unordered_set>

#include "state.h"
#include "loopInvTemplates.h"
#include "stringTemplate.h"
#include "utils.h"
#include <clang/AST/RecursiveASTVisitor.h>
#include "Symbolic/expr.h"
#include "macros.h"
#include "specGenerator.h"
#include "Symbolic/aggregateExpr.h"

namespace acslg::spec_generator {
    namespace symb = acslg::analyzer::symbolic;

    namespace {
        // Detect whether a statement subtree touches array or pointer typed expressions.
        bool stmtHasArrayOrPointer(const clang::Stmt *stmt) {
            if (!stmt)
                return false;
            struct ArrayOrPointerVisitor : clang::RecursiveASTVisitor<ArrayOrPointerVisitor> {
                bool found = false;

                bool TraverseStmt(clang::Stmt *S) {
                    if (found || !S)
                        return true;
                    return clang::RecursiveASTVisitor<ArrayOrPointerVisitor>::TraverseStmt(S);
                }

                bool VisitExpr(clang::Expr *E) {
                    if (!E)
                        return true;
                    auto qt = E->getType();
                    if (!qt.isNull() && (qt->isPointerType() || qt->isArrayType()))
                        found = true;
                    if (llvm::isa<clang::ArraySubscriptExpr>(E))
                        found = true;
                    if (auto *UO = llvm::dyn_cast<clang::UnaryOperator>(E)) {
                        if (UO->getOpcode() == clang::UO_Deref ||
                            UO->getOpcode() == clang::UO_AddrOf)
                            found = true;
                    }
                    return true;
                }
            };

            ArrayOrPointerVisitor visitor;
            visitor.TraverseStmt(const_cast<clang::Stmt *>(stmt));
            return visitor.found;
        }

        bool loopHasArrayOrPointer(const LoopInfo &loopInfo) {
            return stmtHasArrayOrPointer(loopInfo.bodyStmt) ||
                   stmtHasArrayOrPointer(loopInfo.condExpr) ||
                   stmtHasArrayOrPointer(loopInfo.incStmt) ||
                   stmtHasArrayOrPointer(loopInfo.initStmt);
        }
    } // namespace

    /**
     * @class CheckAndDumpLoopInfoPlugin
     * @brief Debug plugin that prints parsed loop info without emitting ACSL clauses.
     */
    class CheckAndDumpLoopInfoPlugin : public PathInsensitiveLoopInvPlugin {
      public:
        CheckAndDumpLoopInfoPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        /**
         * @brief Log collected loop metadata for inspection.
         */
        GenResultType generate(const analyzer::ProgramState &,
                               const analyzer::ProgramState &,
                               const LoopInfo &loopInfo) const override {
            if (loopInfo.entryAndCurrentInfo) {
                auto &entryAndCurrentInfo = loopInfo.entryAndCurrentInfo.value();
                if (entryAndCurrentInfo.symbolicLoopEntry->getPaths().size() != 1) {
                    ERROR("`symbolicLoopEntry` is in an invalid state");
                }
                // Dump the symbolic entry state to help diagnose plugin ordering and data flow.
                INFO("`loopEntryInfo_` is std::set.");
                INFO(entryAndCurrentInfo.symbolicLoopEntry->dump());
            } else {
                INFO("`loopEntryInfo_` isn't std::set.");
            }

            if (loopInfo.indexInfo) {
                auto &indexInfo = loopInfo.indexInfo.value();
                INFO("`loopEntryInfo_` is std::set.");
                INFO("`indexRealAddr_`: " + indexInfo.indexRealAddr->dump());
                INFO("`indexSymbolicValue_`: " + indexInfo.indexSymbolicValue->dump());
                std::string opStr;
                switch (indexInfo.op) {
#define BINARY_OPERATION(Name, Spelling)                                                           \
    case clang::BO_##Name: opStr = #Spelling; break;
#include <clang/AST/OperationKinds.def>
                    default: UNREACHABLE();
                }
                INFO("`op_`: " + opStr);
                INFO("`indexBound_`: " + indexInfo.indexBound->dump());
                INFO("`preciseLoopCount_`: " + indexInfo.preciseLoopCount->dump());
                INFO("`maxLoopCount_`: " + indexInfo.maxLoopCount->dump());
                INFO("`indexPattern_`: " + indexInfo.indexPattern.dump());
            } else {
                INFO("indexInfo_ isn't std::set.");
            }

            if (loopInfo.patternInfo) {
                auto &patternInfo = loopInfo.patternInfo.value();
                INFO("patternInfo_ is std::set.");
                for (auto &[addr, pattern] : patternInfo.normalExitPatternsMap) {
                    INFO("address: " + addr.get().dump());
                    if (pattern)
                        INFO("pattern: " + pattern.value().dump());
                    else
                        INFO("pattern: std::nullopt(too complex)");
                }
            } else {
                INFO("patternInfo_ isn't std::set.");
            }

            return GenResultType{
                .acsl = std::nullopt, .acslUsedPoints = {}, .globalNormalPathPostInfo = {}};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(CheckAndDumpLoopInfoPlugin, "checkAndDumpLoopInfo");

    /**
     * @class LinearInvariantPlugin
     * @brief Generates linear loop invariants based on index patterns and loop entry state.
     */
    class LinearInvariantPlugin : public PathSensitiveLoopInvPlugin {
      public:
        LinearInvariantPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        size_t propose() const override { return 0; }
        /**
         * @brief Attempt to emit ACSL invariants assuming linear index progression.
         */
        std::optional<GenResultType> tryGenerate(const analyzer::ProgramState &,
                                                 const analyzer::ProgramState &,
                                                 const LoopInfo &loopInfo) const override {
            if (loopInfo.entryAndCurrentInfo == std::nullopt || loopInfo.indexInfo == std::nullopt)
                ERROR("Dependencies are not met.");

            auto &entryAndCurrentInfo = loopInfo.entryAndCurrentInfo.value();
            auto &indexInfo           = loopInfo.indexInfo.value();

            if (entryAndCurrentInfo.symbolicLoopEntry->getPaths().size() != 1) {
                ERROR("symbolicLoopEntry has something wrong, check the SetLoopEntryPlugin?");
            }

            std::unique_ptr<symb::SymbolicExpr> loopCond;
            auto lhs = indexInfo.indexSymbolicValue->clone();
            auto rhs = indexInfo.indexBound->clone();
            switch (indexInfo.op) {
                using enum clang::BinaryOperatorKind;
                using enum symb::BinaryOpExpr::Operator;
                case BO_LT: {
                    // Normalize strict inequalities to non-strict to simplify invariant printing.
                    auto newRHS = std::make_unique<symb::BinaryOpExpr>(
                        rhs->clone(), Subtract, std::make_unique<symb::LiteralExpr>(1));
                    loopCond = std::make_unique<symb::BinaryOpExpr>(lhs->clone(), LessEqual,
                                                                    std::move(newRHS));
                    break;
                }
                case BO_GT: {
                    auto newRHS = std::make_unique<symb::BinaryOpExpr>(
                        rhs->clone(), Add, std::make_unique<symb::LiteralExpr>(1));
                    loopCond = std::make_unique<symb::BinaryOpExpr>(lhs->clone(), GreaterEqual,
                                                                    std::move(newRHS));
                    break;
                }
                case BO_LE:
                    loopCond = std::make_unique<symb::BinaryOpExpr>(lhs->clone(), LessEqual,
                                                                    std::move(rhs));
                    break;
                case BO_GE:
                    loopCond = std::make_unique<symb::BinaryOpExpr>(lhs->clone(), GreaterEqual,
                                                                    std::move(rhs));
                    break;
                case BO_NE: {
                    if (indexInfo.indexPattern.step < 0) {
                        auto rhsPlus1 = std::make_unique<symb::BinaryOpExpr>(
                            rhs->clone(), Add, std::make_unique<symb::LiteralExpr>(1));
                        auto geExpr = std::make_unique<symb::BinaryOpExpr>(
                            lhs->clone(), GreaterEqual, std::move(rhsPlus1));
                        loopCond = std::move(geExpr);
                    } else if (indexInfo.indexPattern.step > 0) {
                        auto rhsMinus1 = std::make_unique<symb::BinaryOpExpr>(
                            rhs->clone(), Subtract, std::make_unique<symb::LiteralExpr>(1));
                        auto leExpr = std::make_unique<symb::BinaryOpExpr>(lhs->clone(), LessEqual,
                                                                           std::move(rhsMinus1));
                        loopCond    = std::move(leExpr);
                    } else {
                        UNREACHABLE();
                    }
                    break;
                }
                default:
                    ERROR("Unexpected operator, check `setIndexPlugin` may solve this problem.");
            }

            auto symbolEntry = entryAndCurrentInfo.symbolicLoopEntry->getPaths().front()->clone();
            if (loopInfo.sharedMemoryMap) {
                for (const auto &[addr, val] : *loopInfo.sharedMemoryMap) {
                    if (!llvm::isa<symb::VariableAddress>(addr.get()))
                        continue;
                    auto constVal = val->tryEvalAsConstant();
                    if (constVal == std::nullopt)
                        continue;
                    symbolEntry->getMutMemoryState().write(
                        addr, std::make_unique<symb::LiteralExpr>(constVal.value()));
                }
            }
            auto loopCurrent = entryAndCurrentInfo.symbolicLoopCurrent->clone();

            if (loopCurrent->getPaths().size() + entryAndCurrentInfo.inactivePaths.size() >
                (2 << 5)) {
                // time/memory limit exceeded
                return {};
            }

            bool generateBranches =
                !loopHasArrayOrPointer(loopInfo); // collapse branches for array/pointer loops

            auto [spec, normalPostInfos, interruptPostInfos] =
                analyzer::buildLoopInvariant(std::move(loopCond), *symbolEntry, *loopCurrent,
                                             entryAndCurrentInfo.inactivePaths, generateBranches);

            std::vector<PostPSInfo> normalPostPSInfos;
            for (auto &postInfo : normalPostInfos)
                normalPostPSInfos.emplace_back(std::move(postInfo.first),
                                               std::move(postInfo.second),
                                               analyzer::Path::PathState::Step, std::nullopt);

            std::vector<std::vector<PostPSInfo>> interruptPathsPostPSInfos;
            assert(loopInfo.entryAndCurrentInfo->inactivePaths.size() == interruptPostInfos.size());
            for (size_t i = 0, inactivePathNum = loopInfo.entryAndCurrentInfo->inactivePaths.size();
                 i < inactivePathNum; ++i) {
                auto &interruptPath = loopInfo.entryAndCurrentInfo->inactivePaths.at(i);
                auto &postInfos     = interruptPostInfos.at(i);
                std::vector<PostPSInfo> infos;
                for (auto &postInfo : postInfos) {
                    if (interruptPath->getPathState() == analyzer::Path::PathState::Return)
                        infos.emplace_back(std::move(postInfo.first), std::move(postInfo.second),
                                           analyzer::Path::PathState::Return,
                                           symb::UnknownExpr::makeUnknown().into_underlying());
                    else
                        infos.emplace_back(std::move(postInfo.first), std::move(postInfo.second),
                                           interruptPath->getPathState(), std::nullopt);
                }
                interruptPathsPostPSInfos.push_back(std::move(infos));
            }
            if (spec == std::nullopt)
                return GenResultType{.acsl                = std::nullopt,
                                     .acslUsedPoints      = {},
                                     .normalPathPostInfos = std::move(normalPostPSInfos),
                                     .interruptPathsPostInfos =
                                         std::move(interruptPathsPostPSInfos)};
            return GenResultType{.acsl                    = std::move(spec),
                                 .acslUsedPoints          = {},
                                 .normalPathPostInfos     = std::move(normalPostPSInfos),
                                 .interruptPathsPostInfos = std::move(interruptPathsPostPSInfos)};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(LinearInvariantPlugin, "StInGXPlugin");

    class LinearInvariantPluginForComplexLoop : public PathSensitiveLoopInvPlugin {
      public:
        LinearInvariantPluginForComplexLoop(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        size_t propose() const override { return 0; }
        std::optional<GenResultType> tryGenerate(const analyzer::ProgramState &,
                                                 const analyzer::ProgramState &,
                                                 const LoopInfo &loopInfo) const override {
            if (loopInfo.entryAndCurrentInfo == std::nullopt)
                ERROR("Dependencies are not met.");

            auto &entryAndCurrentInfo = loopInfo.entryAndCurrentInfo.value();

            if (entryAndCurrentInfo.symbolicLoopEntry->getPaths().size() != 1) {
                ERROR("symbolicLoopEntry has something wrong, check the SetLoopEntryPlugin?");
            }
            auto symbolEntry = entryAndCurrentInfo.symbolicLoopEntry->getPaths().front()->clone();
            if (loopInfo.sharedMemoryMap) {
                for (const auto &[addr, val] : *loopInfo.sharedMemoryMap) {
                    if (!llvm::isa<symb::VariableAddress>(addr.get()))
                        continue;
                    auto constVal = val->tryEvalAsConstant();
                    if (constVal == std::nullopt)
                        continue;
                    symbolEntry->getMutMemoryState().write(
                        addr, std::make_unique<symb::LiteralExpr>(constVal.value()));
                }
            }

            auto [_, evalExprs] = symbolEntry->evalExpr(loopInfo.condExpr);
            if (evalExprs.size() != 1)
                ERROR("Branching is not allowed here.");
            auto &loopCond = evalExprs.front();

            auto loopCurrent = entryAndCurrentInfo.symbolicLoopCurrent->clone();

            if (loopCurrent->getPaths().size() + entryAndCurrentInfo.inactivePaths.size() >
                (2 << 5)) {
                // time/memory limit exceeded
                return {};
            }

            auto [spec, normalPostInfos, interruptPostInfos] =
                analyzer::buildLoopInvariant(std::move(loopCond).into_underlying(), *symbolEntry,
                                             *loopCurrent, entryAndCurrentInfo.inactivePaths);

            std::vector<PostPSInfo> normalPostPSInfos;
            for (auto &postInfo : normalPostInfos)
                normalPostPSInfos.emplace_back(std::move(postInfo.first),
                                               std::move(postInfo.second),
                                               analyzer::Path::PathState::Step, std::nullopt);

            std::vector<std::vector<PostPSInfo>> interruptPathsPostPSInfos;
            assert(loopInfo.entryAndCurrentInfo->inactivePaths.size() == interruptPostInfos.size());
            for (size_t i = 0, inactivePathNum = loopInfo.entryAndCurrentInfo->inactivePaths.size();
                 i < inactivePathNum; ++i) {
                auto &interruptPath = loopInfo.entryAndCurrentInfo->inactivePaths.at(i);
                auto &postInfos     = interruptPostInfos.at(i);
                std::vector<PostPSInfo> infos;
                for (auto &postInfo : postInfos) {
                    if (interruptPath->getPathState() == analyzer::Path::PathState::Return)
                        infos.emplace_back(std::move(postInfo.first), std::move(postInfo.second),
                                           analyzer::Path::PathState::Return,
                                           symb::UnknownExpr::makeUnknown().into_underlying());
                    else
                        infos.emplace_back(std::move(postInfo.first), std::move(postInfo.second),
                                           interruptPath->getPathState(), std::nullopt);
                }
                interruptPathsPostPSInfos.push_back(std::move(infos));
            }
            if (spec == std::nullopt)
                return GenResultType{.acsl                = std::nullopt,
                                     .acslUsedPoints      = {},
                                     .normalPathPostInfos = std::move(normalPostPSInfos),
                                     .interruptPathsPostInfos =
                                         std::move(interruptPathsPostPSInfos)};
            return GenResultType{.acsl                    = std::move(spec),
                                 .acslUsedPoints          = {},
                                 .normalPathPostInfos     = std::move(normalPostPSInfos),
                                 .interruptPathsPostInfos = std::move(interruptPathsPostPSInfos)};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(LinearInvariantPluginForComplexLoop, "StInGXPluginForComplexLoop");

    // todo: deal with complex range
    class LoopAssignsPlugin : public PathInsensitiveLoopInvPlugin {
      public:
        LoopAssignsPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        GenResultType generate(const analyzer::ProgramState &preState,
                               const analyzer::ProgramState &loopEntry,
                               const LoopInfo &loopInfo) const override {
            if (loopInfo.entryAndCurrentInfo == std::nullopt ||
                loopInfo.indexInfo == std::nullopt || loopInfo.patternInfo == std::nullopt)
                ERROR("Dependencies are not met.");

            auto &entryAndCurrentInfo = loopInfo.entryAndCurrentInfo.value();
            auto &indexInfo           = loopInfo.indexInfo.value();
            auto &patternInfo         = loopInfo.patternInfo.value();

            if (entryAndCurrentInfo.symbolicLoopEntry->getPaths().size() != 1) {
                ERROR("symbolicLoopEntry has something wrong, check the SetLoopEntryPlugin?");
            }

            auto &entryMS =
                entryAndCurrentInfo.symbolicLoopEntry->getPaths().at(0)->getMemoryState();

            std::vector<symb::AddressBox> assignedAddrs;
            PostPIInfo normalPostInfo;

            auto &memoryMap = normalPostInfo.memoryMap;
            auto &pathConds = normalPostInfo.pathConds;

            auto isLocal = [&](const symb::Address &addr) {
                auto root = addr.getFromRoot();
                if (root == std::nullopt)
                    TODO();
                auto &varAddrMap = preState.getPaths().at(0)->getVarAddr();
                if (!varAddrMap.contains(root.value()))
                    return true;
                return false;
            }; // isLocal end

            auto tryGetAsRange =
                [&](const symb::Address &addr) -> std::optional<symb::SymbolAddress> {
                auto symbolAddr = llvm::dyn_cast<const symb::SymbolAddress>(&addr);
                if (symbolAddr == nullptr)
                    return std::nullopt;

                auto from = symbolAddr->getFromAddr();
                // If the base address itself is x-step, then there is no need to check the
                // offset (or to check it for reliability).
                if (from == std::nullopt)
                    ERROR("Invalid state");
                if (auto it = patternInfo.normalExitPatternsMap.find(*from.value());
                    it != patternInfo.normalExitPatternsMap.end()) {
                    auto &pattern = it->second;
                    if (pattern == std::nullopt)
                        TODO();
                    auto result = *symbolAddr;
                    result.setOffset(
                        std::make_unique<symb::LiteralExpr>(symb::SymbolAddress::ZERO_OFFSET));
                    if (!indexInfo.preciseLoopCount->isUnknown())
                        result.setLength(indexInfo.preciseLoopCount->simplifiedExpr());
                    else
                        result.setLength(indexInfo.maxLoopCount->simplifiedExpr());
                    return result;
                }

                auto offset = symbolAddr->getOffset();
                // Is offset x-step?
                if (auto symbolValue = llvm::dyn_cast<const symb::SymbolValue>(offset.get())) {
                    auto symbolValueFrom = symbolValue->getFromAddr();
                    if (symbolValueFrom == std::nullopt)
                        TODO();
                    if (auto it = patternInfo.normalExitPatternsMap.find(*symbolValueFrom.value());
                        it != patternInfo.normalExitPatternsMap.end()) {
                        auto &pattern = it->second;
                        if (pattern == std::nullopt)
                            TODO();
                        auto result = *symbolAddr;
                        result.setOffset(pattern.value().initialValue->clone());
                        if (!indexInfo.preciseLoopCount->isUnknown())
                            result.setLength(indexInfo.preciseLoopCount->simplifiedExpr());
                        else
                            result.setLength(indexInfo.maxLoopCount->simplifiedExpr());
                        return result;
                    } else {
                        TODO();
                    }
                }
                return std::nullopt;
            }; // tryGetAsRange end

            std::unordered_map<size_t, utils::not_null<std::unique_ptr<symb::SymbolicExpr>>>
                condsForInsert;
            for (auto &[addr, pattern] : patternInfo.normalExitPatternsMap) {
                using enum symb::BinaryOpExpr::Operator;
                if (isLocal(addr))
                    continue;
                if (auto range = tryGetAsRange(addr)) {
                    if (pattern) {
                        auto [_, ok] = memoryMap.emplace(
                            range.value(), symb::UnknownExpr::makeUnknown().into_underlying());
                        // todo
                        // std::make_unique<BinaryOpExpr>(pattern.value().initialValue_->clone(),
                        // Add,
                        //                           std::make_unique<LiteralExpr>(pattern.value().step_)));

                        // Deal with loops like
                        // {
                        //     p[0] = ...;
                        //     p[1] = ...;
                        //     p[2] = ...;
                        //     p += 3;
                        // }
                        // In which case `tryGetAsRange` will return same **range** for all three
                        // expressions.
                        // This unsound method currently exists solely to handle this special case.
                        if (!ok && !indexInfo.preciseLoopCount->isUnknown())
                            UNREACHABLE();
                    } else {
                        auto [_, ok] = memoryMap.emplace(
                            range.value(), symb::UnknownExpr::makeUnknown().into_underlying());

                        // Deal with loops like
                        // {
                        //     p[0] = ...;
                        //     p[1] = ...;
                        //     p[2] = ...;
                        //     p += 3;
                        // }
                        // In which case `tryGetAsRange` will return same **range** for all three
                        // expressions.
                        // This unsound method currently exists solely to handle this special case.
                        if (!ok && !indexInfo.preciseLoopCount->isUnknown())
                            UNREACHABLE();
                    }
                    assignedAddrs.emplace_back(std::move(range.value()));
                } else {
                    // Use lambda to eliminate nested if
                    [&]() {
                        if (!pattern)
                            return;
                        if (!indexInfo.preciseLoopCount->isUnknown()) {
                            // Loop count is precise (index's step is 1 or -1)

                            // init + step * loopCount
                            auto [_, ok] = memoryMap.emplace(
                                addr,
                                std::make_unique<symb::BinaryOpExpr>(
                                    pattern.value().initialValue->clone(), Add,
                                    std::make_unique<symb::BinaryOpExpr>(
                                        std::make_unique<symb::LiteralExpr>(pattern.value().step),
                                        Multiply, indexInfo.preciseLoopCount->clone())));
                            if (!ok)
                                UNREACHABLE();
                        } else {
                            // index's step is not +-1.

                            // Just check.
                            if (std::abs(pattern.value().step) !=
                                std::abs(indexInfo.indexPattern.step))
                                return;

                            auto pointAfterLoop = symb::SourcePoint::fromStmtAfter(
                                loopInfo.bodyStmt,
                                entryAndCurrentInfo.symbolicLoopEntry->getContext()
                                    .getSourceManager(),
                                entryAndCurrentInfo.symbolicLoopEntry->getContext()
                                    .getLangOptions());

                            auto indexValueAfterLoop =
                                getSymbol(indexInfo.indexExpr->getType(),
                                          indexInfo.indexRealAddr->addressClone().into_underlying(),
                                          std::move(pointAfterLoop));

                            using enum symb::BinaryOpExpr::Operator;

                            // i >= n (step > 0) or
                            // i <= 0 (step < 0)
                            auto firstIndexCond =
                                (indexInfo.indexPattern.step > 0
                                     ? std::make_unique<symb::BinaryOpExpr>(
                                           indexValueAfterLoop->clone(), GreaterEqual,
                                           indexInfo.indexBound->clone())
                                     : std::make_unique<symb::BinaryOpExpr>(
                                           indexValueAfterLoop->clone(), LessEqual,
                                           indexInfo.indexBound->clone()));

                            // i < n + step (step > 0) or
                            // i > 0 + step (step < 0)
                            auto secondIndexCond =
                                (indexInfo.indexPattern.step > 0
                                     ? std::make_unique<symb::BinaryOpExpr>(
                                           indexValueAfterLoop->clone(), LessThan,
                                           std::make_unique<symb::BinaryOpExpr>(
                                               indexInfo.indexBound->clone(), Add,
                                               std::make_unique<symb::LiteralExpr>(
                                                   indexInfo.indexPattern.step)))
                                     : std::make_unique<symb::BinaryOpExpr>(
                                           indexValueAfterLoop->clone(), GreaterThan,
                                           std::make_unique<symb::BinaryOpExpr>(
                                               indexInfo.indexBound->clone(), Add,
                                               std::make_unique<symb::LiteralExpr>(
                                                   indexInfo.indexPattern.step))));

                            condsForInsert.emplace(firstIndexCond->hash(), firstIndexCond->clone());
                            condsForInsert.emplace(secondIndexCond->hash(),
                                                   secondIndexCond->clone());

                            // abs(i_post - i_init)
                            auto diff = (indexInfo.indexPattern.step > 0
                                             ? std::make_unique<symb::BinaryOpExpr>(
                                                   indexValueAfterLoop->clone(), Subtract,
                                                   indexInfo.indexSymbolicValue->clone())
                                             : std::make_unique<symb::BinaryOpExpr>(
                                                   indexInfo.indexSymbolicValue->clone(), Subtract,
                                                   indexValueAfterLoop->clone()));

                            auto postValue = (pattern.value().step > 0
                                                  ? std::make_unique<symb::BinaryOpExpr>(
                                                        pattern.value().initialValue->clone(), Add,
                                                        std::move(diff))
                                                  : std::make_unique<symb::BinaryOpExpr>(
                                                        pattern.value().initialValue->clone(),
                                                        Subtract, std::move(diff)));

                            memoryMap.emplace(addr, std::move(postValue));
                        }
                    }();

                    // If all branches fails, fall into here
                    memoryMap.emplace(addr, symb::UnknownExpr::makeUnknown().into_underlying());
                    assignedAddrs.push_back(addr);
                }
            }

            for (auto &[_, cond] : condsForInsert) {
                pathConds.push_back(std::move(cond));
            }

            std::string specs;
            std::unordered_set<symb::SourcePoint> allUsedPoints;
            auto loopEntryPoint = entryAndCurrentInfo.symbolicLoopEntry->getStartPoint();
            std::unordered_set<size_t> insertedACSL{};
            for (auto &addr : assignedAddrs) {
                for (auto &path : loopEntry.getPaths()) {
                    auto concreteAddrExpr = addr.get().getSubstitutedExpr(*path, loopEntryPoint);
                    auto concreteAddr =
                        llvm::dyn_cast<const symb::Address>(concreteAddrExpr.get().get());
                    if (concreteAddr == nullptr)
                        UNREACHABLE();

                    // todo: it's wrong.
                    auto acslExpected =
                        concreteAddr->getACSLOfValue({.noStateLabelFunctionAt = true});
                    if (!acslExpected &&
                        acslExpected.error() == symb::SymbolicExpr::GetACSLError::UnknownExpr)
                        acslExpected = addr.get().getACSLOfValue(
                            {.predefinedLabels = {{loopEntryPoint, "LoopEntry"}}}, loopEntryPoint);
                    if (!acslExpected) {
                        WARN("Value of {" + concreteAddr->dump() + "} getACSL failed.");
                        continue;
                    }
                    auto &[spec, usedPoints] = acslExpected.value();
                    auto specHash            = utils::hash_val(spec);
                    if (insertedACSL.contains(specHash))
                        continue;
                    insertedACSL.insert(specHash);
                    specs += spec + ", ";
                    allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                         std::make_move_iterator(usedPoints.end()));
                }
            }

            std::vector<PostPIInfo> interruptPostInfos{entryAndCurrentInfo.inactivePaths.size()};

            assert(interruptPostInfos.size() == patternInfo.interruptedPathPatternsMaps.size());
            for (size_t i = 0, infoNum = interruptPostInfos.size(); i < infoNum; ++i) {
                auto &postMemoryMap = interruptPostInfos.at(i).memoryMap;

                for (auto &assignedAddr : assignedAddrs) {
                    postMemoryMap.emplace(assignedAddr,
                                          symb::UnknownExpr::makeUnknown().into_underlying());
                }
                for (auto &[addr, _] : patternInfo.interruptedPathPatternsMaps.at(i)) {
                    if (auto range = tryGetAsRange(addr)) {
                        postMemoryMap.emplace(std::move(range.value()),
                                              symb::UnknownExpr::makeUnknown().into_underlying());
                    } else {
                        postMemoryMap.emplace(addr,
                                              symb::UnknownExpr::makeUnknown().into_underlying());
                    }
                }
            }

            if (specs.empty())
                return GenResultType{.acsl                         = R"(loop assigns \nothing;)",
                                     .acslUsedPoints               = {},
                                     .globalNormalPathPostInfo     = {},
                                     .globalInterruptPathsPostInfo = std::move(interruptPostInfos)};
            return GenResultType{.acsl =
                                     "loop assigns " + specs.substr(0, specs.length() - 2) + ";",
                                 .acslUsedPoints               = std::move(allUsedPoints),
                                 .globalNormalPathPostInfo     = std::move(normalPostInfo),
                                 .globalInterruptPathsPostInfo = std::move(interruptPostInfos)};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(LoopAssignsPlugin, "loopAssigns");

    class ComplexLoopAssignsPlugin : public PathInsensitiveLoopInvPlugin {
      public:
        ComplexLoopAssignsPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        GenResultType generate(const analyzer::ProgramState &preState,
                               const analyzer::ProgramState &loopEntry,
                               const LoopInfo &loopInfo) const override {
            if (loopInfo.entryAndCurrentInfo == std::nullopt)
                ERROR("Dependencies are not met.");

            auto &entryAndCurrentInfo = loopInfo.entryAndCurrentInfo.value();

            if (entryAndCurrentInfo.symbolicLoopEntry->getPaths().size() != 1) {
                ERROR("symbolicLoopEntry has something wrong, check the SetLoopEntryPlugin?");
            }

            auto loopEntryPoint = entryAndCurrentInfo.symbolicLoopEntry->getStartPoint();

            auto isLocal = [&](const symb::Address &addr) {
                auto root = addr.getFromRoot();
                if (root == std::nullopt)
                    TODO();
                auto &varAddrMap = preState.getPaths().at(0)->getVarAddr();
                if (!varAddrMap.contains(root.value()))
                    return true;
                return false;
            };

            std::unordered_set<symb::AddressBox, symb::AddressBoxHash, symb::AddressBoxEq>
                assignedAddrs;
            auto collectAssignedFromPath =
                [&](const analyzer::Path &path,
                    std::unordered_set<symb::AddressBox, symb::AddressBoxHash, symb::AddressBoxEq>
                        &setToUpdate) {
                    for (auto &&[addr, value] : path.getMemoryState().flat()) {
                        (void)value;
                        if (isLocal(addr.get()))
                            continue;
                        if (path.is_point_to_structure(addr))
                            continue;
                        if (path.isUnchanged(
                                addr, *entryAndCurrentInfo.symbolicLoopEntry->getPaths().front()))
                            continue;
                        setToUpdate.insert(addr);
                    }
                };

            for (auto &path : entryAndCurrentInfo.symbolicLoopCurrent->getPaths())
                collectAssignedFromPath(*path, assignedAddrs);

            PostPIInfo normalPostInfo;
            for (auto &addr : assignedAddrs) {
                normalPostInfo.memoryMap.emplace(
                    addr, symb::UnknownExpr::makeUnknown().into_underlying());
            }

            std::string specs;
            std::unordered_set<symb::SourcePoint> allUsedPoints;
            std::unordered_set<size_t> insertedACSL{};
            for (auto &addr : assignedAddrs) {
                for (auto &path : loopEntry.getPaths()) {
                    auto concreteAddrExpr = addr.get().getSubstitutedExpr(*path, loopEntryPoint);
                    auto concreteAddr =
                        llvm::dyn_cast<const symb::Address>(concreteAddrExpr.get().get());
                    if (concreteAddr == nullptr)
                        UNREACHABLE();

                    // todo: it's wrong
                    auto acslExpected =
                        concreteAddr->getACSLOfValue({.noStateLabelFunctionAt = true});
                    if (!acslExpected &&
                        acslExpected.error() == symb::SymbolicExpr::GetACSLError::UnknownExpr)
                        acslExpected = addr.get().getACSLOfValue(
                            {.predefinedLabels = {{loopEntryPoint, "LoopEntry"}}}, loopEntryPoint);
                    if (!acslExpected) {
                        WARN("Value of {" + concreteAddr->dump() + "} getACSL failed.");
                        continue;
                    }
                    auto &[spec, usedPoints] = acslExpected.value();
                    auto specHash            = utils::hash_val(spec);
                    if (insertedACSL.contains(specHash))
                        continue;
                    insertedACSL.insert(specHash);
                    specs += spec + ", ";
                    allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                         std::make_move_iterator(usedPoints.end()));
                }
            }

            std::vector<PostPIInfo> interruptePostInfos{entryAndCurrentInfo.inactivePaths.size()};
            for (size_t i = 0, infoNum = interruptePostInfos.size(); i < infoNum; ++i) {
                auto &postInfo              = interruptePostInfos.at(i);
                auto &path                  = entryAndCurrentInfo.inactivePaths.at(i);
                auto interruptAssignedAddrs = assignedAddrs;
                collectAssignedFromPath(*path, interruptAssignedAddrs);

                for (auto &addr : interruptAssignedAddrs) {
                    postInfo.memoryMap.emplace(addr,
                                               symb::UnknownExpr::makeUnknown().into_underlying());
                }
            }

            if (specs.empty())
                return GenResultType{.acsl                     = R"(loop assigns \nothing;)",
                                     .acslUsedPoints           = {},
                                     .globalNormalPathPostInfo = std::move(normalPostInfo),
                                     .globalInterruptPathsPostInfo =
                                         std::move(interruptePostInfos)};
            return GenResultType{.acsl =
                                     "loop assigns " + specs.substr(0, specs.length() - 2) + ";",
                                 .acslUsedPoints               = std::move(allUsedPoints),
                                 .globalNormalPathPostInfo     = std::move(normalPostInfo),
                                 .globalInterruptPathsPostInfo = std::move(interruptePostInfos)};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(ComplexLoopAssignsPlugin, "complexLoopAssigns");

    class ParadigmMaxMinPlugin : public PathInsensitiveLoopInvPlugin {
      public:
        ParadigmMaxMinPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        GenResultType generate(const analyzer::ProgramState &,
                               const analyzer::ProgramState &,
                               const LoopInfo &loopInfo) const override {
            if (loopInfo.entryAndCurrentInfo == std::nullopt ||
                loopInfo.indexInfo == std::nullopt || loopInfo.patternInfo == std::nullopt)
                ERROR("Dependencies are not met.");

            auto &entryAndCurrentInfo = loopInfo.entryAndCurrentInfo.value();
            auto &indexInfo           = loopInfo.indexInfo.value();
            auto &patternInfo         = loopInfo.patternInfo.value();

            if (entryAndCurrentInfo.symbolicLoopEntry->getPaths().size() != 1) {
                ERROR("symbolicLoopEntry has something wrong, check the SetLoopEntryPlugin?");
            }

            // Only work when loop is 1-step.
            int64_t indexStep;
            if (auto it = patternInfo.normalExitPatternsMap.find(*indexInfo.indexSymbolicAddr);
                it != patternInfo.normalExitPatternsMap.end()) {
                if (it->second == std::nullopt)
                    ERROR("PatternsMap_ is in an invalid state");
                if ((*it->second).step != 1 && (*it->second).step != -1)
                    return GenResultType{
                        .acsl = std::nullopt, .acslUsedPoints = {}, .globalNormalPathPostInfo = {}};
                else
                    indexStep = (*it->second).step;
            } else {
                ERROR("PatternsMap_ is in an invalid state");
            }

            std::string spec;
            PostPIInfo normalPostInfo;

            // Hook which deals with every if.
            auto ifVisitor = utils::StmtVisitor{[&](const clang::IfStmt *s) {
                if (s == nullptr)
                    return;

                std::optional<StringTemplate> specTemplate{std::nullopt};
                std::optional<symb::MaxMinOverRange::Extremum> extremum{};
                // Parameters for template filling, see StringTemplate for more information.
                std::optional<std::string> param_n{std::nullopt}, param_array{std::nullopt},
                    param_index{std::nullopt}, param_m{std::nullopt};
                std::unique_ptr<symb::SymbolAddress> arrayAddr{nullptr};
                // @SgtPepper114 2025/10/26: I'm in the middle of rewriting `regularForm` as
                // `getACSL`. This plugin is a beast and barely anyone uses it, so I'm not gonna
                // worry about the correct way to do it for now. If it breaks, just band-aid it by
                // fixing all the config and return value stuff of `getACSL`.

                if (auto acslExpected =
                        indexInfo.indexRealAddr->getACSLOfValue({.noStateLabelFunctionAt = true})) {
                    param_index = acslExpected.value().first;
                } else {
                    return;
                }
                if (auto acslExpected =
                        indexInfo.indexBound->getACSL({.noStateLabelFunctionAt = true})) {
                    param_n = acslExpected.value().first;
                } else {
                    return;
                }

                auto getAddress = [&](const clang::Expr *expr)
                    -> std::optional<utils::not_null<std::unique_ptr<symb::Address>>> {
                    if (expr == nullptr)
                        return std::nullopt;
                    try {
                        // May pass some strange expr to extractAddress.
                        return entryAndCurrentInfo.symbolicLoopEntry->getPaths()
                            .at(0)
                            ->extractLValue(expr);
                    } catch (...) { return std::nullopt; }
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
                        if (idxAddr == std::nullopt || *idxAddr.value() != *indexInfo.indexRealAddr)
                            return false;

                        if (auto addr = getAddress(arraySub->getBase()))
                            if (auto acslExpected = addr.value()->getACSLOfValue(
                                    {.noStateLabelFunctionAt = true})) {
                                param_array = acslExpected.value().first;
                                if (auto *symbolAddr =
                                        llvm::dyn_cast<symb::Address>(addr.value().get().get()))
                                    arrayAddr = std::make_unique<symb::SymbolAddress>(
                                        arraySub->getType(),
                                        symbolAddr->addressClone().into_underlying(),
                                        entryAndCurrentInfo.symbolicLoopEntry->getStartPoint());
                                return true;
                            }
                        return false;
                    } else if (auto unary = dyn_cast_if_present<clang::UnaryOperator>(
                                   expr->IgnoreParenImpCasts());
                               unary && unary->getOpcode() == clang::UnaryOperatorKind::UO_Deref) {
                        if (auto bin = dyn_cast_if_present<clang::BinaryOperator>(
                                unary->getSubExpr()->IgnoreParenImpCasts());
                            bin && bin->getOpcode() == clang::BinaryOperatorKind::BO_Add) {
                            // *(p+i)

                            // Is 'i' loop's index?
                            if (auto rhsAddr = getAddress(bin->getRHS());
                                rhsAddr == std::nullopt ||
                                *rhsAddr.value() != *indexInfo.indexRealAddr)
                                return false;
                            if (auto addr = getAddress(bin->getLHS()))
                                if (auto acslExpected = addr.value()->getACSLOfValue(
                                        {.noStateLabelFunctionAt = true})) {
                                    param_array = acslExpected.value().first;
                                    if (auto *symbolAddr =
                                            llvm::dyn_cast<symb::Address>(addr.value().get().get()))
                                        arrayAddr = std::make_unique<symb::SymbolAddress>(
                                            unary->getType(),
                                            symbolAddr->addressClone().into_underlying(),
                                            entryAndCurrentInfo.symbolicLoopEntry->getStartPoint());
                                    return true;
                                }
                            return false;
                        } else if (auto declRef = dyn_cast_if_present<clang::DeclRefExpr>(
                                       unary->getSubExpr()->IgnoreParenImpCasts())) {
                            // *it

                            auto addr = getAddress(declRef);
                            if (addr == std::nullopt)
                                return false;
                            // Does this variable step same as loop?
                            if (auto it = patternInfo.normalExitPatternsMap.find(*addr.value());
                                it == patternInfo.normalExitPatternsMap.end() ||
                                it->second == std::nullopt || (*it->second).step != indexStep)
                                return false;
                            if (auto acslExpected = addr.value()->getACSLOfValue(
                                    {.noStateLabelFunctionAt = true})) {
                                param_array = acslExpected.value().first;
                                if (auto *symbolAddr =
                                        llvm::dyn_cast<symb::Address>(addr.value().get().get()))
                                    arrayAddr = std::make_unique<symb::SymbolAddress>(
                                        declRef->getType(),
                                        symbolAddr->addressClone().into_underlying(),
                                        entryAndCurrentInfo.symbolicLoopEntry->getStartPoint());
                                return true;
                            }
                            return false;
                        }
                    } else {
                        return false;
                    }
                    UNREACHABLE();
                }; // parseIndexedArray end

                auto isLocal = [&](const clang::VarDecl *varDecl) {
                    if (!entryAndCurrentInfo.symbolicLoopEntry->getPaths()
                             .at(0)
                             ->getVarAddr()
                             .contains(varDecl))
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
                            if (llvm::isa<symb::SymbolValue>(*indexInfo.indexBound))
                                specTemplate = maxOnLeft ? FIND_MAX_LOOP_WITH_VAR_BOUND
                                                         : FIND_MIN_LOOP_WITH_VAR_BOUND;
                            else
                                specTemplate = maxOnLeft ? FIND_MAX_LOOP_WITH_OTHER_BOUND
                                                         : FIND_MIN_LOOP_WITH_OTHER_BOUND;
                            extremum = maxOnLeft ? symb::MaxMinOverRange::Extremum::Max
                                                 : symb::MaxMinOverRange::Extremum::Min;
                            break;
                        case BO_GE:
                        case BO_GT:
                            if (llvm::isa<symb::SymbolValue>(*indexInfo.indexBound))
                                specTemplate = maxOnLeft ? FIND_MIN_LOOP_WITH_VAR_BOUND
                                                         : FIND_MAX_LOOP_WITH_VAR_BOUND;
                            else
                                specTemplate = maxOnLeft ? FIND_MIN_LOOP_WITH_OTHER_BOUND
                                                         : FIND_MAX_LOOP_WITH_OTHER_BOUND;
                            extremum = maxOnLeft ? symb::MaxMinOverRange::Extremum::Min
                                                 : symb::MaxMinOverRange::Extremum::Max;
                            break;
                        default: return;
                    }
                    DEBUG("Operator matched.");

                    // Verify 'max'.
                    if (auto var = dyn_cast<clang::VarDecl>(maxExpr->getDecl());
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
                } else {
                    return; // if condition doesn't match the expected binary pattern
                }

                if (specTemplate == std::nullopt || param_n == std::nullopt ||
                    param_array == std::nullopt || param_index == std::nullopt ||
                    param_m == std::nullopt) {
                    UNREACHABLE();
                }

                // Verify 'then' of if.
                if (auto thenStmt = s->getThen()) {
                    auto symbolState = entryAndCurrentInfo.symbolicLoopEntry->clone();
                    symbolState->step(thenStmt);
                    for (auto &path : symbolState->getPaths()) {
                        std::unique_ptr<symb::SymbolValue> maxVar{nullptr};
                        if (auto maxValue = path->getVarState(maxDecl);
                            llvm::isa<symb::SymbolValue>(*maxValue)) {
                            maxVar = std::unique_ptr<symb::SymbolValue>(
                                llvm::dyn_cast<symb::SymbolValue>(
                                    std::move(maxValue).into_underlying().release()));
                        } else {
                            return;
                        }

                        auto evalResult = path->evalExpr(elementExpr);
                        if (evalResult.second.size() != 1)
                            ERROR("Branch isn't permitted here.");
                        auto &elementValue = evalResult.second.front();
                        if (*maxVar != *elementValue)
                            return;
                    }
                } else {
                    return;
                }
                DEBUG("Then Verified.");

                // Verify 'else' of if.
                if (auto elseStmt = s->getElse()) {
                    auto symbolState = entryAndCurrentInfo.symbolicLoopEntry->clone();
                    symbolState->step(elseStmt);
                    for (auto &path : symbolState->getPaths()) {
                        if (auto varAddrIt = path->getVarAddr().find(maxDecl);
                            varAddrIt != path->getVarAddr().end()) {
                            auto &maxAddr = varAddrIt->second;
                            if (!path->isUnchanged(
                                    *maxAddr,
                                    *entryAndCurrentInfo.symbolicLoopEntry->getPaths().front()))
                                return;
                        } else {
                            ERROR("Can't find maxDecl after step, something must be wrong.");
                        }
                    }
                }
                DEBUG("Else Verified.");

                if (!arrayAddr || extremum == std::nullopt)
                    return;

                // Pretty sure we have found a 'find_max' loop.
                spec += (*specTemplate)
                            .to_string(NameMap{{"n", *param_n},
                                               {"array", *param_array},
                                               {"index", *param_index},
                                               {"m", *param_m}}) +
                        "\n";

                using enum symb::BinaryOpExpr::Operator;
                auto arrayRange = std::make_unique<symb::SymbolAddress>(*arrayAddr);
                if (indexStep > 0) {
                    arrayRange->resetOffset(); // just zero offset, :)
                    arrayRange->setLength(indexInfo.indexBound->clone());
                    // arrayRange->setOffset(indexInfo.indexSymbolicValue->clone());
                    // arrayRange->setLength(std::make_unique<symb::BinaryOpExpr>(
                    //     indexInfo.indexBound->clone(), Subtract,
                    //     indexInfo.indexSymbolicValue->clone()));
                } else {
                    arrayRange->resetOffset(); // just zero offset, :)
                    // arrayRange->setOffset(indexInfo.indexBound->clone());
                    arrayRange->setLength(std::make_unique<symb::BinaryOpExpr>(
                        indexInfo.indexSymbolicValue->clone(), Subtract,
                        indexInfo.indexBound->clone()));
                }

                auto &entryPath = entryAndCurrentInfo.symbolicLoopEntry->getPaths().at(0);
                auto maxAddrIt  = entryPath->getVarAddr().find(maxDecl);
                if (maxAddrIt == entryPath->getVarAddr().end())
                    return;

                auto pointAfterLoop = symb::SourcePoint::fromStmtAfter(
                    loopInfo.bodyStmt,
                    entryAndCurrentInfo.symbolicLoopEntry->getContext().getSourceManager(),
                    entryAndCurrentInfo.symbolicLoopEntry->getContext().getLangOptions());
                symb::AddressBox maxAddrBox{*maxAddrIt->second};
                normalPostInfo.memoryMap.emplace(
                    maxAddrBox, std::make_unique<symb::MaxMinOverRange>(std::move(arrayRange), "k",
                                                                        *extremum, pointAfterLoop));
            }}; // ifVisitor end
            ifVisitor.runOn(loopInfo.bodyStmt);

            if (spec.empty())
                return GenResultType{.acsl                         = std::nullopt,
                                     .acslUsedPoints               = {},
                                     .globalNormalPathPostInfo     = normalPostInfo,
                                     .globalInterruptPathsPostInfo = {}};

            spec.pop_back(); // earse \n
            return GenResultType{.acsl                         = spec,
                                 .acslUsedPoints               = {},
                                 .globalNormalPathPostInfo     = std::move(normalPostInfo),
                                 .globalInterruptPathsPostInfo = {}};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(ParadigmMaxMinPlugin, "paradigmMaxMin");

    class LoopVariantPlugin : public PathInsensitiveLoopInvPlugin {
      public:
        LoopVariantPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        GenResultType generate(const analyzer::ProgramState &,
                               const analyzer::ProgramState &,
                               const LoopInfo &loopInfo) const override {
            if (loopInfo.indexInfo == std::nullopt)
                ERROR("Dependencies are not met.");

            auto &indexInfo = loopInfo.indexInfo.value();

            // Yes, the expression of the loop variant is maxLoopCount. :)
            auto acslExpected =
                indexInfo.maxLoopCount->simplifiedExpr()->getACSL({.noStateLabelFunctionAt = true});
            if (!acslExpected) {
                WARN("Variant {" + indexInfo.maxLoopCount->simplifiedExpr()->dump() +
                     "} getACSL failed.");
                return GenResultType{.acsl                         = std::nullopt,
                                     .acslUsedPoints               = {},
                                     .globalNormalPathPostInfo     = {},
                                     .globalInterruptPathsPostInfo = {}};
            }
            auto spec = "loop variant " + acslExpected.value().first + ";";
            return GenResultType{.acsl                         = std::move(spec),
                                 .acslUsedPoints               = {},
                                 .globalNormalPathPostInfo     = {},
                                 .globalInterruptPathsPostInfo = {}};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(LoopVariantPlugin, "loopVariant");

    class ParadigmSearchPlugin : public PathSensitiveLoopInvPlugin {
      public:
        ParadigmSearchPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        size_t propose() const override { return 100; }
        std::optional<GenResultType> tryGenerate(const analyzer::ProgramState &,
                                                 const analyzer::ProgramState &loopEntry,
                                                 const LoopInfo &loopInfo) const override {
            if (loopInfo.entryAndCurrentInfo == std::nullopt ||
                loopInfo.indexInfo == std::nullopt || loopInfo.patternInfo == std::nullopt)
                ERROR("Dependencies are not met.");

            auto &entryAndCurrentInfo = loopInfo.entryAndCurrentInfo.value();
            auto &indexInfo           = loopInfo.indexInfo.value();
            auto &patternInfo         = loopInfo.patternInfo.value();

            if (entryAndCurrentInfo.symbolicLoopEntry->getPaths().size() != 1) {
                ERROR("symbolicLoopEntry has something wrong, check the SetLoopEntryPlugin?");
            }

            // This plugin expects only two paths: one where the search succeeds and breaks/returns,
            // and the other where no match is found and the loop terminates when the condition is
            // no longer met.
            if (entryAndCurrentInfo.inactivePaths.size() != 1)
                return std::nullopt;

            auto &interruptedPath = entryAndCurrentInfo.inactivePaths.front();

            // Only work when loop is 1-step.
            int64_t indexStep;
            if (auto it = patternInfo.normalExitPatternsMap.find(*indexInfo.indexSymbolicAddr);
                it != patternInfo.normalExitPatternsMap.end()) {
                if (it->second == std::nullopt)
                    ERROR("Index Should have pattern.");
                if ((*it->second).step != 1 && (*it->second).step != -1)
                    return {};
                else
                    indexStep = (*it->second).step;
            } else {
                ERROR("PatternsMap_ is in an invalid state");
            }

            // todo: may deal with multiple conditions.
            const auto &interruptedConds = interruptedPath->getPathConditions();
            if (interruptedConds.size() != 1)
                return {};

            const auto &interruptedCond = *interruptedConds.begin();

            auto pointAfterLoop = symb::SourcePoint::fromStmtAfter(
                loopInfo.bodyStmt,
                entryAndCurrentInfo.symbolicLoopEntry->getContext().getSourceManager(),
                entryAndCurrentInfo.symbolicLoopEntry->getContext().getLangOptions());

            auto indexValueAfterLoop =
                getSymbol(indexInfo.indexExpr->getType(),
                          indexInfo.indexRealAddr->addressClone().into_underlying(),
                          std::move(pointAfterLoop));

            auto getSubExpr = [&](const symb::Symbol &symbol)
                -> std::optional<utils::not_null<std::unique_ptr<symb::SymbolicExpr>>> {
                auto fromAddr = symbol.getFromAddr();
                if (fromAddr == std::nullopt)
                    return std::nullopt;
                auto it = patternInfo.normalExitPatternsMap.find(*fromAddr.value());
                // The value on this address doesn't change during loop, so just copy it.
                if (it == patternInfo.normalExitPatternsMap.end())
                    return symbol.toSymbolicExpr()->clone();
                if (it->second == std::nullopt)
                    return std::nullopt;
                auto &[initValue, step] = it->second.value();
                using enum symb::BinaryOpExpr::Operator;
                // x_init + x_step * (index - index_init)
                if (indexStep > 0)
                    return std::make_unique<symb::BinaryOpExpr>(
                        initValue->clone(), Add,
                        std::make_unique<symb::BinaryOpExpr>(
                            std::make_unique<symb::LiteralExpr>(step), Multiply,
                            std::make_unique<symb::BinaryOpExpr>(
                                std::make_unique<symb::SymbolAddress::RangeIndex>("k"), Subtract,
                                indexInfo.indexSymbolicValue->clone())));
                // x_init + x_step * (index_init - index)
                return std::make_unique<symb::BinaryOpExpr>(
                    initValue->clone(), Add,
                    std::make_unique<symb::BinaryOpExpr>(
                        std::make_unique<symb::LiteralExpr>(step), Multiply,
                        std::make_unique<symb::BinaryOpExpr>(
                            indexInfo.indexSymbolicValue->clone(), Subtract,
                            std::make_unique<symb::SymbolAddress::RangeIndex>("k"))));
            }; // getSubExpr ends

            auto sameValueOnRealEntries = [&](const symb::SymbolicExpr &expr)
                -> std::optional<utils::not_null<std::unique_ptr<symb::SymbolicExpr>>> {
                std::unique_ptr<symb::SymbolicExpr> commonValue{nullptr};
                for (auto &entry : loopEntry.getPaths()) {
                    auto subedExpr = expr.getSubstitutedExpr(
                        *entry, loopInfo.entryAndCurrentInfo->loopEntryPoint);
                    if (commonValue == nullptr)
                        commonValue = std::move(subedExpr).into_underlying();
                    else if (*commonValue != *subedExpr)
                        return std::nullopt;
                }
                return commonValue;
            }; // sameValueOnRealEntries ends

            symb::SymbolicExpr::HashExprMap hashExprMapForSub{};
            std::unique_ptr<symb::SymbolAddress> arrayInCond;
            for (auto &[hash, symbol] : interruptedCond->collectUsedSymbols()) {
                auto fromAddr = symbol->getFromAddr();
                if (fromAddr == std::nullopt)
                    return {};
                if (auto fromSymbolAddr =
                        llvm::dyn_cast<const symb::SymbolAddress>(fromAddr.value().get().get())) {
                    if (arrayInCond == nullptr)
                        arrayInCond = std::make_unique<symb::SymbolAddress>(*fromSymbolAddr);
                    auto offset = fromSymbolAddr->getOffset();
                    for (auto &[hashInOff, symbolInOff] : offset->collectUsedSymbols()) {
                        auto subedExpr = getSubExpr(*symbolInOff);
                        if (subedExpr == std::nullopt)
                            return std::nullopt;
                        hashExprMapForSub.insert_or_assign(hashInOff, std::move(subedExpr.value()));
                    }
                    continue;
                }
                auto subedExpr = getSubExpr(*symbol);
                if (subedExpr == std::nullopt)
                    return std::nullopt;
                hashExprMapForSub.insert_or_assign(hash, std::move(subedExpr.value()));
            }
            if (arrayInCond == nullptr)
                return {};

            auto pred = interruptedCond->getSubstitutedValueExpr(hashExprMapForSub);

            std::vector<PostPSInfo> normalPostInfos{1};
            std::vector<std::vector<PostPSInfo>> interruptedPathsInfos{std::vector<PostPSInfo>{1}};
            auto &normalPathInfo          = normalPostInfos.at(0);
            auto &interruptedPathInfo     = interruptedPathsInfos.at(0).at(0);
            interruptedPathInfo.pathState = interruptedPath->getPathState();
            if (interruptedPath->getPathState() == analyzer::Path::PathState::Return) {
                if (interruptedPath->getReturnExpr() == std::nullopt)
                    ERROR("This path has path state 'return' but no return expr.");
                if (!interruptedPath->getReturnExpr().value()->collectUsedSymbols().empty()) {
                    interruptedPathInfo.returnExpr =
                        symb::UnknownExpr::makeUnknown().into_underlying();
                    // todo
                } else {
                    interruptedPathInfo.returnExpr =
                        interruptedPath->getReturnExpr().value()->clone();
                }
            }
            using enum symb::QuantifierOverRange::Quantifier;
            using enum symb::BinaryOpExpr::Operator;

            assert(arrayInCond != nullptr);
            auto arrayRange = std::make_unique<symb::SymbolAddress>(*arrayInCond);
            if (indexStep > 0) {
                arrayRange->setOffset(indexInfo.indexSymbolicValue->clone());
                arrayRange->setLength(
                    std::make_unique<symb::BinaryOpExpr>(indexInfo.indexBound->clone(), Subtract,
                                                         indexInfo.indexSymbolicValue->clone()));
            } else {
                arrayRange->setOffset(indexInfo.indexBound->clone());
                arrayRange->setLength(
                    std::make_unique<symb::BinaryOpExpr>(indexInfo.indexSymbolicValue->clone(),
                                                         Subtract, indexInfo.indexBound->clone()));
            }
            normalPathInfo.pathState = analyzer::Path::PathState::Step;
            normalPathInfo.pathConds.push_back(std::make_unique<symb::QuantifierOverRange>(
                std::make_unique<symb::SymbolAddress>(*arrayRange), "k", ForAll,
                std::make_unique<symb::UnaryOpExpr>(symb::UnaryOpExpr::Operator::LogicalNot,
                                                    pred->clone())));

            interruptedPathInfo.pathConds.push_back(std::make_unique<symb::QuantifierOverRange>(
                std::move(arrayRange), "k", Exist, pred->clone().into_underlying()));

            auto expected =
                std::make_unique<symb::UnaryOpExpr>(symb::UnaryOpExpr::Operator::LogicalNot,
                                                    pred->clone())
                    ->getACSL({.predefinedLabels{
                        {entryAndCurrentInfo.symbolicLoopEntry->getStartPoint(), "LoopEntry"}}});
            if (expected) {
                auto resACSL =
                    StringTemplate{"loop invariant \\forall integer k; ${leftBound} <= k "
                                   "< ${rightBound} ==> ${pred};"};
                std::unordered_set<symb::SourcePoint> usedPoints;
                usedPoints = std::move(expected.value().second);
                std::string leftBoundStr, rightBoundStr;
                if (indexStep > 0) {
                    auto leftBound = sameValueOnRealEntries(*indexInfo.indexPattern.initialValue);
                    if (leftBound == std::nullopt)
                        leftBound = indexInfo.indexPattern.initialValue->clone();
                    auto leftExpected =
                        leftBound.value()->getACSL({}, entryAndCurrentInfo.loopEntryPoint);
                    assert(leftBound);
                    auto rightExpected =
                        indexInfo.indexSymbolicValue->getACSL({.noStateLabelFunctionAt = true});
                    assert(rightExpected);
                    leftBoundStr = leftExpected.value().first;
                    usedPoints.insert(std::make_move_iterator(leftExpected.value().second.begin()),
                                      std::make_move_iterator(leftExpected.value().second.end()));
                    rightBoundStr = rightExpected.value().first;
                    usedPoints.insert(std::make_move_iterator(rightExpected.value().second.begin()),
                                      std::make_move_iterator(rightExpected.value().second.end()));
                } else {
                    auto leftExpected =
                        indexInfo.indexSymbolicValue->getACSL({.noStateLabelFunctionAt = true});
                    assert(leftExpected);
                    auto rightBound = sameValueOnRealEntries(*indexInfo.indexPattern.initialValue);
                    if (rightBound == std::nullopt)
                        rightBound = indexInfo.indexPattern.initialValue->clone();
                    auto rightExpected =
                        rightBound.value()->getACSL({}, entryAndCurrentInfo.loopEntryPoint);
                    assert(rightBound);
                    leftBoundStr = leftExpected.value().first;
                    usedPoints.insert(std::make_move_iterator(leftExpected.value().second.begin()),
                                      std::make_move_iterator(leftExpected.value().second.end()));
                    rightBoundStr = rightExpected.value().first;
                    usedPoints.insert(std::make_move_iterator(rightExpected.value().second.begin()),
                                      std::make_move_iterator(rightExpected.value().second.end()));
                }

                return GenResultType{.acsl = resACSL.to_string({{"leftBound", leftBoundStr},
                                                                {"rightBound", rightBoundStr},
                                                                {"pred", expected.value().first}}),
                                     .acslUsedPoints          = std::move(usedPoints),
                                     .normalPathPostInfos     = std::move(normalPostInfos),
                                     .interruptPathsPostInfos = std::move(interruptedPathsInfos)};
            }

            return GenResultType{.acsl                    = std::nullopt,
                                 .acslUsedPoints          = {},
                                 .normalPathPostInfos     = std::move(normalPostInfos),
                                 .interruptPathsPostInfos = std::move(interruptedPathsInfos)};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(ParadigmSearchPlugin, "paradigmSearch");
} // namespace acslg::spec_generator
