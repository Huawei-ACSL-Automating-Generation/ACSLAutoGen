#ifndef STATE_H
#define STATE_H

#include <unordered_map>
#include "symbolic.h"
#include "function.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Expr.h"

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

    void LoopInit(std::unordered_map<Variable *, std::unique_ptr<SymbolicExpr>> &initMap);

    std::unique_ptr<SymbolicExpr> getVarState(const clang::VarDecl *var);
    const std::vector<std::unique_ptr<SymbolicExpr>> &getPathConditions() const;

    Address *allocMemory(const clang::VarDecl *);
    Address *allocMemory();

    void insertVarState(Address *addr, const clang::Expr *expr);
    void insertVarState(Address *addr, std::unique_ptr<SymbolicExpr> expr);
    void insertVarState(const clang::VarDecl *var, const clang::Expr *expr);
    void insertVarState(const clang::VarDecl *var, std::unique_ptr<SymbolicExpr> expr);

    void insertPathCondition(const clang::Expr *cond);

    // ProgramState has no ASTContext to construct new clang::Expr, so Expr must be passed to Path.
    // However, has param typed BinaryOpExpr::Operator is ugly, may modify it later.
    void insertPathCondition(
        const clang::Expr *LHS, const BinaryOpExpr::Operator op, const clang::Expr *RHS);
    void insertDefaultPathConds(const std::vector<const clang::Expr *> &conds);
    void setReturnExpr(const clang::Expr *expr) { returnExpr = convertExpr(expr); };
    void setPathState(PathState state) { currentState = state; }
    void updateVarState(const clang::BinaryOperator *binOp);

    bool isActive() const { return currentState == PathState::Step; }

    std::unique_ptr<Path> clone() const;

    const clang::Stmt *StmtCtx = nullptr;

    std::string dump() const;

  private:
    std::unique_ptr<SymbolicExpr> convertExpr(const clang::Expr *expr);

    // Map: variable record definition ID -> corresponding symbolic address.
    std::unordered_map<const clang::VarDecl *, std::unique_ptr<Address>> varAddr;

    // Map: symbolic address -> value stored at that address, separating variable–address mapping
    // from address–value mapping.
    std::unordered_map<Address, std::unique_ptr<SymbolicExpr>, AddressHash> memoryState;

    // SET: List of symbolic expressions representing the path condition.
    std::vector<std::unique_ptr<SymbolicExpr>> pathConditions;

    // Holds the current path state. Default is set to Step
    PathState currentState = PathState::Step;

    std::unique_ptr<SymbolicExpr> returnExpr = std::make_unique<NullExpr>();

    unsigned int addrCounter = 0;
};

class ProgramState
{
  public:
    ProgramState(std::unique_ptr<Path> initialPath, ACSLFunction *context);
    ProgramState(ACSLFunction *context);
    ~ProgramState() = default;

    void init();

    void step(const clang::Stmt *stmt);

    void addNewDecls(const std::vector<const clang::VarDecl *> &varDecls);

    void setStates(Path::PathState state, const clang::Stmt *stmt);

    void setReturnExpr(const clang::Expr *expr);
    void updateVarState(const clang::BinaryOperator *binOp);

    std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<ProgramState>> splitActiveInactive();
    std::unique_ptr<ProgramState> Merge(const std::vector<const ProgramState *> &states);
    std::unique_ptr<ProgramState> clone() const;

    const clang::Stmt *StmtCtx = nullptr;
    void ResetState();

  private:
    std::vector<std::unique_ptr<Path>> paths{};

    std::vector<const clang::VarDecl *> loopIndexes{};

    std::unique_ptr<ACSLFunction> Context;

    // Only be used in step when processing SwitchStmt, just for a cleaner code.
    void stepSimpleSwitch(const clang::SwitchStmt *switchstmt);

    void stepBranch(const std::vector<const clang::Expr *> &branchConds,
        const std::vector<const clang::Stmt *> &branchStmts);

    void stepLoop(const clang::Stmt *loopStmt);

    std::vector<const clang::VarDecl *> determineIndexVars(const clang::Stmt *init,
        const clang::Expr *cond,
        const clang::Stmt *body,
        const clang::Stmt *step);

    void CollectLoopACSL();
};
#endif
