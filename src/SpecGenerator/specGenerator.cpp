/**
 * @file specGenerator.cpp
 * @brief Implements ACSL generation using plugin dispatch for functions and loops.
 */
#include <algorithm>
#include <iterator>
#include <llvm/Support/Casting.h>
#include <ranges>
#include <llvm/ADT/TypeSwitch.h>
#include <unordered_set>

#include "specGenerator.h"
#include "Symbolic/expr.h"
#include "utilityTemplates.h"
#include "macros.h"
#include "state.h"

namespace acslg::spec_generator {
    namespace symb = acslg::analyzer::symbolic;

    namespace {
        // auxiliary function
        /**
         * @brief Fetch plugins by group name and filter by desired interface type.
         * @tparam T Target plugin subclass to collect.
         * @param groupName Group identifier registered in ACSLPluginGroupRegistry.
         * @return Vector of plugin pointers cast to the requested type.
         *
         * This helper resolves group membership, reports unknown groups early, and preserves the
         * order defined in the group registration for deterministic generation.
         */
        template <typename T> std::vector<const T *> getPlugins(std::string_view groupName) {
            const ACSLPluginGroup *group = ACSLPluginGroupRegistry::instance().getGroup(groupName);
            if (!group) {
                auto names = ACSLPluginGroupRegistry::instance().allGroupNames();
                std::string allName;
                allName += "[";
                for (auto &name : names) {
                    allName += name;
                    allName += ", ";
                }
                if (allName.size() > 1) {
                    // No flag!
                    allName.pop_back();
                    allName.pop_back();
                }
                allName += "]";
                ERROR("Unknown ACSL group: " + std::string{groupName} +
                      ". All registered groups: " + allName);
            }

            std::vector<std::string> ids = group->pluginIds;

            std::vector<const T *> plugins;
            for (auto &pid : ids) {
                auto *pl = ACSLPluginRegistry::instance().get(pid);
                if (!pl)
                    ERROR("Plugin with id " + pid + " does not exist!");
                auto *fcp = llvm::dyn_cast<const T>(pl);
                if (fcp) {
                    plugins.push_back(fcp);
                }
            }
            return plugins;
        }

        symb::ExprHandle unknownHandle() {
            return symb::ExprFactoryScope::current().unknown();
        }

    } // namespace

    /**
     * @brief Render the pattern as a readable string for debugging.
     * @return Textual dump.
     */
    std::string LoopInfo::Pattern::dump() const {
        using namespace utils::dump_fmt;

        std::ostringstream oss;

        oss << type("LoopPattern") << " {\n";
        oss << "  " << key("initialValue") << ": ";
        oss << initialValue->dump() << "\n";
        oss << "  " << key("step") << ": " << lit(std::to_string(step)) << "\n";
        oss << "}";

        return oss.str();
    }

    LoopInfo::LoopInfo(const clang::Stmt *ls)
        : loopStmt(ls), initStmt{nullptr}, condExpr{nullptr}, incStmt(nullptr), bodyStmt(nullptr) {
        if (const auto *forStmt = dyn_cast<clang::ForStmt>(loopStmt)) {
            initStmt = forStmt->getInit();
            condExpr = forStmt->getCond();
            incStmt  = forStmt->getInc();
            bodyStmt = forStmt->getBody();
        } else if (const auto *whileStmt = dyn_cast<clang::WhileStmt>(loopStmt)) {
            condExpr = whileStmt->getCond();
            bodyStmt = whileStmt->getBody();
        } else if (const auto *doWhileStmt = dyn_cast<clang::DoStmt>(loopStmt)) {
            condExpr = doWhileStmt->getCond();
            bodyStmt = doWhileStmt->getBody();
        } else {
            ERROR("LoopStmt should be a clang::Stmt of loop.");
        }
    }

    /**
     * @brief Generate an ACSL function contract by invoking the configured plugins.
     * @param pre [in] Program state at function entry.
     * @param post [in] Program state after symbolic execution.
     * @param groupName [in] Plugin group controlling which contract clauses are produced.
     * @return Pair of complete ACSL text and set of SourcePoints referenced.
     */
    [[nodiscard]] std::pair<std::string, std::unordered_set<symb::SourcePoint>> emitFunctionContract(
        const analyzer::ProgramState &pre,
        const analyzer::ProgramState &post,
        std::string_view groupName) {
        symb::ExprFactoryScope exprScope(pre.getExprFactory());
        auto plugins     = getPlugins<FunctionContractPlugin>(groupName);
        std::string spec = ACSL_HEAD.to_string();

        std::unordered_set<symb::SourcePoint> allUsedPoints;
        for (auto &plugin : plugins) {
            if (plugin == nullptr)
                continue;
            DEBUG("Plugin {" + std::string{plugin->id()} + "} is generating...");
            if (auto [s, usedPoints] = plugin->generate(pre, post); s) {
                spec += *s;
                // Accumulate labels so callers can emit the necessary marker statements once.
                allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                     std::make_move_iterator(usedPoints.end()));
            }
        }
        spec += ACSL_END.to_string();
        return std::pair{spec, std::move(allUsedPoints)};
    }

    /**
     * @brief Run loop info plugins to populate LoopInfo for a loop statement.
     * @param preState [in] State before the loop.
     * @param loopEntry [in] State representing entry into the loop.
     * @param loopStmt [in] Loop statement to analyze.
     * @param groupName [in] Plugin group to execute.
     * @return Pair of LoopInfo and success flag (false aborts generation).
     */
    std::pair<LoopInfo, bool> parseLoopInfo(const analyzer::ProgramState &preState,
                                            const analyzer::ProgramState &loopEntry,
                                            const clang::Stmt *loopStmt,
                                            std::string_view groupName) {
        symb::ExprFactoryScope exprScope(preState.getExprFactory());
        auto plugins = getPlugins<LoopInfoPlugin>(groupName);

        LoopInfo loopInfo{loopStmt};
        for (auto &plugin : plugins) {
            if (plugin == nullptr)
                UNREACHABLE();
            DEBUG("Plugin {" + std::string{plugin->id()} + "} is parsing...");
            if (!plugin->parse(preState, loopEntry, loopInfo))
                return std::pair{std::move(loopInfo), false};
        }
        return std::pair{std::move(loopInfo), true};
    }

    /**
     * @brief Parse loop info for complex loops; does not abort on plugin failure.
     * @param preState [in] State before the loop.
     * @param loopEntry [in] State at loop entry.
     * @param loopInfo [in,out] Loop information object to be filled.
     * @param groupName [in] Plugin group to execute.
     */
    void parseComplexLoopInfo(const analyzer::ProgramState &preState,
                              const analyzer::ProgramState &loopEntry,
                              LoopInfo &loopInfo,
                              std::string_view groupName) {
        symb::ExprFactoryScope exprScope(preState.getExprFactory());
        auto plugins = getPlugins<LoopInfoPlugin>(groupName);

        for (auto &plugin : plugins) {
            if (plugin == nullptr)
                UNREACHABLE();
            DEBUG("Plugin {" + std::string{plugin->id()} + "} is parsing...");
            if (!plugin->parse(preState, loopEntry, loopInfo))
                ERROR("Plugin: {" + std::string{plugin->id()} +
                      "} parsing complex loop's information failed.");
        }
    }

    /**
     * @brief Generate loop invariants, assigns, and variants using path-insensitive and
     *        path-sensitive plugins.
     * @param preState [in] State before entering the loop.
     * @param loopEntry [in] State representing loop entry.
     * @param loopInfo [in] Parsed loop metadata.
     * @param piGroupName [in] Path-insensitive plugin group identifier.
     * @param psGroupName [in] Path-sensitive plugin group identifier.
     * @return ACSL clauses, used SourcePoints, and merged post-loop state.
     */
    [[nodiscard]] EmitLoopInvResult emitLoopInvariant(const analyzer::ProgramState &preState,
                                                      const analyzer::ProgramState &loopEntry,
                                                      const LoopInfo &loopInfo,
                                                      std::string_view piGroupName,
                                                      std::string_view psGroupName) {
        symb::ExprFactoryScope exprScope(preState.getExprFactory());
        auto piPlugins = getPlugins<PathInsensitiveLoopInvPlugin>(piGroupName);
        auto psPlugins = getPlugins<PathSensitiveLoopInvPlugin>(psGroupName);
        std::vector<std::unique_ptr<analyzer::Path>> invariants;
        std::string spec = ACSL_HEAD.to_string();
        std::unordered_set<symb::SourcePoint> allUsedPoints;
        std::vector<std::string> assignsClauses;
        std::vector<std::string> invariantClauses;
        std::vector<std::string> variantClauses;

        enum class LoopClauseKind {
            Assigns,
            Invariant,
            Variant
        };
        auto classifyClauseKind = [](std::string_view clause) {
            auto startsWith = [](std::string_view text, std::string_view prefix) {
                return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
            };
            auto firstNotSpace = clause.find_first_not_of(" \t");
            if (firstNotSpace != std::string_view::npos)
                clause.remove_prefix(firstNotSpace);
            if (startsWith(clause, "loop assigns"))
                return LoopClauseKind::Assigns;
            if (startsWith(clause, "loop variant"))
                return LoopClauseKind::Variant;
            return LoopClauseKind::Invariant;
        };
        auto storeClause = [&](std::string clause) {
            switch (classifyClauseKind(clause)) {
                case LoopClauseKind::Assigns: assignsClauses.emplace_back(std::move(clause)); break;
                case LoopClauseKind::Variant: variantClauses.emplace_back(std::move(clause)); break;
                case LoopClauseKind::Invariant:
                default: invariantClauses.emplace_back(std::move(clause)); break;
            }
        };

        auto loopEntryPoint = symb::SourcePoint::fromStmtBefore(
            loopInfo.loopStmt, loopEntry.getContext().getSourceManager(),
            loopEntry.getContext().getLangOptions());
        if (preState.getPaths().size() != loopEntry.getPaths().size()) {
            // The implementation assumes a 1:1 mapping of pre/entry paths; bail out if branching
            // diverged earlier.
            ERROR("Branch is unsupported here.");
        }

        auto &entryPaths = loopEntry.getPaths();
        auto pathNum     = preState.getPaths().size();
        auto postState   = preState.clone(/*with path*/ false);
        assert(loopInfo.entryAndCurrentInfo);
        size_t interruptPathNum = loopInfo.entryAndCurrentInfo->inactivePaths.size();

        auto exprEqual = [](const symb::SymbolicExpr &lhs, const symb::SymbolicExpr &rhs) {
            return lhs.equal(rhs);
        };

        auto mergePostInfo = [&](PostPSInfo &lhs, const PostPSInfo &rhs) {
            if (lhs.pathState != rhs.pathState)
                ERROR("PathState mismatch when merging post infos.");

            std::unordered_set<symb::AddressBox, symb::AddressBoxHash, symb::AddressBoxEq> addresses;
            for (const auto &[addr, _] : lhs.memoryMap)
                addresses.insert(addr);
            for (const auto &[addr, _] : rhs.memoryMap)
                addresses.insert(addr);

            for (const auto &addr : addresses) {
                auto lhsIt = lhs.memoryMap.find(addr);
                auto rhsIt = rhs.memoryMap.find(addr);

                if (lhsIt != lhs.memoryMap.end() && rhsIt != rhs.memoryMap.end()) {
                    if (exprEqual(*lhsIt->second, *rhsIt->second))
                        continue;
                } else if (rhsIt != rhs.memoryMap.end()) {
                    // If only rhs writes the address, prefer its value to preserve available info.
                    continue;
                } else {
                    // If lhs is the only writer, keep it; otherwise fall through to unknown.
                    continue;
                }

                lhs.memoryMap.insert_or_assign(addr, unknownHandle());
            }

            analyzer::PathConditions intersected;
            intersected.reserve(std::min(lhs.pathConds.size(), rhs.pathConds.size()));
            for (const auto &cond : lhs.pathConds)
                if (rhs.pathConds.find(cond) != rhs.pathConds.end())
                    intersected.emplace(cond);
            lhs.pathConds = std::move(intersected);

            if (lhs.pathState == analyzer::Path::PathState::Return) {
                if (!(lhs.returnExpr && rhs.returnExpr))
                    UNREACHABLE();
                if (!exprEqual(*lhs.returnExpr.value(), *rhs.returnExpr.value())) {
                    lhs.returnExpr = unknownHandle();
                }
            }
        };

        auto mergeAllPostInfos =
            [&](const std::vector<PostPSInfo> &infos) -> std::optional<PostPSInfo> {
            if (infos.empty())
                return std::nullopt;
            PostPSInfo merged{infos.front()};
            for (size_t i = 1; i < infos.size(); ++i)
                mergePostInfo(merged, infos.at(i));
            return merged;
        };

        struct BranchInfos {
            std::vector<PostPSInfo> normal;
            std::vector<std::vector<PostPSInfo>> interrupts;
        };
        auto resultInfos = std::vector<BranchInfos>{pathNum};
        for (auto &infos : resultInfos)
            infos.interrupts.resize(interruptPathNum);

        auto updateResultInfoWithInfo = [&loopEntryPoint](const analyzer::Path &currentPath,
                                                          PostPSInfo &toUpdate, auto &&info) {
            auto &factory = symb::ExprFactoryScope::current();
            for (const auto &[addr, value] : info.memoryMap) {
                auto subedAddrExpr =
                    symb::getSubstitutedExprHandle(factory, addr.get(), currentPath, loopEntryPoint);
                auto subedAddr = symb::dyn_cast<const symb::Address>(subedAddrExpr.get().get());
                if (subedAddr == nullptr)
                    UNREACHABLE();
                auto subedValue =
                    symb::getSubstitutedExprHandle(factory, *value, currentPath, loopEntryPoint);
                if (auto it = toUpdate.memoryMap.find(*subedAddr);
                    it != toUpdate.memoryMap.end() && !it->second->isUnknown()) {
                    WARN("Another plugin has already updated this address. The new value: "
                         "{" +
                         subedValue->dump() + "} is discarded.");
                    continue;
                }
                toUpdate.memoryMap.insert_or_assign(*subedAddr, subedValue);
            }

            for (const auto &cond : info.pathConds) {
                // Substitute conditions so they refer to the current path's viewpoint of the loop
                // entry.
                auto subedConds =
                    symb::getSubstitutedExprHandle(factory, *cond, currentPath, loopEntryPoint);
                toUpdate.pathConds.emplace(subedConds);
                // todo: may insert for each unmodified position:
                // Symbol(with fromPoint_ = afterLoop) == the current value.
            }

            if constexpr (requires { info.pathState; }) {
                toUpdate.pathState = info.pathState;
                if (info.pathState == analyzer::Path::PathState::Return) {
                    assert(info.returnExpr);
                    if (info.returnExpr.value()->isUnknown())
                        return;
                    auto subedReturnExpr =
                        symb::getSubstitutedExprHandle(
                            factory, *info.returnExpr.value(), currentPath, loopEntryPoint);
                    toUpdate.returnExpr = subedReturnExpr;
                }
            }
        };

        auto updateResultInfosWithGlobalInfo = [&](PostPIInfo &info) {
            // for every pre-path
            for (auto i : std::views::iota(size_t{0}, pathNum)) {
                auto &entryPath         = *entryPaths.at(i);
                auto &postBranchesInfos = resultInfos.at(i).normal;

                // if there're no post-path (this function may be called before
                // `updateResultInfosWithPerPathInfo`), insert one.
                if (postBranchesInfos.empty())
                    postBranchesInfos.emplace_back();

                // It's global post information, so apply it to every post-path.
                for (auto &postBranchInfo : postBranchesInfos)
                    updateResultInfoWithInfo(entryPath, postBranchInfo, info);
            }
        }; // updateResultInfosWithGlobalInfo

        auto updateResultInfosWithGlobalInterruptInfo = [&](size_t interruptIdx, PostPIInfo &info) {
            for (auto i : std::views::iota(size_t{0}, pathNum)) {
                auto &entryPath = *entryPaths.at(i);
                auto &branches  = resultInfos.at(i).interrupts.at(interruptIdx);

                if (branches.empty())
                    branches.emplace_back();

                for (auto &postBranchInfo : branches)
                    updateResultInfoWithInfo(entryPath, postBranchInfo, info);
            }
        }; // updateResultInfosWithGlobalInterruptInfo

        auto updateResultInfosWithMergedNormalPathInfo = [&](PostPSInfo &info) {
            for (auto i : std::views::iota(size_t{0}, pathNum)) {
                auto &entryPath = *entryPaths.at(i);
                auto &branches  = resultInfos.at(i).normal;

                if (branches.empty())
                    branches.emplace_back();

                for (auto &postBranchInfo : branches)
                    updateResultInfoWithInfo(entryPath, postBranchInfo, info);
            }
        }; // updateResultInfosWithMergedNormalPathInfo

        auto updateResultInfosWithMergedInterruptPathInfo = [&](size_t interruptIdx,
                                                                PostPSInfo &info) {
            for (auto i : std::views::iota(size_t{0}, pathNum)) {
                auto &entryPath = *entryPaths.at(i);
                auto &branches  = resultInfos.at(i).interrupts.at(interruptIdx);

                if (branches.empty())
                    branches.emplace_back();

                for (auto &postBranchInfo : branches)
                    updateResultInfoWithInfo(entryPath, postBranchInfo, info);
            }
        }; // updateResultInfosWithMergedInterruptPathInfo

        auto updateResultInfosWithPerPathInfo = [&](std::vector<PostPSInfo> &infos) {
            // for every pre-path
            for (auto i : std::views::iota(size_t{0}, pathNum)) {
                auto &entryPath         = *entryPaths.at(i);
                auto &postBranchesInfos = resultInfos.at(i).normal;

                // If there're no post-path (this function may be called before
                // `updateResultInfosWithGlobalInfo`), insert one.
                if (postBranchesInfos.empty())
                    postBranchesInfos.emplace_back();
                if (postBranchesInfos.size() != 1)
                    ERROR("ResultInfos should only be updated once per-path.");

                // Generate infos.size() post-paths.
                for (size_t j = 1; j < infos.size(); ++j)
                    postBranchesInfos.push_back(postBranchesInfos.back());

                assert(postBranchesInfos.size() == infos.size());
                // It's global post information, so apply it to every post-path.
                for (size_t j = 0; j < postBranchesInfos.size(); ++j) {
                    auto &postBranchInfo = postBranchesInfos.at(j);
                    auto &info           = infos.at(j);
                    updateResultInfoWithInfo(entryPath, postBranchInfo, info);
                }
            }
        }; // updateResultInfosWithPerPathInfo

        auto updateResultInfosWithPerInterruptPathInfo = [&](size_t interruptIdx,
                                                             std::vector<PostPSInfo> &infos) {
            for (auto i : std::views::iota(size_t{0}, pathNum)) {
                auto &entryPath         = *entryPaths.at(i);
                auto &postBranchesInfos = resultInfos.at(i).interrupts.at(interruptIdx);

                if (postBranchesInfos.empty())
                    postBranchesInfos.emplace_back();
                if (postBranchesInfos.size() != 1)
                    ERROR("ResultInfos should only be updated once per-path.");

                for (size_t j = 1; j < infos.size(); ++j)
                    postBranchesInfos.push_back(postBranchesInfos.back());

                assert(postBranchesInfos.size() == infos.size());
                for (size_t j = 0; j < postBranchesInfos.size(); ++j) {
                    auto &postBranchInfo = postBranchesInfos.at(j);
                    auto &info           = infos.at(j);
                    updateResultInfoWithInfo(entryPath, postBranchInfo, info);
                }
            }
        }; // updateResultInfosWithPerInterruptPathInfo

        for (auto &piPlugin : piPlugins) {
            if (piPlugin == nullptr)
                UNREACHABLE();
            DEBUG("Plugin {" + std::string{piPlugin->id()} + "} is generating...");
            auto [s, usedPoints, normalPostInfo, interruptPathsPostInfo] =
                piPlugin->generate(preState, loopEntry, loopInfo);

            if (s) {
                storeClause(std::move(*s));
            }
            allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                 std::make_move_iterator(usedPoints.end()));

            updateResultInfosWithGlobalInfo(normalPostInfo);
            if (interruptPathsPostInfo.empty())
                continue;
            if (interruptPathNum != interruptPathsPostInfo.size()) {
                ERROR("Interrupt paths count mismatch between LoopInfo and plugin result.");
            } else {
                for (size_t idx = 0; idx < interruptPathsPostInfo.size(); ++idx)
                    updateResultInfosWithGlobalInterruptInfo(idx, interruptPathsPostInfo[idx]);
            }
        }

        std::optional<size_t> bestPriority{};
        std::optional<PathSensitiveLoopInvPlugin::GenResultType> bestResult{};
        std::vector<PathSensitiveLoopInvPlugin::GenResultType> otherResults;
        for (auto &psPlugin : psPlugins) {
            if (psPlugin == nullptr)
                UNREACHABLE();
            DEBUG("Plugin {" + std::string{psPlugin->id()} + "} is generating...");

            auto currentPriority = psPlugin->propose();
            auto res             = psPlugin->tryGenerate(preState, loopEntry, loopInfo);
            if (res == std::nullopt)
                continue;

            if (res->acsl) {
                storeClause(std::move(res->acsl.value()));
                allUsedPoints.insert(std::make_move_iterator(res.value().acslUsedPoints.begin()),
                                     std::make_move_iterator(res.value().acslUsedPoints.end()));
            }

            if (!bestPriority || currentPriority > bestPriority.value()) {
                if (bestResult)
                    otherResults.push_back(std::move(bestResult.value()));
                bestPriority = currentPriority;
                bestResult   = std::move(res.value());
            } else {
                otherResults.push_back(std::move(res.value()));
            }
        }
        if (bestResult) {
            updateResultInfosWithPerPathInfo(bestResult->normalPathPostInfos);
            if (interruptPathNum != bestResult->interruptPathsPostInfos.size()) {
                ERROR("Interrupt paths count mismatch between LoopInfo and plugin result.");
            } else {
                for (size_t idx = 0; idx < bestResult->interruptPathsPostInfos.size(); ++idx)
                    updateResultInfosWithPerInterruptPathInfo(
                        idx, bestResult->interruptPathsPostInfos.at(idx));
            }
        }

        for (auto &res : otherResults) {
            if (auto merged = mergeAllPostInfos(res.normalPathPostInfos))
                updateResultInfosWithMergedNormalPathInfo(*merged);

            if (interruptPathNum != res.interruptPathsPostInfos.size()) {
                ERROR("Interrupt paths count mismatch between LoopInfo and plugin result.");
                continue;
            }
            for (size_t idx = 0; idx < res.interruptPathsPostInfos.size(); ++idx) {
                if (auto merged = mergeAllPostInfos(res.interruptPathsPostInfos.at(idx)))
                    updateResultInfosWithMergedInterruptPathInfo(idx, *merged);
            }
        }
        auto appendClauses = [&](const std::vector<std::string> &clauses) {
            for (const auto &clause : clauses) {
                spec += "    " /*4 spaces*/ + clause + "\n";
            }
        };
        appendClauses(assignsClauses);
        appendClauses(invariantClauses);
        appendClauses(variantClauses);
        spec += ACSL_END.to_string();

        assert(postState->getPaths().empty());
        // Build post-state from result infos.
        for (auto i : std::views::iota(size_t{0}, pathNum)) {
            auto &prePath = preState.getPaths().at(i);
            if (!prePath->isActive()) {
                postState->insertPath(prePath->clone());
                continue;
            }
            auto &postBranchesInfos = resultInfos.at(i);

            auto appendPostPaths = [&](std::vector<PostPSInfo> &branches) {
                for (auto &postBranchInfo : branches) {
                    auto postPath = prePath->clone();
                    for (auto [addr, value] : postBranchInfo.memoryMap) {
                        auto root = addr.get().getFromRoot();
                        if (root == std::nullopt)
                            TODO();
                        // Skip writes to symbols that were not visible in the pre-path to avoid
                        // inventing new locals.
                        if (!postPath->getVarAddr().contains(root.value()))
                            continue;
                        postPath->updateMemory(addr, value);
                    }
                    // Carry over path termination state and optional return expression.
                    postPath->setPathState(postBranchInfo.pathState);
                    if (postBranchInfo.returnExpr)
                        postPath->setReturnExpr(postBranchInfo.returnExpr.value());
                    else
                        postPath->setReturnExpr(std::nullopt);
                    // Reapply substituted path conditions produced by plugins.
                    for (auto pathCond : postBranchInfo.pathConds)
                        postPath->insertPathCondition(pathCond);
                    postState->insertPath(std::move(postPath));
                }
            };

            if (postBranchesInfos.normal.empty() &&
                std::ranges::all_of(postBranchesInfos.interrupts,
                                    [](const auto &branches) { return branches.empty(); }))
                WARN("A path has no post-info, there might be some errors.");

            appendPostPaths(postBranchesInfos.normal);
            for (auto &interruptInfos : postBranchesInfos.interrupts)
                appendPostPaths(interruptInfos);
        }

        return EmitLoopInvResult{.acsl       = spec,
                                 .usedPoints = std::move(allUsedPoints),
                                 .postState  = std::move(postState)};
    }
} // namespace acslg::spec_generator
