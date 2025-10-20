#ifndef __ACSLG_SRC_ANALYZER_FUNCTION_H__
#define __ACSLG_SRC_ANALYZER_FUNCTION_H__

#include <clang/AST/Decl.h>

namespace acslg::analyzer {
    class ACSLFunction {
      public:
        ACSLFunction(const clang::FunctionDecl *FD) : FuncDecl(FD) {}

        const clang::FunctionDecl *getFunctionDecl() const { return FuncDecl; }
        std::unique_ptr<ACSLFunction> clone() const;

        bool operator==(const ACSLFunction &RHS) const { return FuncDecl == RHS.getFunctionDecl(); }

      private:
        const clang::FunctionDecl *FuncDecl;
    };
} // namespace acslg::analyzer

#endif
