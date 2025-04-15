// src/globalSM/globalSM.h

#ifndef GLOBAL_SM_H
#define GLOBAL_SM_H

#include <memory>
#include <mutex>
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

    void initialize(clang::ASTContext &context)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SM = &context.getSourceManager();
    }

    GlobalSM(const GlobalSM &) = delete;
    GlobalSM &operator=(const GlobalSM &) = delete;

  private:
    GlobalSM() = default;
    ~GlobalSM() = default;

    clang::SourceManager *SM;
    mutable std::mutex mutex_;
};

#endif // SIMPLE_SM_H
