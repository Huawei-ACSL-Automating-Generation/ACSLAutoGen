/**
 * @file analysis.h
 * @brief Declares the ACSLAnalyzer entry point that walks discovered functions and drives contract
 * generation.
 */
#ifndef __ACSLG_SRC_ANALYZER_ANALYSIS_H__
#define __ACSLG_SRC_ANALYZER_ANALYSIS_H__

#include "Context/context.h"
#include "function.h"
#include <vector>
#include <memory>

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
         */
        ACSLAnalyzer(context::ACSLGContext &ctx) : context_(ctx) {}

        /**
         * @brief Entry point that iterates functions in the translation unit and generates ACSL
         * specifications for each eligible definition.
         */
        void analyzeFunctions();

      private:
        context::ACSLGContext &context_;
        std::vector<std::unique_ptr<ACSLFunction>> functions_;

        /**
         * @brief Build and insert the ACSL contract for a single function.
         * @param func [in] Wrapper around the Clang function declaration being processed.
         */
        void generateFunctionSpec(ACSLFunction *func);
    };
} // namespace acslg::analyzer

#endif
