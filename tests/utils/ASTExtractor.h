// tests/utils/ASTExtractor.

#ifndef AST_EXTRACTOR_H
#define AST_EXTRACTOR_H

#include <memory>
#include <string>
#include "clang/AST/AST.h"
#include "clang/Tooling/Tooling.h"

namespace utils
{
    class ASTExtractor
    {
      public:
        explicit ASTExtractor(const std::string &code);
        ~ASTExtractor();

        ASTExtractor(const ASTExtractor &) = delete;
        ASTExtractor &operator=(const ASTExtractor &) = delete;
        ASTExtractor(ASTExtractor &&) = default;
        ASTExtractor &operator=(ASTExtractor &&) = default;

        template <typename NodeType> NodeType *findFirstNode();

        clang::ASTContext &getASTContext() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> PImpl;
    };

} // namespace utils

#endif // AST_EXTRACTOR_H
