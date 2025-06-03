// src/SpecGenerator/looopInfoPlugins.cpp

#include <unordered_set>
#include "specGenerator.h"
#include "stringTemplate.h"
#include "macros.h"
#include "state.h"

using namespace std;
using namespace clang;

class VariablePatternPlugin : public LoopInfoPlugin
{
  public:
    string id() const override { return id_; }
    bool parse(const ProgramState &pre,
        const Stmt *init,
        const Expr *cond,
        const Stmt *inc,
        const Stmt *body,
        LoopInfo &loopInfo) const override
    {
        return true;
        // auto preState = pre.clone();
        // if (init)
        //     preState->step(init);

        // typedef LoopInfo::VarPattern VarPattern;
        // vector<VarPattern> varPatterns;

        // // has
    }

  private:
    string id_;
};