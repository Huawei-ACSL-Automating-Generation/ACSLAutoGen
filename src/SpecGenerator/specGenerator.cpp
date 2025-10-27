// src/SpecGenerator/specGenerators.cpp

#include <iterator>
#include <llvm-19/llvm/Support/Casting.h>
#include <ranges>
#include <llvm/ADT/TypeSwitch.h>
#include <unordered_set>

#include "specGenerator.h"
#include "expr.h"
#include "utilityTemplates.h"
#include "macros.h"
#include "state.h"

namespace acslg::spec_generator {
    namespace symb = acslg::analyzer::symbolic;

    namespace {
        // auxiliary function
        template <typename T>
        std::vector<const T *> getPlugins(
            std::string_view groupName,
            std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds) {
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
            if (extraPluginIds)
                ids.insert(ids.end(), (*extraPluginIds).get().begin(),
                           (*extraPluginIds).get().end());

            std::vector<const T *> plugins;
            for (auto &pid : ids) {
                auto *pl = ACSLPluginRegistry::instance().get(pid);
                if (!pl)
                    ERROR("Plugin with id " + pid + " does not exist!");
                auto *fcp = dynamic_cast<const T *>(pl);
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
        initialValue_ = other.initialValue_->clone().into_underlying();
        step_         = other.step_;
        return *this;
    }

    std::string LoopInfo::Pattern::dump() const {
        using namespace utils::dump_fmt;

        std::ostringstream oss;

        oss << type("LoopPattern") << " {\n";
        oss << "  " << key("initialValue") << ": ";
        oss << initialValue_->dump() << "\n";
        oss << "  " << key("step") << ": " << lit(std::to_string(step_)) << "\n";
        oss << "}";

        return oss.str();
    }

    LoopInfo::LoopInfo(const clang::Stmt *loopStmt)
        : loopStmt_(loopStmt), initStmt_{nullptr}, condExpr_{nullptr}, incStmt_(nullptr),
          bodyStmt_(nullptr) {
        if (const auto *forStmt = dyn_cast<clang::ForStmt>(loopStmt)) {
            initStmt_ = forStmt->getInit();
            condExpr_ = forStmt->getCond();
            incStmt_  = forStmt->getInc();
            bodyStmt_ = forStmt->getBody();
        } else if (const auto *whileStmt = dyn_cast<clang::WhileStmt>(loopStmt)) {
            condExpr_ = whileStmt->getCond();
            bodyStmt_ = whileStmt->getBody();
        } else if (const auto *doWhileStmt = dyn_cast<clang::DoStmt>(loopStmt)) {
            condExpr_ = doWhileStmt->getCond();
            bodyStmt_ = doWhileStmt->getBody();
        } else {
            ERROR("LoopStmt should be a clang::Stmt of loop.");
        }
    }

    [[nodiscard]]
    std::pair<std::string, std::unordered_set<symb::SourcePoint>> emitFunctionContract(
        const analyzer::ProgramState &pre,
        const analyzer::ProgramState &post,
        std::string_view groupName,
        std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds) {
        auto plugins     = getPlugins<FunctionContractPlugin>(groupName, extraPluginIds);
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

    std::pair<LoopInfo, bool> parseLoopInfo(
        const analyzer::ProgramState &preState,
        const analyzer::ProgramState &loopEntry,
        const clang::Stmt *loopStmt,
        std::string_view groupName,
        std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds) {
        auto plugins = getPlugins<LoopInfoPlugin>(groupName, extraPluginIds);

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

    void parseComplexLoopInfo(
        const analyzer::ProgramState &preState,
        const analyzer::ProgramState &loopEntry,
        LoopInfo &loopInfo,
        std::string_view groupName,
        std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds) {
        auto plugins = getPlugins<LoopInfoPlugin>(groupName, extraPluginIds);

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
    EmitLoopInvResult emitLoopInvariant(
        const analyzer::ProgramState &preState,
        const analyzer::ProgramState &loopEntry,
        const LoopInfo &loopInfo,
        std::string_view groupName,
        std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds) {
        auto plugins = getPlugins<LoopInvariantPlugin>(groupName, extraPluginIds);
        std::vector<std::unique_ptr<analyzer::Path>> invariants;
        std::string spec = ACSL_HEAD.to_string();
        std::unordered_set<symb::SourcePoint> allUsedPoints;

        auto loopEntryPoint = symb::SourcePoint::fromStmtBefore(
            loopInfo.loopStmt_, loopEntry.getContext().getSourceManager(),
            loopEntry.getContext().getLangOptions());
        auto postState   = preState.clone();
        auto &postPaths  = postState->getPaths();
        auto pathNum     = postPaths.size();
        auto resultInfos = std::vector<std::vector<PostInfo>>{pathNum};

        // Update the resultInfos with a plugin's postInfo.
        auto updateResultInfos = [&](std::vector<PostInfo> &infos) {
            if (infos.empty())
                return;
            if (preState.getPaths().size() != loopEntry.getPaths().size()) {
                ERROR("Branch is unsupported here.");
            }

            auto &entryPaths = loopEntry.getPaths();

            for (auto i : std::views::iota(size_t{0}, pathNum)) {
                auto &entryPath         = *entryPaths.at(i);
                auto &postBranchesInfos = resultInfos.at(i);

                if (infos.size() != 1)
                    TODO();
                if (postBranchesInfos.empty())
                    postBranchesInfos.emplace_back();
                auto &info           = infos.at(0);
                auto &postBranchInfo = postBranchesInfos.at(0);

                for (auto &[addr, value] : info.memoryMap_) {
                    auto subedAddrExpr = addr.get().getSubstitutedExpr(entryPath, loopEntryPoint);
                    auto subedAddr = llvm::dyn_cast<const symb::Address>(subedAddrExpr.get().get());
                    if (subedAddr == nullptr)
                        UNREACHABLE();
                    auto subedValue = value->getSubstitutedExpr(entryPath, loopEntryPoint);
                    if (auto it = postBranchInfo.memoryMap_.find(*subedAddr);
                        it != postBranchInfo.memoryMap_.end() && !it->second->isUnknown()) {
                        WARN("Another plugin has already updated this address. The new value: {" +
                             subedValue->dump() + "} is discarded.");
                        continue;
                    }
                    postBranchInfo.memoryMap_.insert_or_assign(*subedAddr, std::move(subedValue));
                }

                for (auto &cond : info.pathConds_) {
                    auto subedConds = cond->getSubstitutedExpr(entryPath, loopEntryPoint);
                    postBranchInfo.pathConds_.push_back(std::move(subedConds));
                }
            }
        }; // updatePostState

        for (auto &plugin : plugins) {
            if (plugin == nullptr)
                UNREACHABLE();
            DEBUG("Plugin {" + std::string{plugin->id()} + "} is generating...");
            auto [s, usedPoints, continueFlag, postInfos] =
                plugin->generate(preState, loopEntry, loopInfo);

            if (s) {
                spec += "    " /*4 spaces*/ + *s + "\n";
            }
            allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                 std::make_move_iterator(usedPoints.end()));

            updateResultInfos(postInfos);

            if (!continueFlag)
                break;
        }
        spec += ACSL_END.to_string();

        // Build post-state from result infos.
        for (auto i : std::views::iota(size_t{0}, pathNum)) {
            auto &prePath           = preState.getPaths().at(i);
            auto &postPath          = postPaths.at(i);
            auto &postBranchesInfos = resultInfos.at(i);

            if (postBranchesInfos.empty()) {
                WARN("A path has no post-info, there might be some errors.");
                continue;
            }

            if (postBranchesInfos.size() != 1)
                TODO();

            auto &postBranchInfo = postBranchesInfos.at(0);
            for (auto &[addr, value] : postBranchInfo.memoryMap_) {
                auto root = addr.get().getFromRoot();
                if (root == std::nullopt)
                    TODO();
                if (!postPath->getVarAddr().contains(root.value()))
                    continue;
                postPath->updateMemory(addr, std::move(value));
            }
            for (auto &&[addr, value] : prePath->getMemoryState().flat()) {
                if (postPath->getMemoryState().contains(addr))
                    continue;
                postPath->updateMemory(addr, value->clone());
            }

            for (auto &pathCond : prePath->getPathConditions())
                postPath->insertPathCondition(pathCond->clone());
            for (auto &pathCond : postBranchInfo.pathConds_)
                postPath->insertPathCondition(std::move(pathCond));
        }

        return EmitLoopInvResult{.acsl       = spec,
                                 .usedPoints = std::move(allUsedPoints),
                                 .postState  = std::move(postState)};
    }
} // namespace acslg::spec_generator