#ifndef FUNCTION_H
#define FUNCTION_H

#include <unordered_set>
#include <unordered_map>
#include <clang/AST/Decl.h>
#include "Utils/utils.h"

class ACSLFunction {
  public:
    ACSLFunction(const clang::FunctionDecl *FD) : FuncDecl(FD) {}

    const clang::FunctionDecl *getFunctionDecl() const { return FuncDecl; }
    std::unique_ptr<ACSLFunction> clone() const;

    bool operator==(const ACSLFunction &RHS) { return FuncDecl == RHS.getFunctionDecl(); }
    bool operator!=(const ACSLFunction &RHS) { return !(*this == RHS); }

  private:
    const clang::FunctionDecl *FuncDecl;

    // std::unordered_map<const clang::Stmt *,
    //                    std::unordered_set<std::pair<const clang::VarDecl *, const clang::VarDecl
    //                    *>,
    //                                       acslg::pair_hash>>
    //     loopAssigns;
};

#endif
