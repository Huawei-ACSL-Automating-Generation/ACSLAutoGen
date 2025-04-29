// src/specGenerator/specGenerators.cpp

#include "specGenerator.h"
#include "utilityTemplates.h"
#include "macros.h"

using namespace std;

string emitFunctionContract(const ProgramState &pre,
    const ProgramState &post,
    const string &groupName,
    const vector<string> &extraPluginIds)
{
    const ACSLPluginGroup *group = ACSLPluginGroupRegistry::instance().getGroup(groupName);
    if (!group)
        ERROR("Unknown ACSL group: " + groupName);

    vector<string> ids = group->pluginIds;
    ids.insert(ids.end(), extraPluginIds.begin(), extraPluginIds.end());

    string spec = ACSL_HEAD.to_string();
    for (auto &pid : ids)
    {
        auto *pl = ACSLPluginRegistry::instance().get(pid);
        if (!pl)
            continue;
        auto *fcp = dynamic_cast<FunctionContractPlugin *>(pl);
        if (fcp)
        {
            spec += "\t" + fcp->generate(pre, post) + "\n";
        }
    }
    spec += ACSL_END.to_string();
    return spec;
}