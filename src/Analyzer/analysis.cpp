#include "analysis.h"
#include "macros.h"
#include "function.h"
#include "clang/AST/Stmt.h"
#include "state.h"
#include "SpecGenerator/specGenerator.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace llvm;
using namespace std;

namespace acslg::analyzer {
    void ACSLAnalyzer::analyzeFunctions() {
        PROCESS("Running analysis functions...");
        for (auto *func : this->context_.getFunctions()) {
            auto loc = func->getLocation();
            if (!context_.getSourceManager().isInMainFile(loc))
                continue;

            auto wrappedFunc = make_unique<ACSLFunction>(func);
            generateFunctionSpec(wrappedFunc.get());
            functions_.push_back(std::move(wrappedFunc));
        }
    }

    void ACSLAnalyzer::generateFunctionSpec(ACSLFunction *func) {
        const FunctionDecl *FD = func->getFunctionDecl();

        if (FD->getNameAsString() == "main") {
            WARN("Ignore MAIN Function.");
            return;
        }
        // @WindOctober: TODO remove.
        // if (FD->getNameAsString() != "IsLegalFlag" && FD->getNameAsString() != "BN_SetFlag" &&
        //     FD->getNameAsString() != "BN_IsZero")
        //     return;
        // INFO("Processing Function " + FD->getNameAsString());

        auto state = make_unique<ProgramState>(make_unique<ACSLFunction>(FD), context_);

        if (const Stmt *Body = FD->getBody()) {
            if (!isa<CompoundStmt>(Body))
                UNIMPLEMENT("Function body of " + FD->getNameAsString() + " is not a CompoundStmt");

            state->init();
            const CompoundStmt *CS = cast<CompoundStmt>(Body);
            auto preState          = state->clone();

            for (const Stmt *stmt : CS->children())
                state->step(stmt);
            INFO(state->dump());

            bool hasPointer = false, hasLoop = false;
            for (auto &&[_, value] : preState->getPaths()[0]->getMemoryState().flat()) {
                if (value->getType() == symbolic::SymbolicExpr::ExprType::Address)
                    hasPointer = true;
            }
            for (auto stmt : dyn_cast<CompoundStmt>(Body)->children()) {
                if (isa<ForStmt>(stmt) || isa<WhileStmt>(stmt) || isa<DoStmt>(stmt))
                    hasLoop = true;
            }

            if (hasPointer && hasLoop)
                return;

            auto spec = spec_generator::emitFunctionContract(*preState, *state);
            INFO(spec);

            auto &SM       = context_.getSourceManager();
            auto fileBegin = SM.getFileLoc(FD->getSourceRange().getBegin());
            context_.insertText(fileBegin, spec, /*after*/ false, /*indentNewLines*/ true);
        } else {
            INFO("No function body found for: " + FD->getNameAsString());
        }
    }
}