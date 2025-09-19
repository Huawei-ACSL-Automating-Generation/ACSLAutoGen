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
            spec += *s;
    }
    spec += ACSL_END.to_string();
    return spec;
}

pair<LoopInfo, bool> parseLoopInfo(
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
            UNREACHABLE();
        if (!plugin->parse(preState, loopEntry, cond, inc, body, loopInfo))
            return pair{std::move(loopInfo), false};
    }
    return pair{std::move(loopInfo), true};
}

void parseComplexLoopInfo(const ProgramState &preState,
                          const ProgramState &loopEntry,
                          const clang::Expr *cond,
                          const clang::Stmt *inc,
                          const clang::Stmt *body,
                          LoopInfo &loopInfo,
                          std::string_view groupName,
                          optional<reference_wrapper<const vector<string>>> extraPluginIds) {
    auto plugins = getPlugins<LoopInfoPlugin>(groupName, extraPluginIds);

    for (auto &plugin : plugins) {
        if (plugin == nullptr)
            UNREACHABLE();
        if (!plugin->parse(preState, loopEntry, cond, inc, body, loopInfo))
            ERROR("Plugin: {" + string{plugin->id()} +
                  "} parsing complex loop's information failed.");
    }
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

    auto postState   = preState.clone();
    auto &postPaths  = postState->getPaths();
    auto pathNum     = postPaths.size();
    auto resultInfos = vector<vector<PostInfo>>{pathNum};

    // Update the resultInfos with a plugin's postInfo.
    auto updateResultInfos = [&](vector<PostInfo> &infos) {
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
                auto subedAddr = getSubstitutedAddr(addr, entryPath);
                substituteSymbols(value, entryPath);
                if (auto it = postBranchInfo.memoryMap_.find(*subedAddr);
                    it != postBranchInfo.memoryMap_.end() && !it->second->isUnknown()) {
                    auto regForm = value->simplifiedExpr()->regularForm();
                    WARN("Another plugin has already updated this address. The new value: {" +
                         (regForm ? regForm.value() : value->dump()) + "} is discarded.");
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

        updateResultInfos(postInfos);

        if (!continueFlag)
            break;
    }
    spec += ACSL_END.to_string();

    // Build post-state from result infos.
    for (auto i : views::iota(size_t{0}, pathNum)) {
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
            if (root == nullopt)
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

    return pair{spec, std::move(postState)};
}

void substituteSymbols(not_null<unique_ptr<SymbolicExpr>> &expr, const Path &loopEntryPath) {
    auto &mem = loopEntryPath.getMemoryState();
    switch (expr->getType()) {
        using enum SymbolicExpr::ExprType;
        case Literal: return;
        case Variable: {
            auto var = dynamic_cast<const Symbolic::Variable *>(expr.get().get());
            std::visit(
                [&](auto &&arg) -> void {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        TODO();
                    } else if constexpr (std::is_same_v<
                                             T, not_null<std::unique_ptr<const Symbolic::Address>>>) {
                        if (auto value = mem.read(*arg)) {
                            expr = value.value()->clone();
                        } else {
                            // This address may originate from an address on this path (at loop
                            // entry) that has not yet been accessed; retain this variable
                            // without substitution.
                            return;
                        }
                    }
                },
                var->getFrom());
            return;
        }
        case Address: {
            auto symbolAddr = dynamic_cast<const Symbolic::SymbolAddress *>(expr.get().get());
            if (symbolAddr == nullptr)
                UNREACHABLE();
            std::visit(
                [&](auto &&arg) -> void {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        TODO();
                    } else if constexpr (std::is_same_v<
                                             T, not_null<std::unique_ptr<const Symbolic::Address>>>) {
                        if (auto value = mem.read(*arg)) {
                            expr = value.value()->clone();
                        } else {
                            // This address may originate from an address on this path (at loop
                            // entry) that has not yet been accessed; retain this variable
                            // without substitution.
                            return;
                        }
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
            substituteSymbols(bin->getLeft(), loopEntryPath);
            substituteSymbols(bin->getRight(), loopEntryPath);
            return;
        }
        case UnaryOp: {
            // note: non-const!
            auto un = dynamic_cast<Symbolic::UnaryOpExpr *>(expr.get().get());
            if (un == nullptr)
                UNREACHABLE();
            substituteSymbols(un->getSub(), loopEntryPath);
            return;
        }
        case Structure: TODO();
        case Unknown: return;
        default: UNREACHABLE();
    }
    UNREACHABLE();
};

not_null<unique_ptr<Address>> getSubstitutedAddr(const Address &addr, const Path &loopEntryPath) {
    if (addr.getAddressType() != Address::AddressType::SymbolAddr)
        return addr.addressClone();
    auto &symbolAddr = dynamic_cast<const SymbolAddress &>(addr);
    auto &mem        = loopEntryPath.getMemoryState();
    return std::visit(
        [&](auto &&arg) -> not_null<unique_ptr<Address>> {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                TODO();
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                if (auto value = mem.read(*arg)) {
                    auto realAddr = value.value()->tryEvalAsOffsetedAddr();
                    if (realAddr == nullopt)
                        ERROR("This expr should be a address");
                    auto offset = symbolAddr.getOffset()->clone();
                    substituteSymbols(offset, loopEntryPath);

                    realAddr.value()->addOffset(offset->simplifiedExpr());
                    if (symbolAddr.isRange()) {
                        auto length = symbolAddr.getLength()->clone();
                        substituteSymbols(length, loopEntryPath);
                        realAddr.value()->setLength(length->simplifiedExpr());
                    }
                    return std::move(realAddr).value().into_underlying();
                } else {
                    // This address may originate from an address on this path (at loop
                    // entry) that has not yet been accessed; retain this address
                    // without substitution.
                    return symbolAddr.addressClone();
                }
            }
        },
        symbolAddr.getFrom());
};