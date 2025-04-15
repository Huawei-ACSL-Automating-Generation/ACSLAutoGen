// tests/utils/ASTExtractor.cpp

#include "ASTExtractor.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include <sstream>

using namespace clang;
using namespace clang::tooling;

namespace utils
{

    ASTExtractor::ASTExtractor(const std::string &code) : AST()
    {
        AST = buildASTFromCode(code);
        if (!AST)
        {
            llvm::errs() << "Failed to parse code.\n";
        }
    }

    clang::ASTContext &ASTExtractor::getASTContext() const { return AST->getASTContext(); }

    template <typename NodeType>
    class NodeFinderVisitor : public clang::RecursiveASTVisitor<NodeFinderVisitor<NodeType>>
    {
      public:
        NodeFinderVisitor() : Found(nullptr) {}

        bool VisitStmt(clang::Stmt *S)
        {
            if (!Found)
            {
                if (auto *node = llvm::dyn_cast<NodeType>(S))
                    Found = node;
            }
            return (Found == nullptr);
        }

        bool VisitDecl(clang::Decl *D)
        {
            if (!Found)
            {
                if (auto *node = llvm::dyn_cast<NodeType>(D))
                    Found = node;
            }
            return (Found == nullptr);
        }

        NodeType *Found;
    };

    template <typename NodeType> NodeType *ASTExtractor::findFirstNode()
    {
        auto &Ctx = getASTContext();
        NodeFinderVisitor<NodeType> visitor;
        visitor.TraverseDecl(Ctx.getTranslationUnitDecl());
        return visitor.Found;
    }
} // namespace utils
