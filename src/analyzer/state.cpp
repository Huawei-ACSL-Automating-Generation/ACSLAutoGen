#include "state.h"
#include "macros.h"

using namespace std;
using namespace clang;
SymbolicExpr *Path::getVarState(VarDecl *var)
{
    auto canonicalVar = var->getCanonicalDecl();
    auto it = stateMap.find(canonicalVar);
    if (it == stateMap.end())
        ERROR("Variable state not found");

    return it->second.get();
}

const vector<unique_ptr<SymbolicExpr>> &Path::getPathConditions() const { return pathConditions; }

void Path::insertVarState(VarDecl *var, unique_ptr<SymbolicExpr> expr)
{
    auto canonicalVar = var->getCanonicalDecl();
    auto it = stateMap.find(canonicalVar);
    if (it != stateMap.end())
        it->second = std::move(expr);
    else
        stateMap.insert({canonicalVar, std::move(expr)});
}

void Path::insertPathCondition(unique_ptr<SymbolicExpr> cond)
{
    pathConditions.push_back(std::move(cond));
}

ProgramState::ProgramState() { paths.push_back(std::make_unique<Path>()); }
