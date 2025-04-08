#include "analysis.h"
#include "macros.h"
#include "function.h"
#include "clang/AST/Stmt.h"
#include "utils/utils.h"

using namespace clang;
using namespace llvm;
using namespace std;
void ACSLAnalyzer::analyzeFunctions()
{
    PROCESS("Running analysis functions...");
    for (auto *func : this->Context.getFunctions())
    {
        auto wrappedFunc = make_unique<ACSLFunction>(func);
        generateFunctionSpec(wrappedFunc.get());
        Functions.push_back(std::move(wrappedFunc));
    }
}

void ACSLAnalyzer::generateFunctionSpec(ACSLFunction *func)
{
    const FunctionDecl *FD = func->getFunctionDecl();
    INFO("Processing Function " + FD->getNameAsString());

    if (const Stmt *Body = FD->getBody())
    {
        if (!isa<CompoundStmt>(Body))
            UNIMPLEMENT("Function body of " + FD->getNameAsString() + " is not a CompoundStmt");

        const CompoundStmt *CS = cast<CompoundStmt>(Body);
        for (const Stmt *child : CS->children())
        {
            if (isLoopStmt(child)) {}
            else {}
            TODO();
        }
    }
    else
    {
        INFO("No function body found for: " + FD->getNameAsString());
    }
}