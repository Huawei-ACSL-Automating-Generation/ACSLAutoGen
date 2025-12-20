#ifndef __ACSLG_SRC_ANALYZER_ANALYSIS_H__
#define __ACSLG_SRC_ANALYZER_ANALYSIS_H__

#include "Context/context.h"
#include "function.h"
#include <vector>
#include <memory>

namespace acslg::analyzer {
    class ACSLAnalyzer {
      public:
        ACSLAnalyzer(context::ACSLGContext &ctx, std::string targetFuncName = {})
            : context_(ctx), targetFuncName_(std::move(targetFuncName)) {}

        void analyzeFunctions();

      private:
        context::ACSLGContext &context_;
        std::vector<std::unique_ptr<ACSLFunction>> functions_;
        std::string targetFuncName_;

        void generateFunctionSpec(ACSLFunction *func);
    };
} // namespace acslg::analyzer

#endif
