#include "utils.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"

using namespace clang;
using namespace llvm;
using namespace std;
bool isLoopStmt(const Stmt *stmt)
{
    return isa<ForStmt>(stmt) || isa<WhileStmt>(stmt) || isa<DoStmt>(stmt) ||
           isa<CXXForRangeStmt>(stmt);
}
