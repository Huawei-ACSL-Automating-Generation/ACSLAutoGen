// src/context/globalSM.h

#ifndef GLOBAL_SM_H
#define GLOBAL_SM_H

#include <memory>
#include <mutex>
#include <tuple>
#include "clang/Basic/SourceManager.h"
#include "clang/AST/ASTContext.h"

class GlobalSM
{
  public:
    static GlobalSM &getInstance()
    {
        static GlobalSM instance;
        return instance;
    }

    static clang::SourceManager &getSM() { return *getInstance().SM; }

    // Tuple{name(empty string for unnamed Decl), sourceText, filename, lineNumber, columnNumber}
    static std::optional<
        std::tuple<std::string, llvm::StringRef, llvm::StringRef, unsigned, unsigned>>
    getDeclInfo(const clang::Decl *decl);

    // Tuple{sourceText, filename, lineNumber, columnNumber}
    static std::optional<std::tuple<llvm::StringRef, llvm::StringRef, unsigned, unsigned>>
    getStmtInfo(const clang::Stmt *stmt);

    void initialize(clang::ASTContext &context)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SM = &context.getSourceManager();
    }

    GlobalSM(const GlobalSM &)            = delete;
    GlobalSM &operator=(const GlobalSM &) = delete;

  private:
    GlobalSM()  = default;
    ~GlobalSM() = default;

    clang::SourceManager *SM;
    mutable std::mutex mutex_;
};

#endif // GLOBAL_SM_H
