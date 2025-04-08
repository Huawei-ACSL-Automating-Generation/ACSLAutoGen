#ifndef UTILS_H
#define UTILS_H

#include "analyzer/symbolic.h"
#include "clang/AST/Stmt.h"

bool isLoopStmt(const clang::Stmt *stmt);

std::unique_ptr<SymbolicExpr> convertExpr(const clang::Expr *expr);

#endif // UTILS_H
