// src/Context/globalSM.h

#ifndef GLOBAL_SM_H
#define GLOBAL_SM_H

#include <memory>
#include <tuple>
#include "macros.h"
#include "clang/Basic/SourceManager.h"
#include "clang/AST/ASTContext.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"

class GlobalSM {
  public:
    static GlobalSM &getInstance() {
        static GlobalSM instance;
        return instance;
    }

    static clang::SourceManager &getSM() { return *getInstance().SM_; }
    static auto getRewriter() -> auto & { return getInstance().rewriter_; }
    static void InsertText(clang::SourceLocation Loc,
                           llvm::StringRef Str,
                           bool InsertAfter    = true,
                           bool indentNewLines = false) {
        getInstance().insertedStrs_.emplace_back(Str.data());
        getInstance().rewriter_.InsertText(Loc, Str, InsertAfter, indentNewLines);
    }
    static auto getInsertedStrings() -> const auto & { return getInstance().insertedStrs_; }

    // Tuple{name(empty string for unnamed Decl), sourceText, filename, lineNumber, columnNumber}
    static std::optional<std::tuple<std::string, llvm::StringRef, llvm::StringRef, unsigned, unsigned>> getDeclInfo(
        const clang::Decl *decl);

    // Tuple{sourceText, filename, lineNumber, columnNumber}
    static std::optional<std::tuple<llvm::StringRef, llvm::StringRef, unsigned, unsigned>> getStmtInfo(
        const clang::Stmt *stmt);

    void initialize(clang::SourceManager &SM, const clang::LangOptions &LO) {
        SM_ = &SM;
        rewriter_.setSourceMgr(*SM_, LO);
    }

    GlobalSM(const GlobalSM &)            = delete;
    GlobalSM &operator=(const GlobalSM &) = delete;

  private:
    GlobalSM()  = default;
    ~GlobalSM() = default;

    clang::SourceManager *SM_ = nullptr;
    clang::Rewriter rewriter_;
    std::vector<std::string> insertedStrs_;
};

#endif // GLOBAL_SM_H
