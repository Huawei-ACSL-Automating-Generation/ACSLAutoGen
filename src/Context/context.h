#ifndef __ACSLG_SRC_CONTEXT_CONTEXT_H__
#define __ACSLG_SRC_CONTEXT_CONTEXT_H__

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "Analyzer/Symbolic/expr.h"

namespace acslg::context {
    class ACSLGContext {
      public:
        ACSLGContext(clang::ASTContext &context)
            : context_(context), TU_(context.getTranslationUnitDecl()),
              SM_(context_.getSourceManager()), LO_(context_.getLangOpts()) {
            rewriter_.setSourceMgr(SM_, LO_);
        }

        ACSLGContext(const ACSLGContext &)            = delete;
        ACSLGContext(ACSLGContext &&)                 = delete;
        ACSLGContext &operator=(const ACSLGContext &) = delete;
        ACSLGContext &operator=(ACSLGContext &&)      = delete;

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

        void insertUsedPoints(std::unordered_set<analyzer::symbolic::SourcePoint> set) {
            for (auto &point : set) {
                if (usedPoints_.contains(point))
                    continue;
                usedPoints_.insert(point);
                rewriter_.InsertText(point.asSourceLocation(),
                                     "\n//@ ghost " + point.getLabel() + ":\n", false, true);
            }
        }

        std::string getModifiedSource() const {
            auto buf = rewriter_.getRewriteBufferFor(SM_.getMainFileID());
            if (buf == nullptr)
                ERROR("Get rewriter buffer failed.");
            return std::string{buf->begin(), buf->end()};
        }

      private:
        clang::ASTContext &context_;
        const clang::TranslationUnitDecl *TU_;
        clang::SourceManager &SM_;
        clang::LangOptions LO_;
        clang::Rewriter rewriter_{};
        std::vector<std::string> insertedStrs_{};
        std::unordered_set<analyzer::symbolic::SourcePoint> usedPoints_{};
    };
} // namespace acslg::context

#endif
