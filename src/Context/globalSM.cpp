// src/globalSM/globalSM.cpp

#include "globalSM.h"
#include "clang/Lex/Lexer.h"
#include "clang/AST/Stmt.h"

using namespace std;
using namespace clang;

optional<tuple<string, llvm::StringRef, llvm::StringRef, unsigned, unsigned>>
GlobalSM::getDeclInfo(const Decl *decl)
{
    tuple<string, llvm::StringRef, llvm::StringRef, unsigned, unsigned> result;
    if (!decl)
        return nullopt;
    auto &SM = GlobalSM::getSM();
    if (auto namedDecl = dyn_cast<NamedDecl>(decl); namedDecl)
        get<0>(result) = namedDecl->getNameAsString();

    clang::SourceRange range         = decl->getSourceRange();
    clang::CharSourceRange charRange = clang::CharSourceRange::getTokenRange(range);
    clang::LangOptions langOpts;
    get<1>(result) = clang::Lexer::getSourceText(charRange, SM, langOpts);

    clang::SourceLocation loc = decl->getBeginLoc();
    get<2>(result)            = SM.getFilename(loc);
    get<3>(result)            = SM.getSpellingLineNumber(loc);
    get<4>(result)            = SM.getSpellingColumnNumber(loc);
    return result;
}

optional<tuple<llvm::StringRef, llvm::StringRef, unsigned, unsigned>>
GlobalSM::getStmtInfo(const clang::Stmt *stmt)
{
    tuple<llvm::StringRef, llvm::StringRef, unsigned, unsigned> result;
    if (!stmt)
        return nullopt;
    auto &SM = GlobalSM::getSM();

    clang::SourceRange range         = stmt->getSourceRange();
    clang::CharSourceRange charRange = clang::CharSourceRange::getTokenRange(range);
    clang::LangOptions langOpts;
    get<0>(result) = clang::Lexer::getSourceText(charRange, SM, langOpts);

    clang::SourceLocation loc = stmt->getBeginLoc();
    get<1>(result)            = SM.getFilename(loc);
    get<2>(result)            = SM.getSpellingLineNumber(loc);
    get<3>(result)            = SM.getSpellingColumnNumber(loc);
    return result;
}