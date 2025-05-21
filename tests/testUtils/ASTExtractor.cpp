// tests/testUtils/ASTExtractor.cpp

#include "ASTExtractor.h"

using namespace clang;
using namespace clang::tooling;

ASTExtractor::ASTExtractor(const std::string &code)
{
    AST = buildASTFromCode(code);
    if (!AST)
    {
        llvm::errs() << "Failed to parse code.\n";
    }
}

clang::ASTContext &ASTExtractor::getASTContext() const { return AST->getASTContext(); }