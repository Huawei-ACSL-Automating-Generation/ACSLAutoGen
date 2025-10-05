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

template <typename NodeType>
class NthDeclFinderVisitor : public clang::RecursiveASTVisitor<NthDeclFinderVisitor<NodeType>> {
  public:
    explicit NthDeclFinderVisitor(unsigned n) : Found(nullptr), target_(n), count_(0) {}

    bool VisitDecl(clang::Decl *D) {
        if (auto *node = llvm::dyn_cast<NodeType>(D)) {
            if (++count_ == target_) {
                Found = node;
                return false;
            }
        }
        return Found == nullptr;
    }

    NodeType *Found;

  private:
    unsigned target_;
    unsigned count_;
};

class ASTExtractor {
  public:
    ASTExtractor()                                = default;
    ASTExtractor(const ASTExtractor &)            = delete;
    ASTExtractor &operator=(const ASTExtractor &) = delete;
    ASTExtractor(ASTExtractor &&)                 = default;
    ASTExtractor &operator=(ASTExtractor &&)      = default;

    void init(std::string_view code);

    template <typename NodeType> NodeType *findFirstDecl() {
        auto &ctx = getASTContext();
        DeclFinderVisitor<NodeType> visitor;
        visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        return visitor.Found;
    }

    template <typename NodeType> NodeType *findNthDecl(unsigned n) {
        auto &ctx = getASTContext();
        NthDeclFinderVisitor<NodeType> visitor(n);
        visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        return visitor.Found;
    }

    template <typename NodeType> NodeType *findFirstStmt() {
        auto &ctx = getASTContext();
        StmtFinderVisitor<NodeType> visitor;
        visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        return visitor.Found;
    }

    clang::ASTContext &getASTContext() const { return AST_->getASTContext(); }
    const clang::LangOptions &getLangOptions() const { return AST_->getLangOpts(); }
    clang::SourceManager &getSourceManager() const { return AST_->getSourceManager(); }

  private:
    std::unique_ptr<clang::ASTUnit> AST_;
};

#endif // AST_EXTRACTOR_H
