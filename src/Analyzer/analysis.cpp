#include "analysis.h"
#include "macros.h"
#include "function.h"
#include "clang/AST/Stmt.h"
#include "state.h"
#include "Context/globalSM.h"
#include "SpecGenerator/specGenerator.h"
#include "clang/Rewrite/Core/Rewriter.h"

using namespace clang;
using namespace llvm;
using namespace std;

void ACSLAnalyzer::analyzeFunctions() {
    PROCESS("Running analysis functions...");
    for (auto *func : this->Context.getFunctions()) {
        auto loc = func->getLocation();
        if (!GlobalSM::getSM().isInMainFile(loc))
            continue;

        auto wrappedFunc = make_unique<ACSLFunction>(func);
        generateFunctionSpec(wrappedFunc.get());
        Functions.push_back(std::move(wrappedFunc));
    }
}

void ACSLAnalyzer::generateFunctionSpec(ACSLFunction *func) {
    const FunctionDecl *FD = func->getFunctionDecl();

    if (FD->getNameAsString() == "main") {
        WARN("Ignore MAIN Function.");
        return;
    }
    // @WindOctober: TODO remove.
    // if (FD->getNameAsString() != "BN_SetFlag")
    //     return;
    INFO("Processing Function " + FD->getNameAsString());

    auto state = std::make_unique<ProgramState>(make_unique<ACSLFunction>(FD));

    if (const Stmt *Body = FD->getBody()) {
        if (!isa<CompoundStmt>(Body))
            UNIMPLEMENT("Function body of " + FD->getNameAsString() + " is not a CompoundStmt");

        state->init();
        const CompoundStmt *CS = cast<CompoundStmt>(Body);
        auto preState          = state->clone();

        for (const Stmt *stmt : CS->children())
            state->step(stmt);
        // INFO(state->dump());

        bool hasPointer = false, hasLoop = false;
        for (auto &&[_, value] : preState->getPaths()[0]->getMemoryState().flat()) {
            if (value->getType() == SymbolicExpr::ExprType::SymbolAddress)
                hasPointer = true;
        }
        for (auto stmt : dyn_cast<CompoundStmt>(Body)->children()) {
            if (isa<ForStmt>(stmt) || isa<WhileStmt>(stmt) || isa<DoStmt>(stmt))
                hasLoop = true;
        }

        if (hasPointer && hasLoop)
            return;

        auto spec = emitFunctionContract(*preState, *state);
        INFO(spec);

        auto beginLoc = FD->getSourceRange().getBegin();
        GlobalSM::InsertText(beginLoc, spec, /*after*/ false,
                             /*indentNewLines*/ true);
    } else {
        INFO("No function body found for: " + FD->getNameAsString());
    }
}