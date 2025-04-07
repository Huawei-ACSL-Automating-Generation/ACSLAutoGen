#include "context.h"

using namespace clang;
using namespace llvm;

std::vector<const FunctionDecl *> ACSLContext::getFunctions() const
{
    std::vector<const FunctionDecl *> funcs;
    for(const auto *decl : TU->decls())
    {
        if(const auto *funcDecl = dyn_cast<FunctionDecl>(decl))
            funcs.push_back(funcDecl);
    }
    return funcs;
}