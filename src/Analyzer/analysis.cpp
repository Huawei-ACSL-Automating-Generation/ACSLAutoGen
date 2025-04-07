#include "analysis.h"
#include "macros.h"
#include "function.h"

using namespace clang;
using namespace std;
void ACSLAnalyzer::analyzeFunctions()
{
    PROCESS("Running analysis functions...");
    for(auto *func : this->Context.getFunctions())
    {
        auto wrappedFunc = make_unique<ACSLFunction>(func);
        generateFunctionSpec(wrappedFunc.get());
        Functions.push_back(std::move(wrappedFunc));
    }
}

void ACSLAnalyzer::generateFunctionSpec(ACSLFunction *func)
{
    INFO("Processing Function " + func->getFunctionDecl()->getNameAsString());
}