#include "state.h"

SymbolicExpr *ProgramState::getVarState(clang::VarDecl *var)
{
    auto canonicalVar = var->getCanonicalDecl();
    auto it = stateMap.find(canonicalVar);
    if(it == stateMap.end())
    {
        // ERROR_MACRO("Variable state not found"); // TODO: define error macro
        return nullptr;
    }
    return it->second.get();
}
