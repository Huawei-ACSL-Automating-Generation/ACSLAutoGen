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

    struct ASTExtractor::Impl
    {
        std::unique_ptr<ASTUnit> AST;
    };

    ASTExtractor::ASTExtractor(const std::string &code) : PImpl(std::make_unique<Impl>())
    {
        PImpl->AST = buildASTFromCode(code, "input.cc");
        if (!PImpl->AST)
        {
            llvm::errs() << "Failed to parse code.\n";
        }
    }

    ASTExtractor::~ASTExtractor() = default;

    clang::ASTContext &ASTExtractor::getASTContext() const { return PImpl->AST->getASTContext(); }

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
