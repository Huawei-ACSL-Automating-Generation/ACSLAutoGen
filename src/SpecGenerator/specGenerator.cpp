// src/SpecGenerator/specGenerators.cpp

#include <iterator>
#include <llvm-19/llvm/Support/Casting.h>
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
    } // namespace

    LoopInfo::Pattern &LoopInfo::Pattern::operator=(const Pattern &other) {
        if (this == &other)
            return *this;
        initialValue = other.initialValue->clone().into_underlying();
        step         = other.step;
        return *this;
    }

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

    LoopInfo::LoopInfo(const clang::Stmt *loopStmt)
        : loopStmt(loopStmt), initStmt{nullptr}, condExpr{nullptr}, incStmt(nullptr),
          bodyStmt(nullptr) {
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

    [[nodiscard]]
    std::pair<std::string, std::unordered_set<symb::SourcePoint>> emitFunctionContract(
        const analyzer::ProgramState &pre,
        const analyzer::ProgramState &post,
        std::string_view groupName) {
        auto plugins     = getPlugins<FunctionContractPlugin>(groupName);
        std::string spec = ACSL_HEAD.to_string();

        std::unordered_set<symb::SourcePoint> allUsedPoints;
        for (auto &plugin : plugins) {
            if (plugin == nullptr)
                continue;
            DEBUG("Plugin {" + std::string{plugin->id()} + "} is generating...");
            if (auto [s, usedPoints] = plugin->generate(pre, post); s) {
                spec += *s;
                allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                     std::make_move_iterator(usedPoints.end()));
            }
        }
        spec += ACSL_END.to_string();
        return std::pair{spec, std::move(allUsedPoints)};
    }

    std::pair<LoopInfo, bool> parseLoopInfo(const analyzer::ProgramState &preState,
                                            const analyzer::ProgramState &loopEntry,
                                            const clang::Stmt *loopStmt,
                                            std::string_view groupName) {
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

    void parseComplexLoopInfo(const analyzer::ProgramState &preState,
                              const analyzer::ProgramState &loopEntry,
                              LoopInfo &loopInfo,
                              std::string_view groupName) {
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

    [[nodiscard]]
    EmitLoopInvResult emitLoopInvariant(const analyzer::ProgramState &preState,
                                        const analyzer::ProgramState &loopEntry,
                                        const LoopInfo &loopInfo,
                                        std::string_view piGroupName,
                                        std::string_view psGroupName) {
        auto piPlugins = getPlugins<PathInsensitiveLoopInvPlugin>(piGroupName);
        auto psPlugins = getPlugins<PathSensitiveLoopInvPlugin>(psGroupName);
        std::vector<std::unique_ptr<analyzer::Path>> invariants;
        std::string spec = ACSL_HEAD.to_string();
        std::unordered_set<symb::SourcePoint> allUsedPoints;

        auto loopEntryPoint = symb::SourcePoint::fromStmtBefore(
            loopInfo.loopStmt, loopEntry.getContext().getSourceManager(),
            loopEntry.getContext().getLangOptions());
        if (preState.getPaths().size() != loopEntry.getPaths().size()) {
            ERROR("Branch is unsupported here.");
        }

        auto &entryPaths = loopEntry.getPaths();
        auto pathNum     = preState.getPaths().size();
        auto postState   = preState.clone(/*with path*/ false);
        auto resultInfos = std::vector<std::vector<PostPSInfo>>{pathNum};

        auto updateResultInfoWithInfo = [&loopEntryPoint](const analyzer::Path &currentPath,
                                                          PostPSInfo &toUpdate, auto &&info) {
            for (auto &[addr, value] : info.memoryMap) {
                auto subedAddrExpr = addr.get().getSubstitutedExpr(currentPath, loopEntryPoint);
                auto subedAddr     = llvm::dyn_cast<const symb::Address>(subedAddrExpr.get().get());
                if (subedAddr == nullptr)
                    UNREACHABLE();
                auto subedValue = value->getSubstitutedExpr(currentPath, loopEntryPoint);
                if (auto it = toUpdate.memoryMap.find(*subedAddr);
                    it != toUpdate.memoryMap.end() && !it->second->isUnknown()) {
                    WARN("Another plugin has already updated this address. The new value: "
                         "{" +
                         subedValue->dump() + "} is discarded.");
                    continue;
                }
                toUpdate.memoryMap.insert_or_assign(*subedAddr, std::move(subedValue));
            }

            for (auto &cond : info.pathConds) {
                auto subedConds = cond->getSubstitutedExpr(currentPath, loopEntryPoint);
                toUpdate.pathConds.push_back(std::move(subedConds));
                // todo: may insert for each unmodified position:
                // Symbol(with fromPoint_ = afterLoop) == the current value.
            }

            if constexpr (requires { info.pathState; }) {
                toUpdate.pathState = info.pathState;
            }
        };

        auto updateResultInfosWithGlobalInfo = [&](PostPIInfo &info) {
            // for every pre-path
            for (auto i : std::views::iota(size_t{0}, pathNum)) {
                auto &entryPath         = *entryPaths.at(i);
                auto &postBranchesInfos = resultInfos.at(i);

                // if there're no post-path (this function may be called before
                // `updateResultInfosWithPerPathInfo`), insert one.
                if (postBranchesInfos.empty())
                    postBranchesInfos.emplace_back();

                // It's global post information, so apply it to every post-path.
                for (auto &postBranchInfo : postBranchesInfos)
                    updateResultInfoWithInfo(entryPath, postBranchInfo, info);
            }
        }; // updateResultInfosWithGlobalInfo

        auto updateResultInfosWithPerPathInfo = [&](std::vector<PostPSInfo> &infos) {
            // for every pre-path
            for (auto i : std::views::iota(size_t{0}, pathNum)) {
                auto &entryPath         = *entryPaths.at(i);
                auto &postBranchesInfos = resultInfos.at(i);

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

        for (auto &piPlugin : piPlugins) {
            if (piPlugin == nullptr)
                UNREACHABLE();
            DEBUG("Plugin {" + std::string{piPlugin->id()} + "} is generating...");
            auto [s, usedPoints, postInfo] = piPlugin->generate(preState, loopEntry, loopInfo);

            if (s) {
                spec += "    " /*4 spaces*/ + *s + "\n";
            }
            allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                 std::make_move_iterator(usedPoints.end()));

            updateResultInfosWithGlobalInfo(postInfo);
        }

        std::optional<size_t> lastPriority{};
        for (auto &psPlugin : psPlugins) {
            if (psPlugin == nullptr)
                UNREACHABLE();
            DEBUG("Plugin {" + std::string{psPlugin->id()} + "} is generating...");

            auto currentPriority = psPlugin->propose();
            if (lastPriority && lastPriority.value() > currentPriority)
                continue;

            auto res = psPlugin->tryGenerate(preState, loopEntry, loopInfo);
            if (res == std::nullopt)
                continue;
            lastPriority = currentPriority;

            if (res.value().acsl) {
                spec += "    " /*4 spaces*/ + *res.value().acsl + "\n";
            }
            allUsedPoints.insert(std::make_move_iterator(res.value().acslUsedPoints.begin()),
                                 std::make_move_iterator(res.value().acslUsedPoints.end()));

            updateResultInfosWithPerPathInfo(res.value().perPathPostInfos);
        }
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

            if (postBranchesInfos.empty())
                WARN("A path has no post-info, there might be some errors.");

            for (auto &postBranchInfo : postBranchesInfos) {
                auto postPath = prePath->clone();
                for (auto &[addr, value] : postBranchInfo.memoryMap) {
                    auto root = addr.get().getFromRoot();
                    if (root == std::nullopt)
                        TODO();
                    if (!postPath->getVarAddr().contains(root.value()))
                        continue;
                    postPath->updateMemory(addr, std::move(value));
                }
                // for (auto &&[addr, value] : prePath->getMemoryState().flat()) {
                //     if (postPath->getMemoryState().contains(addr))
                //         continue;
                //     postPath->updateMemory(addr, value->clone());
                // }

                // for (auto &pathCond : prePath->getPathConditions())
                //     postPath->insertPathCondition(pathCond->clone());
                for (auto &pathCond : postBranchInfo.pathConds)
                    postPath->insertPathCondition(std::move(pathCond));
                postState->insertPath(std::move(postPath));
            }
        }

        return EmitLoopInvResult{.acsl       = spec,
                                 .usedPoints = std::move(allUsedPoints),
                                 .postState  = std::move(postState)};
    }
} // namespace acslg::spec_generator