// tests/testUtils/ASTExtractor.

#ifndef AST_EXTRACTOR_H
#define AST_EXTRACTOR_H

#include <memory>
#include <string>
#include "clang/AST/AST.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include <sstream>
#include <type_traits>

template <typename NodeType>
class StmtFinderVisitor : public clang::RecursiveASTVisitor<StmtFinderVisitor<NodeType>> {
  public:
    StmtFinderVisitor() : Found(nullptr) {}

    bool VisitStmt(clang::Stmt *S) {
        if (!Found) {
            if (auto *node = llvm::dyn_cast<NodeType>(S))
                Found = node;
        }
        return (Found == nullptr);
    }

    NodeType *Found;
};

template <typename NodeType>
class DeclFinderVisitor : public clang::RecursiveASTVisitor<DeclFinderVisitor<NodeType>> {
  public:
    DeclFinderVisitor() : Found(nullptr) {}

    bool VisitDecl(clang::Decl *D) {
        if (!Found) {
            if (auto *node = llvm::dyn_cast<NodeType>(D))
                Found = node;
        }
        return (Found == nullptr);
    }

    NodeType *Found;
};

class ASTExtractor {
  public:
    explicit ASTExtractor(const std::string &code);

    ASTExtractor(const ASTExtractor &)            = delete;
    ASTExtractor &operator=(const ASTExtractor &) = delete;
    ASTExtractor(ASTExtractor &&)                 = default;
    ASTExtractor &operator=(ASTExtractor &&)      = default;

    template <typename NodeType> NodeType *findFirstDecl() {
        auto &Ctx = getASTContext();
        DeclFinderVisitor<NodeType> visitor;
        visitor.TraverseDecl(Ctx.getTranslationUnitDecl());
        return visitor.Found;
    }

    template <typename NodeType> NodeType *findFirstStmt() {
        auto &Ctx = getASTContext();
        StmtFinderVisitor<NodeType> visitor;
        visitor.TraverseDecl(Ctx.getTranslationUnitDecl());
        return visitor.Found;
    }

    clang::ASTContext &getASTContext() const { return AST->getASTContext(); }
    const clang::LangOptions &getLangOptions() const { return AST->getLangOpts(); }
    clang::SourceManager &getSourceManager() const { return AST->getSourceManager(); }

  private:
    std::unique_ptr<clang::ASTUnit> AST;
};

#endif // AST_EXTRACTOR_H
