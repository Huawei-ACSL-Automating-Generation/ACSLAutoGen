// src/SpecGenerator/specGenerators.cpp

#include <ranges>
#include "specGenerator.h"
#include "utilityTemplates.h"
#include "macros.h"
#include "state.h"

using namespace std;
using namespace clang;

namespace {
    // auxiliary function
    template <typename T>
    vector<const T *> getPlugins(std::string_view groupName,
                                 optional<reference_wrapper<const vector<string>>> extraPluginIds) {
        const ACSLPluginGroup *group = ACSLPluginGroupRegistry::instance().getGroup(groupName);
        if (!group) {
            auto names = ACSLPluginGroupRegistry::instance().allGroupNames();
            string allName;
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
            ERROR("Unknown ACSL group: " + string{groupName} +
                  ". All registered groups: " + allName);
        }

        vector<string> ids = group->pluginIds;
        if (extraPluginIds)
            ids.insert(ids.end(), (*extraPluginIds).get().begin(), (*extraPluginIds).get().end());

        vector<const T *> plugins;
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

string emitFunctionContract(const ProgramState &pre,
                            const ProgramState &post,
                            std::string_view groupName,
                            optional<reference_wrapper<const vector<string>>> extraPluginIds) {
    auto plugins = getPlugins<FunctionContractPlugin>(groupName, extraPluginIds);
    string spec  = ACSL_HEAD.to_string();

    for (auto &plugin : plugins) {
        if (plugin == nullptr)
            continue;
        if (auto s = plugin->generate(pre, post); s)
            spec += "    " /*4 spaces*/ + *s + "\n";
    }
    spec += ACSL_END.to_string();
    return spec;
}

std::optional<LoopInfo> parseLoopInfo(
    const ProgramState &preState,
    const ProgramState &loopEntry,
    const clang::Expr *cond,
    const clang::Stmt *inc,
    const clang::Stmt *body,
    std::string_view groupName,
    optional<reference_wrapper<const vector<string>>> extraPluginIds) {
    auto plugins = getPlugins<LoopInfoPlugin>(groupName, extraPluginIds);

    LoopInfo loopInfo;
    for (auto &plugin : plugins) {
        if (plugin == nullptr)
            continue;
        if (!plugin->parse(preState, loopEntry, cond, inc, body, loopInfo))
            return nullopt;
    }
    return loopInfo;
}

std::pair<std::string, unique_ptr<ProgramState>> emitLoopInvariant(
    const ProgramState &preState,
    const ProgramState &loopEntry,
    const clang::Expr *cond,
    const clang::Stmt *inc,
    const clang::Stmt *body,
    const LoopInfo &loopInfo,
    std::string_view groupName,
    std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds) {
    auto plugins = getPlugins<LoopInvariantPlugin>(groupName, extraPluginIds);
    vector<unique_ptr<Path>> invariants;
    string spec = ACSL_HEAD.to_string();

    auto substituteVariables = [&](auto f, not_null<unique_ptr<SymbolicExpr>> &expr,
                                   const Path &loopEntryPath) -> void {
        auto &mem = loopEntryPath.getMemoryState();
        switch (expr->getType()) {
            using enum SymbolicExpr::ExprType;
            case Literal: return;
            case Variable: {
                auto var = dynamic_cast<const Symbolic::Variable *>(expr.get().get());
                if (var == nullptr)
                    UNREACHABLE();
                if (auto addrPtr =
                        get_if<not_null<std::unique_ptr<const Address>>>(&var->getFrom())) {
                    auto &addr = **addrPtr;
                    if (auto value = mem.read(addr)) {
                        expr = value.value()->clone();
                    } else {
                        // This variable may originate from an address on this path (at loop entry)
                        // that has not yet been accessed; retain this variable without substitution.
                        return;
                    }
                } else {
                    TODO();
                }
                return;
            }
            case SymbolAddress: {
                auto symbolAddr = dynamic_cast<const Symbolic::Address *>(expr.get().get());
                if (symbolAddr == nullptr)
                    UNREACHABLE();
                std::visit(
                    [&](auto &&arg) -> void {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            TODO();
                        } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                            ERROR("This is a variable's address and should not appear in an "
                                  "expression.");
                        } else if constexpr (std::is_same_v<
                                                 T, not_null<std::unique_ptr<const Address>>>) {
                            if (auto value = mem.read(*arg)) {
                                expr = value.value()->clone();
                            } else {
                                // This address may originate from an address on this path (at loop
                                // entry) that has not yet been accessed; retain this variable
                                // without substitution.
                                return;
                            }
                        } else if constexpr (std::is_same_v<T, std::pair<not_null<std::shared_ptr<
                                                                             const Structure::Info>>,
                                                                         const size_t>>) {
                            TODO();
                        }
                    },
                    symbolAddr->getFrom());
                return;
            }
            case BinaryOp: {
                // note: non-const!
                auto bin = dynamic_cast<Symbolic::BinaryOpExpr *>(expr.get().get());
                if (bin == nullptr)
                    UNREACHABLE();
                f(f, bin->getLeft(), loopEntryPath);
                f(f, bin->getRight(), loopEntryPath);
                return;
            }
            case UnaryOp: {
                // note: non-const!
                auto un = dynamic_cast<Symbolic::UnaryOpExpr *>(expr.get().get());
                if (un == nullptr)
                    UNREACHABLE();
                f(f, un->getSub(), loopEntryPath);
                return;
            }
            case Structure: TODO();
            case Unknown: return;
            default: UNREACHABLE();
        }
        UNREACHABLE();
    }; // substituteVariables

    auto getSubstitutedAddr = [&](const Address &addr,
                                  const Path &loopEntryPath) -> not_null<unique_ptr<Address>> {
        if (addr.getDimension() == 0)
            return make_unique<Address>(addr);
        auto &mem = loopEntryPath.getMemoryState();
        return std::visit(
            [&](auto &&arg) -> not_null<unique_ptr<Address>> {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    TODO();
                } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                    UNREACHABLE();
                } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                    if (auto value = mem.read(*arg)) {
                        auto realAddr = value.value()->tryEvalAsOffsetedAddr();
                        if (realAddr == nullopt)
                            ERROR("This expr should be a address");
                        if (addr.isOffseted()) {
                            realAddr.value()->addOffset(addr.getOffset()->clone());
                            if (addr.isRange())
                                realAddr.value()->setLength(addr.getLength()->clone());
                        }
                        return std::move(realAddr).value();
                    } else {
                        // This address may originate from an address on this path (at loop
                        // entry) that has not yet been accessed; retain this address
                        // without substitution.
                        return make_unique<Address>(addr);
                    }
                } else if constexpr (std::is_same_v<
                                         T,
                                         std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                   const size_t>>) {
                    TODO();
                }
            },
            addr.getFrom());
    }; // getSubstitutedAddr

    auto postState   = preState.clone();
    auto &postPaths  = postState->getPaths();
    auto pathNum     = postPaths.size();
    auto resultInfos = vector<vector<PostInfo>>{pathNum};

    // Update the resultInfos with a plugin's postInfo.
    auto updateResultInfos = [&](vector<PostInfo> &infos, bool substituteAddr,
                                 bool substituteExpr) {
        if (infos.empty())
            return;
        if (preState.getPaths().size() != loopEntry.getPaths().size()) {
            ERROR("Branch is unsupported here.");
        }

        auto &entryPaths = loopEntry.getPaths();

        for (auto i : views::iota(size_t{0}, pathNum)) {
            auto &entryPath         = *entryPaths.at(i);
            auto &postBranchesInfos = resultInfos.at(i);

            if (infos.size() != 1)
                TODO();
            if (postBranchesInfos.empty())
                postBranchesInfos.emplace_back();
            auto &info           = infos.at(0);
            auto &postBranchInfo = postBranchesInfos.at(0);

            for (auto &[addr, value] : info.memoryMap_) {
                auto subedAddr = substituteAddr
                                     ? getSubstitutedAddr(addr, entryPath).into_underlying()
                                     : make_unique<Address>(addr);
                if (substituteExpr)
                    substituteVariables(substituteVariables, value, entryPath);
                if (auto it = postBranchInfo.memoryMap_.find(*subedAddr);
                    it != postBranchInfo.memoryMap_.end() && !it->second->isUnknown()) {
                    WARN("Another plugin has already updated this address. The new value: {" +
                         value->simplifiedExpr()->regularForm() + "} is discarded.");
                    continue;
                }
                postBranchInfo.memoryMap_.insert_or_assign(*subedAddr, std::move(value));
            }

            for (auto &cond : info.pathConds_) {
                postBranchInfo.pathConds_.push_back(std::move(cond));
            }
        }
    }; // updatePostState

    for (auto &plugin : plugins) {
        if (plugin == nullptr)
            UNREACHABLE();
        auto [s, continueFlag, postInfos] =
            plugin->generate(preState, loopEntry, cond, inc, body, loopInfo);

        if (s) {
            spec += "    " /*4 spaces*/ + *s + "\n";
        }

        updateResultInfos(postInfos, plugin->needSubstituteAddress(), plugin->needSubstituteExpr());

        if (!continueFlag)
            break;
    }
    spec += ACSL_END.to_string();

    // Build post-state from result infos.
    for (auto i : views::iota(size_t{0}, pathNum)) {
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
            if (!postPath->getVarAddr().contains(addr.getFromRoot()))
                continue;
            postPath->updateMemory(addr, std::move(value));
        }
        for (auto &pathCond : postBranchInfo.pathConds_) {
            postPath->insertPathCondition(std::move(pathCond));
        }
    }

    return pair{spec, std::move(postState)};
}