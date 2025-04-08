#include "utils.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include <clang/AST/Expr.h>
#include "macros.h"

using namespace clang;
using namespace llvm;
using namespace std;
bool isLoopStmt(const Stmt *stmt)
{
    return isa<ForStmt>(stmt) || isa<WhileStmt>(stmt) || isa<DoStmt>(stmt) ||
           isa<CXXForRangeStmt>(stmt);
}

unique_ptr<SymbolicExpr> convertExpr(const Expr *expr)
{
    if (!expr)
        ERROR("Fail to convert a empty Expr");

    expr = expr->IgnoreParenImpCasts();

    UNIMPLEMENT("Unsupported Expr type: " << expr->getStmtClassName());
}