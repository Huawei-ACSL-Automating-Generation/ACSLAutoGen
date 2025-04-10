#ifndef UTILS_H
#define UTILS_H

#include "analyzer/symbolic.h"

// bool isLoopStmt(const clang::Stmt *stmt);
std::unique_ptr<SymbolicExpr> createLNotExpr(std::unique_ptr<SymbolicExpr> expr);
#endif // UTILS_H
