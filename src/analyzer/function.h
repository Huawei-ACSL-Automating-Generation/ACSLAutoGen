#ifndef FUNCTION_H
#define FUNCTION_H

#include "clang/AST/Decl.h"
#include "unordered_set"
#include "unordered_map"

class ACSLFunction
{
  public:
    ACSLFunction(const clang::FunctionDecl *FD) : FuncDecl(FD) {}

    const clang::FunctionDecl *getFunctionDecl() const { return FuncDecl; }
    ACSLFunction *clone() const;

  private:
    struct PairHash
    {
        std::size_t
        operator()(const std::pair<const clang::VarDecl *, const clang::VarDecl *> &p) const
        {
            return std::hash<const void *>()(p.first) ^ std::hash<const void *>()(p.second);
        }
    };

    const clang::FunctionDecl *FuncDecl;

    std::unordered_map<const clang::Stmt *,
        std::unordered_set<std::pair<const clang::VarDecl *, const clang::VarDecl *>, PairHash>>
        loopAssigns;
};

#endif
