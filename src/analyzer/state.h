#ifndef STATE_H
#define STATE_H

#include <unordered_map>
#include "symbolic.h"
#include "clang/AST/Decl.h"

class Path
{
  public:
    Path() = default;
    ~Path() = default;

    SymbolicExpr *getVarState(clang::VarDecl *var);
    const std::vector<std::unique_ptr<SymbolicExpr>> &getPathConditions() const;

    void insertVarState(clang::VarDecl *var, std::unique_ptr<SymbolicExpr> expr);
    void insertPathCondition(std::unique_ptr<SymbolicExpr> cond);

  private:
    // Map: variable record definition ID -> corresponding symbolic expression.
    std::unordered_map<clang::VarDecl *, std::unique_ptr<SymbolicExpr>> stateMap;

    // SET: List of symbolic expressions representing the path condition.
    std::vector<std::unique_ptr<SymbolicExpr>> pathConditions;
};

class ProgramState
{
  public:
    ProgramState();
    ~ProgramState() = default;

  private:
    std::vector<std::unique_ptr<Path>> paths;
};
#endif
