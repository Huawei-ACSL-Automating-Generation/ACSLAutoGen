#ifndef STATE_H
#define STATE_H

#include <unordered_map>
#include <variant>
#include <clang/AST/Decl.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/Expr.h>
#include "Symbolic/expr.h"
#include "function.h"

using namespace Symbolic;
using LValueTarget = std::variant<const clang::VarDecl *, std::unique_ptr<Address>>;
using TransRel     = std::tuple<int, int, std::vector<std::unique_ptr<SymbolicExpr>>>;
using InitRel      = std::pair<int, std::vector<std::unique_ptr<SymbolicExpr>>>;

class Path
{
  public:
    using EvalResult =
        std::pair<std::vector<std::unique_ptr<Path>>, std::vector<std::unique_ptr<SymbolicExpr>>>;

    Path()  = default;
    ~Path() = default;

    enum class PathState
    {
        Step,
        Continue,
        Break,
        Return
    };

    void LoopSymbolize();

    LValueTarget extractLValue(const clang::Expr *lhs);
    std::unique_ptr<Address> extractAddress(const clang::Expr *lhs);

    std::unique_ptr<SymbolicExpr> getVarState(const clang::VarDecl *var);
    const std::vector<std::unique_ptr<SymbolicExpr>> &getPathConditions() const;

    Address *allocMemory(const clang::VarDecl *);
    std::unique_ptr<Address> allocMemory(Address *from);

    void updateMemory(Address *addr, std::unique_ptr<SymbolicExpr> expr);
    void updateVarState(const clang::VarDecl *var, std::unique_ptr<SymbolicExpr> expr);
    void insertPathCondition(std::unique_ptr<SymbolicExpr> cond);

    void setReturnExpr(std::unique_ptr<SymbolicExpr> expr) { returnExpr = std::move(expr); };
    void setPathState(PathState state) { currentState = state; }

    bool isActive() const { return currentState == PathState::Step; }
    // bool isUnchangedState(Address addr);
    std::unique_ptr<Path> clone() const;

    const clang::Stmt *StmtCtx = nullptr;

    std::string dump() const;
    friend class ProgramState;

    auto getVarAddr() const -> const auto & { return varAddr; };
    auto getMemoryState() const -> const auto & { return memoryState; }
    int getNextSymVarId() { return symbolVarCounter++; }
    // auto getReturnExpr() const -> const auto & { return returnExpr; }
    // auto getPathState() const -> const auto & { return currentState; }
    // auto getAddrCounter() const -> const auto & { return addrCounter; }

  private:
    EvalResult evalExpr(const clang::Expr *expr);

    // Map: variable record definition ID -> corresponding symbolic address.
    std::unordered_map<const clang::VarDecl *, std::unique_ptr<Address>> varAddr;

    // Map: symbolic address -> value stored at that address, separating variable–address mapping
    // from address–value mapping.
    std::unordered_map<Address, std::unique_ptr<SymbolicExpr>, AddressHash, AddressEqual>
        memoryState;

    // SET: List of symbolic expressions representing the path condition.
    std::vector<std::unique_ptr<SymbolicExpr>> pathConditions;

    // Holds the current path state. Default is set to Step
    PathState currentState = PathState::Step;

    std::unique_ptr<SymbolicExpr> returnExpr = std::make_unique<NullExpr>();

    unsigned int addrCounter = 0;

    unsigned int symbolVarCounter = 0;
};

class ProgramState
{
  public:
    ProgramState(std::unique_ptr<Path> initialPath, ACSLFunction *context);
    ProgramState(ACSLFunction *context);
    ~ProgramState() = default;

    void init();

    void step(const clang::Stmt *stmt);
    std::vector<std::unique_ptr<SymbolicExpr>> stepExpr(const clang::Expr *expr);

    void addNewDecls(const std::vector<const clang::VarDecl *> &varDecls);

    void setStates(Path::PathState state, const clang::Stmt *stmt);

    void setReturnExpr(const clang::Expr *expr);
    void updateVarState(const clang::BinaryOperator *binOp);

    std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<ProgramState>> splitActiveInactive();
    static std::unique_ptr<ProgramState> merge(const std::vector<const ProgramState *> &states);
    static std::unique_ptr<ProgramState>
    merge(const std::vector<std::unique_ptr<ProgramState>> &states);
    std::unique_ptr<ProgramState> clone() const;
    std::unique_ptr<ProgramState>
    cloneWithPaths(std::vector<std::unique_ptr<Path>> &newPaths) const;
    bool isInactive() const;

    const clang::Stmt *StmtCtx = nullptr;

    std::string dump() const;
    void generateFuncACSL();
    void resetState();

    auto getPaths() const -> const auto & { return paths; };
    auto getContext() const -> const auto & { return Context; }

  private:
    std::vector<std::unique_ptr<Path>> paths{};

    std::unique_ptr<ACSLFunction> Context;

    // Only be used in step when processing SwitchStmt, just for a cleaner code.
    std::vector<std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<SymbolicExpr>>>
    splitStateBySwitchCond(const clang::Expr *switchCond);
    void stepSimpleSwitch(const clang::SwitchStmt *switchstmt);

    void stepBranch(const std::vector<const clang::Expr *> &branchConds,
        const std::vector<const clang::Stmt *> &branchStmts);

    void stepLoop(const clang::Stmt *loopStmt);

    void CollectLoopACSL();
};

void computeLinearInv(const std::vector<std::string> &locations,
    const std::vector<TransRel> &transitions,
    const InitRel &initial);

#endif
