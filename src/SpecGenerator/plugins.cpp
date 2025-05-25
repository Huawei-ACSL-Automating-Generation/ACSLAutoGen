// src/SpecGenerator/plugins.cpp

#include "specGenerator.h"
#include "stringTemplate.h"
#include "macros.h"
#include "state.h"

using namespace std;

class AssignPlugin : public FunctionContractPlugin
{
  public:
    AssignPlugin(const string &ID) : id_(ID) {}
    string id() const override { return id_; }
    optional<string> generate(const ProgramState &pre, const ProgramState &post) override
    {
        // Function's pre-state should have exactly one path.
        if (auto &paths = pre.getPaths(); paths.size() == 1)
        {
            auto &path = paths[0];
            unordered_map<clang::VarDecl const *, optional<uint>> isChangedFlag;
            // for every var in pre-state
            for (auto &[var, _] : path->getVarAddr())
            {
                // Is there any post-path that var's value is changed?
                for (auto &postPath : post.getPaths())
                {
                    auto preValue = path->getVarState(var), postValue = postPath->getVarState(var);
                    uint depth = 0; // represent how many *(deref) before var;
                    while (*preValue == *postValue &&
                           preValue->getType() == SymbolicExpr::ExprType::SymbolAddress)
                    {
                        ++depth;
                        preValue = path->getMemoryState()
                                       .at(*static_cast<Address *>(preValue.get()))
                                       ->clone();
                        postValue = postPath->getMemoryState()
                                        .at(*static_cast<Address *>(postValue.get()))
                                        ->clone();
                    }
                    if (*preValue != *postValue)
                        isChangedFlag[var] = depth;
                }
            }

            string spec     = "assigns ";
            bool haveAssign = false;
            for (auto [var, depth] : isChangedFlag)
            {
                if (!depth)
                    continue; // never happen

                if (haveAssign)
                    spec += ", ";
                haveAssign = true;
                for (uint i = 0; i < *depth; i++)
                    spec += "*";
                spec += var->getName();
            }
            if (!haveAssign)
                spec += R"(\nothing)";
            spec += ";";
            return spec;
        }
        else
        {
            ERROR("The number of paths to the function's pre-state isn't exactly one.");
        }
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(AssignPlugin, "assign");

class ResultPlugin : public FunctionContractPlugin
{
  public:
    ResultPlugin(const string &ID) : id_(ID) {}
    string id() const override { return id_; }
    optional<string> generate(const ProgramState &, const ProgramState &post) override
    {
        // TODO
        return nullopt;
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(ResultPlugin, "result");
