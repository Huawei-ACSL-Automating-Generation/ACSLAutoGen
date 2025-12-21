/**
 * @file context.h
 * @brief Declares the shared analysis context that wraps Clang facilities and rewrite helpers.
 */
#ifndef __ACSLG_SRC_CONTEXT_CONTEXT_H__
#define __ACSLG_SRC_CONTEXT_CONTEXT_H__

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "Analyzer/Symbolic/expr.h"

namespace acslg::context {
    /**
     * @class ACSLGContext
     * @brief Central hub for AST access, source management, and rewrite operations during ACSL
     *        generation.
     *
     * This context is passed through analysis and spec-generation pipelines so they can query AST
     * information, locate source ranges, and inject synthesized ACSL annotations into the rewrite
     * buffer.
     */
    class ACSLGContext {
      public:
        /**
         * @brief Construct a context bound to a Clang ASTContext.
         * @param context [in] ASTContext from the frontend; provides translation unit, source
         *                     manager, and language options.
         */
        ACSLGContext(clang::ASTContext &context)
            : context_(context), TU_(context.getTranslationUnitDecl()),
              SM_(context_.getSourceManager()), LO_(context_.getLangOpts()) {
            rewriter_.setSourceMgr(SM_, LO_);
        }

        ACSLGContext(const ACSLGContext &)            = delete;
        ACSLGContext(ACSLGContext &&)                 = delete;
        ACSLGContext &operator=(const ACSLGContext &) = delete;
        ACSLGContext &operator=(ACSLGContext &&)      = delete;

        /**
         * @brief Enumerate all function declarations in the translation unit.
         * @return Vector of function declaration pointers.
         */
        std::vector<const clang::FunctionDecl *> getFunctions() const;
        /// @brief Access the mutable source manager.
        auto getSourceManager() -> auto & { return SM_; }
        /// @brief Access the const source manager.
        auto getSourceManager() const -> const auto & { return SM_; }
        /// @brief Access the language options.
        auto getLangOptions() const -> const auto & { return LO_; }
        /// @brief Access the rewriter.
        auto getRewriter() -> auto & { return rewriter_; }
        /// @brief Access the const rewriter.
        auto getRewriter() const -> const auto & { return rewriter_; }

        /**
         * @brief Insert text into the rewrite buffer while remembering the inserted content.
         * @param Loc [in] Location to insert at.
         * @param Str [in] Text to insert.
         * @param InsertAfter [in] Whether to insert after the location.
         * @param indentNewLines [in] Whether to indent inserted lines automatically.
         */
        void insertText(clang::SourceLocation Loc,
                        llvm::StringRef Str,
                        bool InsertAfter    = true,
                        bool indentNewLines = false) {
            insertedStrs_.emplace_back(Str.data());
            rewriter_.InsertText(Loc, Str, InsertAfter, indentNewLines);
        }

        /// @brief Retrieve all strings inserted so far (for diagnostics or testing).
        auto getInsertedStrings() -> const auto & { return insertedStrs_; }
        /// @brief Access the underlying ASTContext.
        auto getASTContext() -> auto & { return context_; }

        /**
         * @brief Retrieve basic source information for a declaration.
         * @param decl [in] Declaration to inspect.
         * @return Tuple{name (empty if unnamed), source text, filename, line number, column
         *         number} or nullopt if unavailable.
         */
        std::optional<std::tuple<std::string, llvm::StringRef, llvm::StringRef, unsigned, unsigned>> getDeclInfo(
            const clang::Decl *decl);

        /**
         * @brief Retrieve basic source information for a statement.
         * @param stmt [in] Statement to inspect.
         * @return Tuple{source text, filename, line number, column number} or nullopt if
         *         unavailable.
         */
        std::optional<std::tuple<llvm::StringRef, llvm::StringRef, unsigned, unsigned>> getStmtInfo(
            const clang::Stmt *stmt);

        /**
         * @brief Record labels used inside synthesized ACSL and inject them into the source.
         * @param set [in] Set of SourcePoints that require label definitions.
         */
        void insertUsedPoints(std::unordered_set<analyzer::symbolic::SourcePoint> set) {
            for (auto &point : set) {
                if (usedPoints_.contains(point))
                    continue;
                usedPoints_.insert(point);
                rewriter_.InsertText(point.asSourceLocation(), "\n" + point.getLabel() + ":\n;\n",
                                     false, true);
            }
        }

        /**
         * @brief Get the full modified source after all rewrite insertions.
         * @return Concatenated contents of the rewritten main file.
         */
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
