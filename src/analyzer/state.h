#ifndef STATE_H
#define STATE_H

#include <unordered_map>
#include "symbolic.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"

class Path
{
  public:
    Path() = default;
    ~Path() = default;

    enum class PathState
    {
        Step,
        Continue,
        Break,
        Return
    };

    SymbolicExpr *getVarState(const clang::VarDecl *var);
    const std::vector<std::unique_ptr<SymbolicExpr>> &getPathConditions() const;

    void insertVarState(const clang::VarDecl *var, const clang::Expr *expr);
    void insertPathCondition(const clang::Expr *cond);
    void insertDefaultPathConds(const std::vector<const clang::Expr *> &conds);
    void setReturnExpr(const clang::Expr *expr) { returnExpr = convertExpr(expr); };
    void setPathState(PathState state) { currentState = state; }

    bool isActive() const { return currentState == PathState::Step; }

    std::unique_ptr<Path> clone() const;

  private:
    std::unique_ptr<SymbolicExpr> convertExpr(const clang::Expr *expr);

    // Map: variable record definition ID -> corresponding symbolic expression.
    std::unordered_map<const clang::VarDecl *, std::unique_ptr<SymbolicExpr>> stateMap;

    // SET: List of symbolic expressions representing the path condition.
    std::vector<std::unique_ptr<SymbolicExpr>> pathConditions;

    // Holds the current path state. Default is set to Step
    PathState currentState = PathState::Step;

    std::unique_ptr<SymbolicExpr> returnExpr = std::make_unique<NullExpr>();
};

class ProgramState
{
  public:
    ProgramState();
    ~ProgramState() = default;

    void init(const clang::FunctionDecl *FD);

    void step(const clang::Stmt *stmt);
    void stepBranch(const std::vector<const clang::Expr *> &branchConds,
        const std::vector<const clang::Stmt *> &branchStmts);

    void addNewDecls(const std::vector<const clang::VarDecl *> &varDecls);

    void setStates(Path::PathState state);
    void setReturnExpr(const clang::Expr *expr);

    std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<ProgramState>>
    splitActiveInactive() const;
    std::unique_ptr<ProgramState> Merge(const std::vector<const ProgramState *> &states);
    std::unique_ptr<ProgramState> clone() const;

  private:
    std::vector<std::unique_ptr<Path>> paths;
};
#endif
