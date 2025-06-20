// src/SpecGenerator/specGenerators.cpp

#include "specGenerator.h"
#include "utilityTemplates.h"
#include "macros.h"
#include "state.h"

using namespace std;
using namespace clang;

namespace {
    // auxiliary function
    template <typename T>
    vector<const T *> getPlugins(const string &groupName,
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
            ERROR("Unknown ACSL group: " + groupName + ". All registered groups: " + allName);
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
                            const string &groupName,
                            optional<reference_wrapper<const vector<string>>> extraPluginIds) {
    auto plugins = getPlugins<FunctionContractPlugin>(groupName, extraPluginIds);
    string spec  = ACSL_HEAD.to_string();

    for (auto &plugin : plugins) {
        if (plugin == nullptr)
            continue;
        if (auto s = plugin->generate(pre, post); s)
            spec += "\t" + *s + "\n";
    }
    spec += ACSL_END.to_string();
    return spec;
}

std::optional<LoopInfo> parseLoopInfo(
    const ProgramState &loopEntry,
    const clang::Expr *cond,
    const clang::Stmt *inc,
    const clang::Stmt *body,
    const string &groupName,
    optional<reference_wrapper<const vector<string>>> extraPluginIds) {
    auto plugins = getPlugins<LoopInfoPlugin>(groupName, extraPluginIds);

    LoopInfo loopInfo;
    for (auto &plugin : plugins) {
        if (plugin == nullptr)
            continue;
        if (!plugin->parse(loopEntry, cond, inc, body, loopInfo))
            return nullopt;
    }
    return loopInfo;
}

std::tuple<std::string, std::vector<Formulas>> emitLoopInvariant(
    const ProgramState &loopEntry,
    const clang::Expr *cond,
    const clang::Stmt *inc,
    const clang::Stmt *body,
    const LoopInfo &loopInfo,
    const std::string &groupName,
    std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds) {
    auto plugins = getPlugins<LoopInvariantPlugin>(groupName, extraPluginIds);
    vector<Formulas> invariants;
    string spec = ACSL_HEAD.to_string();

    // TODO: split invariants derived by each plugin [now only stingx-plugin can generate invariant.]
    for (auto &plugin : plugins) {
        if (plugin == nullptr)
            UNREACHABLE();
        auto [s, continueFlag, exit_invs] = plugin->generate(loopEntry, cond, inc, body, loopInfo);

        invariants = std::move(exit_invs);
        if (s) {
            spec += "\t" + *s + "\n";
        }
        if (!continueFlag)
            break;
    }
    spec += ACSL_END.to_string();
    return make_tuple(spec, std::move(invariants));
}