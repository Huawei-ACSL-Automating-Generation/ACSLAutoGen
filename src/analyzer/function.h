#ifndef FUNCTION_H
#define FUNCTION_H

#include "clang/AST/Decl.h"

class ACSLFunction
{
  public:
    ACSLFunction(const clang::FunctionDecl *FD) : FuncDecl(FD) {}

    const clang::FunctionDecl *getFunctionDecl() const { return FuncDecl; }

  private:
    const clang::FunctionDecl *FuncDecl;
};

#endif
