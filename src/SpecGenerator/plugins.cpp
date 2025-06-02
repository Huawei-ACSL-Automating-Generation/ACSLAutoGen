// src/SpecGenerator/plugins.cpp

#include <unordered_set>
#include "specGenerator.h"
#include "stringTemplate.h"
#include "macros.h"
#include "state.h"
#include "utils.h"

using namespace std;
using namespace clang;

class AssignsPlugin : public FunctionContractPlugin
{
  public:
    AssignsPlugin(const string &ID) : id_(ID) {}
    string id() const override { return id_; }
    optional<string> generate(const ProgramState &pre, const ProgramState &post) const override
    {
        typedef pair<const clang::VarDecl *const, optional<string>> AssignedAddr;

        string spec;
        unordered_set<AssignedAddr, acslg::pair_hash> assignedMap;
        // Function's pre-state should have exactly one path.
        if (auto &paths = pre.getPaths(); paths.size() == 1)
        {
            auto &prePath = paths[0];

            auto isFromPointerParam = [&](const Address &addr) {
                auto &varAddrMap = prePath->getVarAddr();
                if (auto it = varAddrMap.find(addr.getVarDecl());
                    it == varAddrMap.end() /* from local parameter */ ||
                    *(it->second) == addr /* function parameter's address */)
                    return false;
                else
                    return true;
            };
            // For every post-state path
            for (auto &postPath : post.getPaths())
            {
                // and every Address in the path's memoryState.
                for (auto &[addr, _] : postPath->getMemoryState())
                {
                    if (!addr.hasVarDecl() /* Just for safety */
                        || !isFromPointerParam(addr) || postPath->isUnchangedState(addr))
                        continue;

                    // TODO(Multiple pointer): only support one dimension now.
                    if (!addr.isOffseted())
                    {
                        assignedMap.insert(AssignedAddr{addr.getVarDecl(), nullopt});
                        continue;
                    }

                    auto offset = addr.getOffset();
                    assignedMap.insert(AssignedAddr{addr.getVarDecl(),
                        offset == nullptr ? optional<string>{nullopt}
                                          : optional<string>{offset->regularForm()}});
                }
            }
        }

        for (auto &[var, offset] : assignedMap)
        {
            auto varName = var->getNameAsString();
            if (offset)
                spec += "*(" + varName + "+" + *offset + ")";
            else
                spec += "*" + varName;
            spec += ", ";
        }

        if (spec.empty())
            return R"(assigns \nothing;)";
        else
            return "assigns " + spec.substr(0, spec.length() - 2) + ";";
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(AssignsPlugin, "assigns");

class ResultPlugin : public FunctionContractPlugin
{
  public:
    ResultPlugin(const string &ID) : id_(ID) {}
    string id() const override { return id_; }
    optional<string> generate(const ProgramState &, const ProgramState &post) const override
    {
        // TODO
        return nullopt;
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(ResultPlugin, "result");
