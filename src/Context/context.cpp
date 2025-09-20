#include "context.h"
#include "clang/Lex/Lexer.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/SourceManager.h"

using namespace std;
using namespace clang;
using namespace llvm;

std::vector<const FunctionDecl *> ACSLContext::getFunctions() const {
    std::vector<const FunctionDecl *> funcs;
    for (const auto *decl : TU_->decls()) {
        if (const auto *funcDecl = dyn_cast<FunctionDecl>(decl))
            funcs.push_back(funcDecl);
    }
    return funcs;
}

optional<tuple<string, llvm::StringRef, llvm::StringRef, unsigned, unsigned>> ACSLContext::
    getDeclInfo(const Decl *decl) {
    tuple<string, llvm::StringRef, llvm::StringRef, unsigned, unsigned> result;
    if (!decl)
        return nullopt;
    if (auto namedDecl = dyn_cast<NamedDecl>(decl); namedDecl)
        get<0>(result) = namedDecl->getNameAsString();

    clang::SourceRange range         = decl->getSourceRange();
    clang::CharSourceRange charRange = clang::CharSourceRange::getTokenRange(range);
    get<1>(result)                   = clang::Lexer::getSourceText(charRange, SM_, LO_);

    clang::SourceLocation loc = decl->getBeginLoc();
    get<2>(result)            = SM_.getFilename(loc);
    get<3>(result)            = SM_.getSpellingLineNumber(loc);
    get<4>(result)            = SM_.getSpellingColumnNumber(loc);
    return result;
}

optional<tuple<llvm::StringRef, llvm::StringRef, unsigned, unsigned>> ACSLContext::getStmtInfo(
    const clang::Stmt *stmt) {
    tuple<llvm::StringRef, llvm::StringRef, unsigned, unsigned> result;
    if (!stmt)
        return nullopt;

    clang::SourceRange range         = stmt->getSourceRange();
    clang::CharSourceRange charRange = clang::CharSourceRange::getTokenRange(range);
    get<0>(result)                   = clang::Lexer::getSourceText(charRange, SM_, LO_);

    clang::SourceLocation loc = stmt->getBeginLoc();
    get<1>(result)            = SM_.getFilename(loc);
    get<2>(result)            = SM_.getSpellingLineNumber(loc);
    get<3>(result)            = SM_.getSpellingColumnNumber(loc);
    return result;
}