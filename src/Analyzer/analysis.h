/**
 * @file analysis.h
 * @brief Declares the ACSLAnalyzer entry point that walks discovered functions and drives contract
 * generation.
 */
#ifndef __ACSLG_SRC_ANALYZER_ANALYSIS_H__
#define __ACSLG_SRC_ANALYZER_ANALYSIS_H__

#include "Context/context.h"
#include "function.h"
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace acslg::analyzer {
    /**
     * @class ACSLAnalyzer
     * @brief Coordinates function discovery and ACSL contract emission for a translation unit.
     *
     * An ACSLAnalyzer owns no AST nodes itself; it holds onto the shared context and wraps each
     * function in an `ACSLFunction` to build the specification. Instances are lightweight and
     * reused per translation unit.
     */
    class ACSLAnalyzer {
      public:
        /**
         * @brief Construct an analyzer bound to a shared compilation context.
         * @param ctx [in] Shared context holding the AST, source manager, and rewriter.
         * @param targetFunctions [in] Optional list of function names to restrict analysis to.
         */
        ACSLAnalyzer(context::ACSLGContext &ctx, std::vector<std::string> targetFunctions = {})
            : context_(ctx), targetFunctions_(targetFunctions.begin(), targetFunctions.end()) {}

        /**
         * @brief Entry point that iterates functions in the translation unit and generates ACSL
         * specifications for each eligible definition.
         */
        void analyzeFunctions();

      private:
        context::ACSLGContext &context_;
        std::vector<std::unique_ptr<ACSLFunction>> functions_;
        std::unordered_set<std::string> targetFunctions_;
        std::unordered_set<std::string> seenTargetFunctions_;

        bool shouldAnalyze(const clang::FunctionDecl *func);
        void verifyRequestedFunctionsFound();
        /**
         * @brief Build and insert the ACSL contract for a single function.
         * @param func [in] Wrapper around the Clang function declaration being processed.
         */
        void generateFunctionSpec(ACSLFunction *func);
    };
} // namespace acslg::analyzer

#endif
