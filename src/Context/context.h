#ifndef CONTEXT_H
#define CONTEXT_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"

class ACSLContext
{
  public:
    ACSLContext(clang::ASTContext &Context) : TU(Context.getTranslationUnitDecl()) {}

  private:
    const clang::TranslationUnitDecl *TU;
};

#endif
