#ifndef UTILS_H
#define UTILS_H

#include <clang/AST/Expr.h>
#include "analyzer/symbolic.h"

// bool isLoopStmt(const clang::Stmt *stmt);
std::unique_ptr<SymbolicExpr> createLNotExpr(std::unique_ptr<SymbolicExpr> expr);

BinaryOpExpr::Operator getCompoundAssignOp(clang::BinaryOperatorKind compoundAssignOp);

bool isAssignOp(const clang::BinaryOperator *binOp);

bool ignoreTopBinop(const clang::BinaryOperator *binOp);
#endif // UTILS_H
