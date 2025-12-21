/**
 * @file analysis.cpp
 * @brief Implements the core analysis loop that walks functions and emits ACSL contracts.
 */
#include "analysis.h"
#include "macros.h"
#include "function.h"
#include "clang/AST/Stmt.h"
#include "state.h"
#include "SpecGenerator/specGenerator.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Basic/SourceManager.h"
#include <sstream>

namespace acslg::analyzer {
    /**
     * @brief Iterate over functions discovered in the translation unit and emit ACSL contracts.
     *
     * The routine filters out declarations without bodies, definitions outside the main file, and
     * multiple occurrences of `main`. Each accepted function is wrapped in an `ACSLFunction` for
     * downstream processing.
     */
    void ACSLAnalyzer::analyzeFunctions() {
        PROCESS("Running analysis functions...");
        static int count = 0;
        for (auto *func : this->context_.getFunctions()) {
            // Skip forward declarations to avoid generating specs for functions without code.
            if (!func->hasBody())
                continue;
            auto loc = func->getLocation();
            // Ignore functions that originate from headers to keep the rewrite confined to the main
            // source file.
            if (!context_.getSourceManager().isInMainFile(loc))
                continue;

            if (!shouldAnalyze(func))
                continue;
            // Only generate a single contract for main even if it appears multiple times in the AST
            // (e.g., due to templates or diagnostics).
            if (func->getNameAsString() == "main" && count)
                continue;
            ++count;
            auto wrappedFunc = std::make_unique<ACSLFunction>(func);
            generateFunctionSpec(wrappedFunc.get());
            functions_.push_back(std::move(wrappedFunc));
        }
        verifyRequestedFunctionsFound();
    }

    /**
     * @brief Produce and insert the ACSL specification for a given function.
     * @param func [in] Wrapper around the target `clang::FunctionDecl`.
     *
     * The method builds an initial program state, symbolically steps through the function body to
     * compute postconditions, and hands both pre/post states to the spec generator. The resulting
     * ACSL text is inserted directly into the source via the context rewriter.
     */
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

        // Step the whole compound body so we reuse the existing scope clean-up logic (collectLocalVars
        // + eraseExpiredLocals) instead of reimplementing it when generating contracts.
        state->step(CS);
        // INFO(state->dump());

        if (FD->getNameAsString() == "main") {
            WARN("Ignore the contracts generating of MAIN Function.");
            return;
        }

        // Generate the ACSL contract text and record any synthetic labels used in the rewrite.
        auto [spec, usedPoints] = spec_generator::emitFunctionContract(*preState, *state);
        // Generic light simplification: drop requires/ensures clauses that mention local parameters,
        // since those should not appear in the final ACSL.
        std::vector<std::string> params;
        params.reserve(FD->param_size());
        for (const auto *p : FD->parameters())
            params.push_back(p->getNameAsString());

        // Light cleanup: remove redundant conjunctions with \true, but preserve all requires/ensures.
        std::ostringstream cleaned;
        std::istringstream iss(spec);
        std::string line;
        while (std::getline(iss, line)) {
            const std::string true_and = "\\true && ";
            const std::string and_true = " && \\true";
            while (line.find(true_and) != std::string::npos)
                line.replace(line.find(true_and), true_and.size(), "");
            while (line.find(and_true) != std::string::npos)
                line.replace(line.find(and_true), and_true.size(), "");
            cleaned << line << "\n";
        }
        spec = cleaned.str();
        INFO(spec);

        auto &SM       = context_.getSourceManager();
        auto fileBegin = SM.getFileLoc(FD->getSourceRange().getBegin());
        // Insert the ACSL contract before the function definition to keep the source stable.
        context_.insertText(fileBegin, spec, /*after*/ false, /*indentNewLines*/ true);
        context_.insertUsedPoints(std::move(usedPoints));
    }

    bool ACSLAnalyzer::shouldAnalyze(const clang::FunctionDecl *func) {
        const std::string funcName = func->getNameAsString();
        if (!targetFunctions_.empty() && !targetFunctions_.contains(funcName))
            return false;

        if (!targetFunctions_.empty())
            seenTargetFunctions_.insert(funcName);
        return true;
    }

    void ACSLAnalyzer::verifyRequestedFunctionsFound() {
        if (targetFunctions_.empty())
            return;

        std::vector<std::string> missing;
        for (const auto &name : targetFunctions_) {
            if (!seenTargetFunctions_.contains(name))
                missing.push_back(name);
        }
        if (missing.empty())
            return;

        std::ostringstream oss;
        oss << "Requested function(s) not found in main file: ";
        for (size_t i = 0; i < missing.size(); ++i) {
            oss << missing[i];
            if (i + 1 < missing.size())
                oss << ", ";
        }
        ERROR(oss.str());
    }
} // namespace acslg::analyzer
