#include "analysis.h"
#include "macros.h"
#include "function.h"
#include "clang/AST/Stmt.h"
#include "state.h"
#include "SpecGenerator/specGenerator.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Basic/SourceManager.h"

namespace acslg::analyzer {
    void ACSLAnalyzer::analyzeFunctions() {
        PROCESS("Running analysis functions...");
        for (auto *func : this->context_.getFunctions()) {
            auto loc = func->getLocation();
            if (!context_.getSourceManager().isInMainFile(loc))
                continue;

            auto wrappedFunc = std::make_unique<ACSLFunction>(func);
            generateFunctionSpec(wrappedFunc.get());
            functions_.push_back(std::move(wrappedFunc));
        }
    }

    void ACSLAnalyzer::generateFunctionSpec(ACSLFunction *func) {
        const clang::FunctionDecl *FD = func->getFunctionDecl();

        if (FD->getNameAsString() == "main") {
            WARN("Ignore MAIN Function.");
            return;
        }
        // @WindOctober: TODO remove.
        // if (FD->getNameAsString() != "IsLegalFlag" && FD->getNameAsString() != "BN_SetFlag" &&
        //     FD->getNameAsString() != "BN_IsZero")
        //     return;
        INFO("Processing Function " + FD->getNameAsString());

        auto state = std::make_unique<ProgramState>(std::make_unique<ACSLFunction>(FD), context_);

        if (const clang::Stmt *Body = FD->getBody()) {
            if (!isa<clang::CompoundStmt>(Body))
                UNIMPLEMENT("Function body of " + FD->getNameAsString() +
                            " is not a clang::CompoundStmt");

            state->init();
            const clang::CompoundStmt *CS = cast<clang::CompoundStmt>(Body);
            auto preState                 = state->clone();

            for (const clang::Stmt *stmt : CS->children())
                state->step(stmt);
            INFO(state->dump());

            bool hasPointer = false, hasLoop = false;
            for (auto &&[_, value] : preState->getPaths()[0]->getMemoryState().flat()) {
                if (llvm::isa<symbolic::Address>(*value))
                    hasPointer = true;
            }
            for (auto stmt : dyn_cast<clang::CompoundStmt>(Body)->children()) {
                if (isa<clang::ForStmt>(stmt) || isa<clang::WhileStmt>(stmt) ||
                    isa<clang::DoStmt>(stmt))
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
} // namespace acslg::analyzer