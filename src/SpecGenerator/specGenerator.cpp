// src/SpecGenerator/specGenerators.cpp

#include "specGenerator.h"
#include "utilityTemplates.h"
#include "macros.h"
#include "state.h"

using namespace std;

string emitFunctionContract(const ProgramState &pre,
    const ProgramState &post,
    const string &groupName,
    optional<reference_wrapper<const vector<string>>> extraPluginIds)
{
    const ACSLPluginGroup *group = ACSLPluginGroupRegistry::instance().getGroup(groupName);
    if (!group)
    {
        auto names = ACSLPluginGroupRegistry::instance().allGroupNames();
        string allName;
        allName += "[";
        for (auto &name : names)
        {
            allName += name;
            allName += ", ";
        }
        if (allName.size() > 1)
        {
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
    string spec = ACSL_HEAD.to_string();
    for (auto &pid : ids)
    {
        auto *pl = ACSLPluginRegistry::instance().get(pid);
        if (!pl)
            continue;
        auto *fcp = dynamic_cast<FunctionContractPlugin *>(pl);
        if (fcp)
        {
            if (auto s = fcp->generate(pre, post); s)
                spec += "\t" + *s + "\n";
        }
    }
    spec += ACSL_END.to_string();
    return spec;
}