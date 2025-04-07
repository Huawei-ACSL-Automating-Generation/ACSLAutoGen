#ifndef STATE_H
#define STATE_H

#include <unordered_map>
#include "symbolic.h"
#include "clang/AST/Decl.h"
class ProgramState
{
  public:
    ProgramState() = default;
    ~ProgramState() = default;

    SymbolicExpr *getVarState(clang::VarDecl *var);

  private:
    // Map: variable record definition ID -> corresponding symbolic expression.
    std::unordered_map<clang::VarDecl *, std::unique_ptr<SymbolicExpr>> stateMap;
};

#endif
