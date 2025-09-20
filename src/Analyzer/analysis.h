#ifndef ANALYSIS_H
#define ANALYSIS_H

#include "Context/context.h"
#include "function.h"
#include <vector>
#include <memory>

class ACSLAnalyzer {
  public:
    ACSLAnalyzer(ACSLContext &ctx) : context_(ctx) {}

    void analyzeFunctions();

  private:
    ACSLContext &context_;
    std::vector<std::unique_ptr<ACSLFunction>> functions_;

    void generateFunctionSpec(ACSLFunction *func);
};

#endif
