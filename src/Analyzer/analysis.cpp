#include "analysis.h"

#include "macros.h"

using namespace clang;
void ACSLAnalyzer::analysis_funcs()
{
    PROCESS("Running analysis functions...");
    for(auto *func : this->Context.getFunctions())
    {
        process_func(func);
    }
}

void ACSLAnalyzer::process_func(const FunctionDecl *func)
{
    std::cout << "Processing function: " << func->getNameAsString() << std::endl;
}