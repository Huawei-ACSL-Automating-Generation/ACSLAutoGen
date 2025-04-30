#include "analysis.h"
#include "macros.h"
#include "function.h"
#include "clang/AST/Stmt.h"
#include "state.h"
#include "context/globalSM.h"
#include "specGenerator/specGenerator.h"

using namespace clang;
using namespace llvm;
using namespace std;
void ACSLAnalyzer::analyzeFunctions()
{
    PROCESS("Running analysis functions...");
    for (auto *func : this->Context.getFunctions())
    {
        auto loc = func->getLocation();
        if (!GlobalSM::getSM().isInMainFile(loc))
            continue;

        auto wrappedFunc = make_unique<ACSLFunction>(func);
        generateFunctionSpec(wrappedFunc.get());
        Functions.push_back(std::move(wrappedFunc));
    }
}

void ACSLAnalyzer::generateFunctionSpec(ACSLFunction *func)
{
    const FunctionDecl *FD = func->getFunctionDecl();

    if (FD->getNameAsString() == "main")
    {
        WARN("Ignore MAIN Function.");
        return;
    }
    INFO("Processing Function " + FD->getNameAsString());

    auto acslFunc = new ACSLFunction(FD);
    auto state = std::make_unique<ProgramState>(acslFunc);

    if (const Stmt *Body = FD->getBody())
    {
        if (!isa<CompoundStmt>(Body))
            UNIMPLEMENT("Function body of " + FD->getNameAsString() + " is not a CompoundStmt");

        state->init();
        auto preState = state->clone();
        const CompoundStmt *CS = cast<CompoundStmt>(Body);
        for (const Stmt *stmt : CS->children())
            state->step(stmt);
        INFO(state->dump());
        // state->generateFuncACSL();
        auto spec = emitFunctionContract(*preState, *state, "DefaultFunctionContract");
        INFO(spec);
        // TODO
    }
    else
    {
        INFO("No function body found for: " + FD->getNameAsString());
    }
}