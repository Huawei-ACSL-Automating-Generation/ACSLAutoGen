/**
 * @file context.cpp
 * @brief Implements the shared analysis context that exposes Clang traversal and rewrite helpers.
 */
#include "context.h"
#include "clang/Lex/Lexer.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/SourceManager.h"

namespace acslg::context {
    /**
     * @brief Collect all function declarations in the translation unit.
     * @return Vector of discovered function declarations.
     */
    std::vector<const clang::FunctionDecl *> ACSLGContext::getFunctions() const {
        std::vector<const clang::FunctionDecl *> funcs;
        for (const auto *decl : TU_->decls()) {
            // Only collect function declarations so later passes can iterate without filtering.
            if (const auto *funcDecl = dyn_cast<clang::FunctionDecl>(decl))
                funcs.push_back(funcDecl);
        }
        return funcs;
    }

    /**
     * @brief Gather source metadata for a declaration, including its text and coordinates.
     * @param decl [in] Declaration of interest.
     * @return Optional tuple with name, text, filename, line, and column.
     *
     * The lookup returns std::nullopt when the declaration is null, allowing callers to skip
     * missing nodes gracefully.
     */
    std::optional<std::tuple<std::string, llvm::StringRef, llvm::StringRef, unsigned, unsigned>> ACSLGContext::
        getDeclInfo(const clang::Decl *decl) {
        std::tuple<std::string, llvm::StringRef, llvm::StringRef, unsigned, unsigned> result;
        if (!decl)
            return std::nullopt;
        if (auto namedDecl = dyn_cast<clang::NamedDecl>(decl); namedDecl)
            // Capture the declaration name when available to help label diagnostics.
            get<0>(result) = namedDecl->getNameAsString();

        clang::SourceRange range         = decl->getSourceRange();
        clang::CharSourceRange charRange = clang::CharSourceRange::getTokenRange(range);
        // Pull the exact source text to allow emitting precise references in generated comments.
        get<1>(result)                   = clang::Lexer::getSourceText(charRange, SM_, LO_);

        clang::SourceLocation loc = decl->getBeginLoc();
        get<2>(result)            = SM_.getFilename(loc);
        get<3>(result)            = SM_.getSpellingLineNumber(loc);
        get<4>(result)            = SM_.getSpellingColumnNumber(loc);
        return result;
    }

    /**
     * @brief Gather source metadata for a statement, including its text and coordinates.
     * @param stmt [in] Statement of interest.
     * @return Optional tuple with text, filename, line, and column.
     */
    std::optional<std::tuple<llvm::StringRef, llvm::StringRef, unsigned, unsigned>> ACSLGContext::
        getStmtInfo(const clang::Stmt *stmt) {
        std::tuple<llvm::StringRef, llvm::StringRef, unsigned, unsigned> result;
        if (!stmt)
            return std::nullopt;

        clang::SourceRange range         = stmt->getSourceRange();
        clang::CharSourceRange charRange = clang::CharSourceRange::getTokenRange(range);
        // Extract the raw statement text to reuse in diagnostics or contract text.
        get<0>(result)                   = clang::Lexer::getSourceText(charRange, SM_, LO_);

        clang::SourceLocation loc = stmt->getBeginLoc();
        get<1>(result)            = SM_.getFilename(loc);
        get<2>(result)            = SM_.getSpellingLineNumber(loc);
        get<3>(result)            = SM_.getSpellingColumnNumber(loc);
        return result;
    }
} // namespace acslg::context
