#include "context.h"
#include "clang/Lex/Lexer.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/SourceManager.h"

namespace acslg::context {
    std::vector<const clang::FunctionDecl *> ACSLContext::getFunctions() const {
        std::vector<const clang::FunctionDecl *> funcs;
        for (const auto *decl : TU_->decls()) {
            if (const auto *funcDecl = dyn_cast<clang::FunctionDecl>(decl))
                funcs.push_back(funcDecl);
        }
        return funcs;
    }

    std::optional<std::tuple<std::string, llvm::StringRef, llvm::StringRef, unsigned, unsigned>> ACSLContext::
        getDeclInfo(const clang::Decl *decl) {
        std::tuple<std::string, llvm::StringRef, llvm::StringRef, unsigned, unsigned> result;
        if (!decl)
            return std::nullopt;
        if (auto namedDecl = dyn_cast<clang::NamedDecl>(decl); namedDecl)
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

    std::optional<std::tuple<llvm::StringRef, llvm::StringRef, unsigned, unsigned>> ACSLContext::
        getStmtInfo(const clang::Stmt *stmt) {
        std::tuple<llvm::StringRef, llvm::StringRef, unsigned, unsigned> result;
        if (!stmt)
            return std::nullopt;

        clang::SourceRange range         = stmt->getSourceRange();
        clang::CharSourceRange charRange = clang::CharSourceRange::getTokenRange(range);
        get<0>(result)                   = clang::Lexer::getSourceText(charRange, SM_, LO_);

        clang::SourceLocation loc = stmt->getBeginLoc();
        get<1>(result)            = SM_.getFilename(loc);
        get<2>(result)            = SM_.getSpellingLineNumber(loc);
        get<3>(result)            = SM_.getSpellingColumnNumber(loc);
        return result;
    }
} // namespace acslg::context