// tests/testUtils/ASTExtractor.cpp

#include "ASTExtractor.h"

using namespace clang;
using namespace clang::tooling;

namespace acslg::test::utils {
    ASTExtractor::ASTExtractor(std::string_view code) {
        AST_ = buildASTFromCodeWithArgs(code, {"-xc", "-std=c11"});
        if (!AST_) {
            llvm::errs() << "Failed to parse code.\n";
        }
    }

    void ASTExtractor::init(std::string_view code) {
        AST_ = buildASTFromCodeWithArgs(code, {"-xc", "-std=c11"});
        if (!AST_) {
            llvm::errs() << "Failed to parse code.\n";
        }
    }
} // namespace acslg::test::utils