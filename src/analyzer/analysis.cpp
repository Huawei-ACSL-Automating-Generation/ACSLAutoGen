#include "analysis.h"
#include "macros.h"
#include "function.h"
#include "clang/AST/Stmt.h"
#include "utils/utils.h"
#include "state.h"

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

    auto state = make_unique<ProgramState>();

    if (const Stmt *Body = FD->getBody())
    {
        if (!isa<CompoundStmt>(Body))
            UNIMPLEMENT("Function body of " + FD->getNameAsString() + " is not a CompoundStmt");

        const CompoundStmt *CS = cast<CompoundStmt>(Body);
        for (const Stmt *stmt : CS->children())
            state->step(stmt);
    }
    else
    {
        INFO("No function body found for: " + FD->getNameAsString());
    }
}