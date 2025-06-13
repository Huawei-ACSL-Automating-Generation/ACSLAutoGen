#ifndef ANALYSIS_H
#define ANALYSIS_H

#include "Context/context.h"
#include "function.h"
#include <vector>
#include <memory>

class ACSLAnalyzer {
  public:
    ACSLAnalyzer(ACSLContext &Ctx) : Context(Ctx) {}

    void analyzeFunctions();

  private:
    ACSLContext &Context;
    std::vector<std::unique_ptr<ACSLFunction>> Functions;

    void generateFunctionSpec(ACSLFunction *func);
};

#endif
