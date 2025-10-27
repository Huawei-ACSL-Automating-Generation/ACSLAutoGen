#ifndef __ACSLG_SRC_ANALYZER_ANALYSIS_H__
#define __ACSLG_SRC_ANALYZER_ANALYSIS_H__

#include "Context/context.h"
#include "function.h"
#include <vector>
#include <memory>

namespace acslg::analyzer {
    class ACSLAnalyzer {
      public:
        ACSLAnalyzer(context::ACSLGContext &ctx) : context_(ctx) {}

        void analyzeFunctions();

      private:
        context::ACSLGContext &context_;
        std::vector<std::unique_ptr<ACSLFunction>> functions_;

        void generateFunctionSpec(ACSLFunction *func);
    };
} // namespace acslg::analyzer

#endif
