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

        // @WindOctober: TODO remove.
        // if (FD->getNameAsString() != "BN_Create" && FD->getNameAsString() != "BN_SetFlag" &&
        //     FD->getNameAsString() != "BN_IsZero")
        //     return;
        // INFO("Processing Function " + FD->getNameAsString());

        auto state = std::make_unique<ProgramState>(std::make_unique<ACSLFunction>(FD), context_);

        const clang::Stmt *body = FD->getBody();
        if (body == nullptr) {
            INFO("No function body found for: " + FD->getNameAsString());
            return;
        }
        if (!isa<clang::CompoundStmt>(body))
            UNIMPLEMENT("Function body of " + FD->getNameAsString() +
                        " is not a clang::CompoundStmt");

        state->init();
        const clang::CompoundStmt *CS = cast<clang::CompoundStmt>(body);
        auto preState                 = state->clone();

        for (const clang::Stmt *stmt : CS->children())
            state->step(stmt);
        INFO(state->dump());

        if (FD->getNameAsString() == "main") {
            WARN("Ignore the contracts generating of MAIN Function.");
            return;
        }

        auto [spec, usedPoints] = spec_generator::emitFunctionContract(*preState, *state);
        INFO(spec);

        auto &SM       = context_.getSourceManager();
        auto fileBegin = SM.getFileLoc(FD->getSourceRange().getBegin());
        context_.insertText(fileBegin, spec, /*after*/ false, /*indentNewLines*/ true);
        context_.insertUsedPoints(std::move(usedPoints));
    }
} // namespace acslg::analyzer