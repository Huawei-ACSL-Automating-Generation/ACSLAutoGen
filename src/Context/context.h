#ifndef CONTEXT_H
#define CONTEXT_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Rewrite/Core/Rewriter.h"

class ACSLContext {
  public:
    ACSLContext(clang::ASTContext &context)
        : context_(context), TU_(context.getTranslationUnitDecl()),
          SM_(context_.getSourceManager()), LO_(context_.getLangOpts()) {
        rewriter_.setSourceMgr(SM_, LO_);
    }

    ACSLContext(const ACSLContext &)            = delete;
    ACSLContext(ACSLContext &&)                 = delete;
    ACSLContext &operator=(const ACSLContext &) = delete;
    ACSLContext &operator=(ACSLContext &&)      = delete;

    std::vector<const clang::FunctionDecl *> getFunctions() const;
    auto getSourceManager() -> auto & { return SM_; }
    auto getSourceManager() const -> const auto & { return SM_; }
    auto getLangOptions() const -> const auto & { return LO_; }
    auto getRewriter() -> auto & { return rewriter_; }
    auto getRewriter() const -> const auto & { return rewriter_; }

    void insertText(clang::SourceLocation Loc,
                    llvm::StringRef Str,
                    bool InsertAfter    = true,
                    bool indentNewLines = false) {
        insertedStrs_.emplace_back(Str.data());
        rewriter_.InsertText(Loc, Str, InsertAfter, indentNewLines);
    }

    auto getInsertedStrings() -> const auto & { return insertedStrs_; }
    auto getASTContext() -> auto & { return context_; }

    // Tuple{name(empty string for unnamed Decl), sourceText, filename, lineNumber, columnNumber}
    std::optional<std::tuple<std::string, llvm::StringRef, llvm::StringRef, unsigned, unsigned>> getDeclInfo(
        const clang::Decl *decl);

    // Tuple{sourceText, filename, lineNumber, columnNumber}
    std::optional<std::tuple<llvm::StringRef, llvm::StringRef, unsigned, unsigned>> getStmtInfo(
        const clang::Stmt *stmt);

  private:
    clang::ASTContext &context_;
    const clang::TranslationUnitDecl *TU_;
    clang::SourceManager &SM_;
    clang::LangOptions LO_;
    clang::Rewriter rewriter_{};
    std::vector<std::string> insertedStrs_{};
};

#endif
