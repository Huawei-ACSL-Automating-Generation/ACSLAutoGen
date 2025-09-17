// tests/testUtils/ASTExtractor.cpp

#include "ASTExtractor.h"
#include "globalSM.h"

using namespace clang;
using namespace clang::tooling;

void ASTExtractor::init(std::string_view code) {
    AST = buildASTFromCodeWithArgs(code, {"-xc", "-std=c11"});
    if (!AST) {
        llvm::errs() << "Failed to parse code.\n";
    }
    GlobalSM::getInstance().initialize(getSourceManager(), getLangOptions());
}