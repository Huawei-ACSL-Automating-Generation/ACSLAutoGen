#include "utils.h"

// using namespace clang;
// using namespace llvm;
using namespace std;
// bool isLoopStmt(const Stmt *stmt)
// {
//     return isa<ForStmt>(stmt) || isa<WhileStmt>(stmt) || isa<DoStmt>(stmt) ||
//            isa<CXXForRangeStmt>(stmt);
// }

unique_ptr<SymbolicExpr> createLNotExpr(unique_ptr<SymbolicExpr> expr)
{
    return make_unique<UnaryOpExpr>(UnaryOpExpr::Operator::LogicalNot, std::move(expr));
}
