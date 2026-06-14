/**
 * @file loopInvariantPlugins.cpp
 * @brief Implements path-sensitive and path-insensitive loop invariant plugins.
 *
 * This file contains plugins for generating ACSL loop clauses (loop invariant / loop assigns /
 * loop variant).
 *
 * - Path-insensitive plugins: their results are treated as globally valid (or globally merged)
 *   across all paths. They return `PathInsensitiveLoopInvPlugin::GenResultType`, which includes:
 *   - `acsl`: the ACSL clause to emit (may be empty)
 *   - `globalNormalPathPostInfo` / `globalInterruptPathsPostInfo`: information used later to
 *     synthesize the post-state
 *
 * - Path-sensitive plugins: they may produce different post-states/conditions for normal exit
 *   paths and interrupt paths (break/return, etc.). They return
 *   `PathSensitiveLoopInvPlugin::GenResultType`.
 *
 * The inputs of these plugins mostly come from `LoopInfo` (populated by plugins in
 * loopInfoPlugins.cpp), for example:
 * - entryAndCurrentInfo: symbolic loop entry/current states and inactivePaths
 * - indexInfo: the detected loop index, bound, step, etc.
 * - patternInfo: linear evolution patterns between iterations (init + step * k)
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
        /**
         * @brief Detect whether a statement/expression subtree involves array/pointer operations.
         *
         * Background: enumerating path branches for loops with pointer/array accesses tends to
         * explode the state space (aliases, address expressions, etc.). Some plugins use this
         * heuristic to decide whether to collapse branches.
         */
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

        /// @brief Coarsely scan init/cond/inc/body to see if the loop touches arrays/pointers.
        bool loopHasArrayOrPointer(const LoopInfo &loopInfo) {
            return stmtHasArrayOrPointer(loopInfo.bodyStmt) ||
                   stmtHasArrayOrPointer(loopInfo.condExpr) ||
                   stmtHasArrayOrPointer(loopInfo.incStmt) ||
                   stmtHasArrayOrPointer(loopInfo.initStmt);
        }

        bool stmtHasNonAffineOps(const clang::Stmt *stmt) {
            if (!stmt)
                return false;
            struct NonAffineVisitor : clang::RecursiveASTVisitor<NonAffineVisitor> {
                bool found = false;

                bool TraverseStmt(clang::Stmt *S) {
                    if (found || !S)
                        return true;
                    return clang::RecursiveASTVisitor<NonAffineVisitor>::TraverseStmt(S);
                }

                bool VisitBinaryOperator(clang::BinaryOperator *BO) {
                    if (!BO)
                        return true;
                    using enum clang::BinaryOperatorKind;
                    switch (BO->getOpcode()) {
                        case BO_Shl:
                        case BO_Shr:
                        case BO_And:
                        case BO_Or:
                        case BO_Xor:
                        case BO_Rem:
                            found = true;
                            break;
                        default:
                            break;
                    }
                    return true;
                }

                bool VisitUnaryOperator(clang::UnaryOperator *UO) {
                    if (!UO)
                        return true;
                    if (UO->getOpcode() == clang::UnaryOperatorKind::UO_Not)
                        found = true;
                    return true;
                }
            };

            NonAffineVisitor visitor;
            visitor.TraverseStmt(const_cast<clang::Stmt *>(stmt));
            return visitor.found;
        }

        bool loopHasNonAffineOps(const LoopInfo &loopInfo) {
            return stmtHasNonAffineOps(loopInfo.bodyStmt) ||
                   stmtHasNonAffineOps(loopInfo.condExpr) ||
                   stmtHasNonAffineOps(loopInfo.incStmt) ||
                   stmtHasNonAffineOps(loopInfo.initStmt);
        }
    } // namespace

    /**
     * @class CheckAndDumpLoopInfoPlugin
     * @brief Debug plugin that prints parsed loop info without emitting ACSL clauses.
     *
     * This plugin is purely diagnostic: it prints the information already collected in LoopInfo,
     * which helps:
     * - Verify that loopInfoPlugins parsed the loop as expected
     * - Diagnose plugin dependencies/order issues (e.g. entryAndCurrentInfo/indexInfo/patternInfo
     *   unexpectedly missing)
     *
     * Note: it does not emit any ACSL clause, and it does not provide meaningful post-state
     * updates.
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
            // 1) entryAndCurrentInfo: symbolic loop entry/current states, plus inactive paths
            // (break/return, etc.).
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

            // 2) indexInfo: index expression/bound/step/etc. (filled by SetIndexPlugin).
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

            // 3) patternInfo: per-address linear evolution patterns (filled by SetPatternsPlugin).
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
     *
     * Goal:
     * - Use `entryAndCurrentInfo` + `indexInfo` to construct a symbolic loop condition `loopCond`
     * - Call `analyzer::buildLoopInvariant` to infer loop invariants from symbolic execution state
     *
     * Rough applicability:
     * - The loop index condition can be reliably recognized (e.g. i < n / i <= n / i != 0)
     * - loopInfoPlugins can provide the index pattern (init + step) and the entry/one-iteration
     *   symbolic states
     *
     * Output:
     * - `acsl`: loop invariants generated by the analyzer layer (may be empty)
     * - Normal/interrupt post infos: used by specGenerator to synthesize the post-state (and may
     *   enable later clause/inline generation)
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

            // The symbolic loop-entry state must have exactly one path; otherwise it means the
            // execution has already branched *before* loop entry, which this plugin (and many
            // other loop plugins) does not support.
            if (entryAndCurrentInfo.symbolicLoopEntry->getPaths().size() != 1) {
                ERROR("symbolicLoopEntry has something wrong, check the SetLoopEntryPlugin?");
            }

            // Build the loop condition `loopCond` from indexInfo.
            //
            // Note: SetIndexPlugin extracts the "index condition" as (lhs op rhs), e.g. i < n.
            // To make inference/printing more stable, we normalize strict inequalities to
            // non-strict ones:
            // - i < n  ==> i <= n-1
            // - i > n  ==> i >= n+1
            // - i != n can also be normalized into a one-sided inequality when step direction is
            //   known (as a coarse "out-of-range" condition)
            std::unique_ptr<symb::SymbolicExpr> loopCond;
            auto lhs = indexInfo.indexSymbolicValue->clone();
            auto rhs = indexInfo.indexBound->clone();
            switch (indexInfo.op) {
                using enum clang::BinaryOperatorKind;
                using enum symb::BinaryOpExpr::Operator;
                case BO_LT: {
                    // Normalize strict inequalities to non-strict to simplify invariant printing.
                    auto newRHS = std::make_unique<symb::BinaryOpExpr>(
                        rhs->clone(), Subtract, std::make_unique<symb::detail::LiteralExprNode>(1));
                    loopCond = std::make_unique<symb::BinaryOpExpr>(lhs->clone(), LessEqual,
                                                                    std::move(newRHS));
                    break;
                }
                case BO_GT: {
                    auto newRHS = std::make_unique<symb::BinaryOpExpr>(
                        rhs->clone(), Add, std::make_unique<symb::detail::LiteralExprNode>(1));
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
                    // Handling "i != bound" depends on the step direction:
                    // - step < 0 (decreasing): i != bound is normalized as i >= bound+1
                    // - step > 0 (increasing): i != bound is normalized as i <= bound-1
                    if (indexInfo.indexPattern.step < 0) {
                        auto rhsPlus1 = std::make_unique<symb::BinaryOpExpr>(
                            rhs->clone(), Add, std::make_unique<symb::detail::LiteralExprNode>(1));
                        auto geExpr = std::make_unique<symb::BinaryOpExpr>(
                            lhs->clone(), GreaterEqual, std::move(rhsPlus1));
                        loopCond = std::move(geExpr);
                    } else if (indexInfo.indexPattern.step > 0) {
                        auto rhsMinus1 = std::make_unique<symb::BinaryOpExpr>(
                            rhs->clone(), Subtract, std::make_unique<symb::detail::LiteralExprNode>(1));
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

            if (loopHasNonAffineOps(loopInfo))
                return std::nullopt;

            // Clone the single loop-entry path for analyzer::buildLoopInvariant.
            // We may also apply a small "constant write-back" from sharedMemoryMap to reduce
            // Unknowns and make expressions easier to simplify.
            auto symbolEntry = entryAndCurrentInfo.symbolicLoopEntry->getPaths().front()->clone();
            if (loopInfo.sharedMemoryMap) {
                // sharedMemoryMap stores values that remain concrete after merging all *real*
                // entry paths. Here we only write back constant-evaluable values on VariableAddress:
                // - avoid injecting complex expressions that may worsen aliasing/structure
                // - avoid unsound overrides on non-variable addresses (e.g. SymbolAddress)
                for (const auto &[addr, val] : *loopInfo.sharedMemoryMap) {
                    if (!symb::isa<symb::VariableAddress>(addr.get()))
                        continue;
                    auto constVal = val->tryEvalAsConstant();
                    if (constVal == std::nullopt)
                        continue;
                    symbolEntry->getMutMemoryState().write(
                        addr, std::make_unique<symb::detail::LiteralExprNode>(constVal.value()));
                }
            }
            auto loopCurrent = entryAndCurrentInfo.symbolicLoopCurrent->clone();

            // Guard: avoid expensive inference when there are too many paths (time/memory bound).
            if (loopCurrent->getPaths().size() + entryAndCurrentInfo.inactivePaths.size() >
                (2 << 5)) {
                // time/memory limit exceeded
                return {};
            }

            // For loops that touch arrays/pointers, disable generateBranches by default:
            // - avoid fine-grained branch enumeration, prefer merged (more conservative) invariants
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

    /**
     * @brief A "complex loop" variant of LinearInvariantPlugin.
     *
     * Key differences from LinearInvariantPlugin:
     * - Does not depend on SetIndexPlugin providing indexInfo (so it does not manually build
     *   loopCond)
     * - Evaluates `loopInfo.condExpr` directly on the symbolic entry path to obtain the symbolic
     *   loop condition
     *
     * Use cases:
     * - condExpr is too complex for SetIndexPlugin to extract a "simple index condition", but the
     *   analyzer can still evaluate it symbolically
     * - The ComplexPathSensitiveLoopInv group enables this plugin by default (see groups.cpp)
     *
     * Limitation:
     * - Requires that `evalExpr` does not branch (evalExprs.size() == 1)
     */
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

            if (loopHasNonAffineOps(loopInfo))
                return std::nullopt;

            if (entryAndCurrentInfo.symbolicLoopEntry->getPaths().size() != 1) {
                ERROR("symbolicLoopEntry has something wrong, check the SetLoopEntryPlugin?");
            }
            auto symbolEntry = entryAndCurrentInfo.symbolicLoopEntry->getPaths().front()->clone();
            if (loopInfo.sharedMemoryMap) {
                // Same as LinearInvariantPlugin: only write back constant-evaluable VariableAddress
                // entries.
                for (const auto &[addr, val] : *loopInfo.sharedMemoryMap) {
                    if (!symb::isa<symb::VariableAddress>(addr.get()))
                        continue;
                    auto constVal = val->tryEvalAsConstant();
                    if (constVal == std::nullopt)
                        continue;
                    symbolEntry->getMutMemoryState().write(
                        addr, std::make_unique<symb::detail::LiteralExprNode>(constVal.value()));
                }
            }

            // Evaluate condExpr directly on the entry path, and require it to be non-branching.
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

    /**
     * @brief Generates `loop assigns ...;` for "regular" loops.
     *
     * Goals:
     * - Use patternInfo to find non-local memory locations that change during the loop
     * - Lift a subset of "linearly moving addresses" into SymbolAddress ranges (for a more compact
     *   assigns clause)
     * - Emit a `loop assigns` clause, and also build a post-state memoryMap (derive post values
     *   when possible; otherwise use Unknown)
     *
     * Dependencies:
     * - entryAndCurrentInfo: for loopEntryPoint and inactivePaths count
     * - indexInfo: for loopCount/index step and deriving range length
     * - patternInfo: to detect init + step patterns, including for interrupted paths
     *
     * Limitations/notes:
     * - Support for complex ranges is still limited (see TODO branches in tryGetAsRange)
     * - Turning symbolic addresses into concrete ACSL fragments is heuristic and may fail
     */
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

            // Decide whether an address is "local":
            // - If its root decl is not in preState's varAddrMap, treat it as local (e.g. declared
            //   inside the loop, or an otherwise invisible temporary)
            // - We only include non-local locations in the loop assigns clause
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
                // If `addr` is not a SymbolAddress (e.g. a plain variable address), we cannot lift
                // it to a range form.
                auto symbolAddr = symb::dyn_cast<const symb::SymbolAddress>(&addr);
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
                    // Case A: the base address itself moves linearly (x-step). Reset the offset to
                    // zero and set length to loopCount to represent a contiguous writable range.
                    auto result = *symbolAddr;
                    result.setOffset(
                        std::make_unique<symb::detail::LiteralExprNode>(symb::SymbolAddress::ZERO_OFFSET));
                    if (!indexInfo.preciseLoopCount->isUnknown())
                        result.setLength(indexInfo.preciseLoopCount->simplifiedExpr());
                    else
                        result.setLength(indexInfo.maxLoopCount->simplifiedExpr());
                    return result;
                }

                auto offset = symbolAddr->getOffset();
                // Is offset x-step?
                if (auto symbolValue = symb::dyn_cast<const symb::SymbolValue>(offset.get())) {
                    auto symbolValueFrom = symbolValue->getFromAddr();
                    if (symbolValueFrom == std::nullopt)
                        TODO();
                    if (auto it = patternInfo.normalExitPatternsMap.find(*symbolValueFrom.value());
                        it != patternInfo.normalExitPatternsMap.end()) {
                        auto &pattern = it->second;
                        if (pattern == std::nullopt)
                            TODO();
                        // Case B: the base is stable but the offset changes linearly (typical for
                        // p[i] where i changes). Use the initial offset and set length = loopCount.
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

            // When the index step is not ±1, some derivations rely on conditions that constrain
            // the post-loop index value to fall within a certain window. We collect those
            // conditions and attach them to globalNormalPathPostInfo.pathConds for later use when
            // interpreting/printing the synthesized post-state.
            std::unordered_map<size_t, utils::not_null<std::unique_ptr<symb::SymbolicExpr>>>
                condsForInsert;
            for (auto &[addr, pattern] : patternInfo.normalExitPatternsMap) {
                using enum symb::BinaryOpExpr::Operator;
                if (isLocal(addr))
                    continue;
                // 1) Try to lift the address to a range form (more compact assigns; use range addr
                // in the post-state as well).
                if (auto range = tryGetAsRange(addr)) {
                    if (pattern) {
                        auto [_, ok] = memoryMap.emplace(
                            range.value(), symb::UnknownExpr::makeUnknown().into_underlying());
                        // todo
                        // std::make_unique<BinaryOpExpr>(pattern.value().initialValue_->clone(),
                        // Add,
                        //                           std::make_unique<detail::LiteralExprNode>(pattern.value().step_)));

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
                        // 2) Non-range address: if a pattern exists, try to derive a post value;
                        // otherwise give up.
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
                                        std::make_unique<symb::detail::LiteralExprNode>(pattern.value().step),
                                        Multiply, indexInfo.preciseLoopCount->clone())));
                            if (!ok)
                                UNREACHABLE();
                        } else {
                            // index's step is not +-1.

                            // Just check.
                            if (std::abs(pattern.value().step) !=
                                std::abs(indexInfo.indexPattern.step))
                                return;

                            // Idea: when step is large and loopCount is imprecise, use the symbolic
                            // post-loop index value (indexValueAfterLoop) to build a post value.
                            // To avoid deriving an out-of-window value, we also insert two guard
                            // conditions:
                            // - step>0: indexValueAfterLoop >= bound AND indexValueAfterLoop < bound + step
                            // - step<0: the analogous constraints
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
                                               std::make_unique<symb::detail::LiteralExprNode>(
                                                   indexInfo.indexPattern.step)))
                                     : std::make_unique<symb::BinaryOpExpr>(
                                           indexValueAfterLoop->clone(), GreaterThan,
                                           std::make_unique<symb::BinaryOpExpr>(
                                               indexInfo.indexBound->clone(), Add,
                                               std::make_unique<symb::detail::LiteralExprNode>(
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

                    // Fallback: if the derivation did not end up installing a post value (or if a
                    // value already exists), default to Unknown.
                    memoryMap.emplace(addr, symb::UnknownExpr::makeUnknown().into_underlying());
                    assignedAddrs.push_back(addr);
                }
            }

            for (auto &[_, cond] : condsForInsert) {
                pathConds.push_back(std::move(cond));
            }

            // Build `loop assigns ...;`:
            // - assignedAddrs are symbolic addresses; we substitute them on each real entry path to
            //   print ACSL.
            // - We de-duplicate produced fragments by hashing the generated text.
            std::string specs;
            std::unordered_set<symb::SourcePoint> allUsedPoints;
            auto loopEntryPoint = entryAndCurrentInfo.symbolicLoopEntry->getStartPoint();
            std::unordered_set<size_t> insertedACSL{};
            for (auto &addr : assignedAddrs) {
                for (auto &path : loopEntry.getPaths()) {
                    auto concreteAddrExpr = addr.get().getSubstitutedExpr(*path, loopEntryPoint);
                    auto concreteAddr =
                        symb::dyn_cast<const symb::Address>(concreteAddrExpr.get().get());
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

            // For interrupt paths (break/return, etc.), conservatively mark "possibly written"
            // locations as Unknown.
            // - assignedAddrs: the write set identified on the normal path
            // - interruptedPathPatternsMaps: additional changing addresses found for each interrupt
            //   path
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

    /**
     * @brief A simplified/fallback `loop assigns` plugin (used in ComplexLoopInfo flows).
     *
     * Approach:
     * - Does not rely on indexInfo/patternInfo; only uses entryAndCurrentInfo
     * - Compares the memoryState of each path after one loop iteration against the entry
     *   snapshot:
     *   - if an address is written and changes, include it in the assigns set
     * - Post-state only records Unknown (conservative)
     *
     * Pros: broad coverage as long as we can get symbolic entry/current states.
     * Cons: conservative; cannot derive ranges or post-value expressions.
     */
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
                    // flat() iterates the memoryState map (addr -> value), including structured and
                    // symbolic addresses.
                    // Filtering rules:
                    // - local: excluded from assigns
                    // - pointers to structures: currently ignored (may need field-sensitive assigns)
                    // - unchanged w.r.t. the entry snapshot: excluded
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
                        symb::dyn_cast<const symb::Address>(concreteAddrExpr.get().get());
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

    /**
     * @brief Recognize max/min-reduction loop idioms and emit stronger invariants.
     *
     * Typical shape (max):
     * @code
     * for (i = 0; i < n; ++i) {
     *   if (m < a[i]) m = a[i];
     * }
     * @endcode
     *
     * What this plugin does:
     * 1) Visit each if-statement in the loop body and try to match a "max/min update" idiom
     * 2) If matched, emit a set of template invariants (see loopInvTemplates.h)
     * 3) Also update the post-state by modeling the post value of m as
     *    `MaxMinOverRange(arrayRange, k, extremum)`
     *
     * Key restrictions:
     * - Only supports index step ±1 (otherwise the range and template bounds are unstable)
     * - Uses symbolic execution to validate then/else:
     *   - then must update m to the current element value
     *   - else must not modify m
     */
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

            // Visit all if statements in the loop body and try to match the idiom.
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
                                        symb::dyn_cast<symb::Address>(addr.value().get().get()))
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
                                            symb::dyn_cast<symb::Address>(addr.value().get().get()))
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
                                        symb::dyn_cast<symb::Address>(addr.value().get().get()))
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
                            if (symb::isa<symb::SymbolValue>(*indexInfo.indexBound))
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
                            if (symb::isa<symb::SymbolValue>(*indexInfo.indexBound))
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
                            symb::isa<symb::SymbolValue>(*maxValue)) {
                            maxVar = std::unique_ptr<symb::SymbolValue>(
                                symb::dyn_cast<symb::SymbolValue>(
                                    std::move(maxValue).into_underlying().release()));
                        } else {
                            return;
                        }

                        auto evalResult = path->evalExpr(elementExpr);
                        if (evalResult.second.size() != 1)
                            ERROR("Branch isn't permitted here.");
                        auto &elementValue = evalResult.second.front();
                        // After executing the then branch, m must equal the current element value
                        // (i.e. m = a[i]).
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
                    // For now we take [0, bound) for max/min over range (reset offset to zero).
                    // More precise modeling (e.g. starting at index_init) is left for future work.
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

                // Safety guard: avoid constructing MaxMinOverRange with an invalid range.
                if (!arrayRange || !arrayRange->getLength()) {
                    WARN("ParadigmMaxMinPlugin: array range missing length, skip post-state.");
                    return;
                }
                DEBUG("ParadigmMaxMinPlugin: range length dump -> " +
                      arrayRange->getLength().value()->dump());

                auto &entryPath = entryAndCurrentInfo.symbolicLoopEntry->getPaths().at(0);
                auto maxAddrIt  = entryPath->getVarAddr().find(maxDecl);
                if (maxAddrIt == entryPath->getVarAddr().end())
                    return;

                auto pointAfterLoop = symb::SourcePoint::fromStmtAfter(
                    loopInfo.bodyStmt,
                    entryAndCurrentInfo.symbolicLoopEntry->getContext().getSourceManager(),
                    entryAndCurrentInfo.symbolicLoopEntry->getContext().getLangOptions());
                DEBUG("ParadigmMaxMinPlugin: pointAfterLoop label -> " + pointAfterLoop.getLabel());
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

            spec.pop_back(); // erase trailing '\n'
            return GenResultType{.acsl                         = spec,
                                 .acslUsedPoints               = {},
                                 .globalNormalPathPostInfo     = std::move(normalPostInfo),
                                 .globalInterruptPathsPostInfo = {}};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(ParadigmMaxMinPlugin, "paradigmMaxMin");

    /**
     * @brief Generates `loop variant ...;`.
     *
     * We reuse `maxLoopCount` computed by SetIndexPlugin in indexInfo:
     * - maxLoopCount is intended to be an upper bound on the remaining distance from the current
     *   index to the exit bound (absolute value)
     * - when it strictly decreases on each iteration, it can serve as a variant for termination
     *
     * Note: this is a conservative/simple choice and is not guaranteed to be strictly valid for
     * all loops (so we may bail out when getACSL fails).
     */
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

    /**
     * @brief Recognize a "linear search" idiom and generate path-sensitive invariants/post-info.
     *
     * Intuitively, this plugin targets the following control-flow structure:
     * - Normal path: loop exits because the loop condition becomes false (no match found)
     * - Interrupted path: some iteration satisfies the "hit condition" and triggers break/return
     *   (match found)
     *
     * Requirements:
     * - `inactivePaths.size() == 1`: exactly one interrupted path (the "found" path)
     * - index step is ±1 (to build quantifier bounds and ranges)
     * - the interrupted path has exactly one path condition (currently we only handle a single
     *   predicate)
     *
     * Core idea:
     * - Take the interrupted-path predicate (e.g. a[i] == x), and rewrite the symbols that vary
     *   with i into a form parameterized by the quantifier variable k (using init/step from
     *   patternInfo)
     * - Build two quantifiers:
     *   - Normal path: forall k in range. !pred(k) (no hit in the scanned range)
     *   - Interrupted path: exists k in range. pred(k) (at least one hit)
     * - Try to print the forall form as an ACSL `loop invariant \forall integer k; ...;`
     */
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

            // Substitute a Symbol with an expression over the quantifier variable k:
            // - If the symbol's address has a (init, step) pattern in patternInfo, build
            //   init + step * (k - i_init) (or the reversed form depending on step direction)
            // - Otherwise treat the value as loop-invariant and just clone it
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
                            std::make_unique<symb::detail::LiteralExprNode>(step), Multiply,
                            std::make_unique<symb::BinaryOpExpr>(
                                std::make_unique<symb::SymbolAddress::RangeIndex>("k"), Subtract,
                                indexInfo.indexSymbolicValue->clone())));
                // x_init + x_step * (index_init - index)
                return std::make_unique<symb::BinaryOpExpr>(
                    initValue->clone(), Add,
                    std::make_unique<symb::BinaryOpExpr>(
                        std::make_unique<symb::detail::LiteralExprNode>(step), Multiply,
                        std::make_unique<symb::BinaryOpExpr>(
                            indexInfo.indexSymbolicValue->clone(), Subtract,
                            std::make_unique<symb::SymbolAddress::RangeIndex>("k"))));
            }; // getSubExpr ends

            // Try to extract a "common concrete value" across multiple real entry paths:
            // - If substituting the expression on each entry path yields the same result, return
            //   that common value
            // - Otherwise return nullopt (meaning the initial value may differ across entry paths)
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
                // If the symbol's source address is a SymbolAddress (typical for array/pointer
                // deref), we also need to substitute symbols used in its offset; meanwhile we keep
                // a copy of the base SymbolAddress to build the quantified range later.
                if (auto fromSymbolAddr =
                        symb::dyn_cast<const symb::SymbolAddress>(fromAddr.value().get().get())) {
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

            // Rewrite the interrupted predicate by replacing its symbols with k-parameterized
            // expressions, yielding pred(k).
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
            // arrayRange denotes the array/pointer access range to quantify over:
            // - step>0: from current index to bound (excluding bound)
            // - step<0: from bound to current index (excluding current index)
            // Note: we represent the range via SymbolAddress offset/length; printing is handled by
            // the getACSL layer.
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

            // Try to print the forall form as a concrete ACSL text. If that fails, we still return
            // post-info but do not emit an invariant clause.
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
