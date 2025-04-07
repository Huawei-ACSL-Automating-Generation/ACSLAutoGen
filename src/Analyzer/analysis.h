#ifndef ANALYSIS_H
#define ANALYSIS_H

#include "../Context/context.h"

class ACSLAnalyzer
{
  public:
    ACSLAnalyzer(ACSLContext &Ctx) : Context(Ctx) {}

    // TODO : returns for analysis?
    void analysis_funcs();

  private:
    ACSLContext &Context;

    // TODO : return for process single func?
    void process_func(const clang::FunctionDecl *);
};

#endif
