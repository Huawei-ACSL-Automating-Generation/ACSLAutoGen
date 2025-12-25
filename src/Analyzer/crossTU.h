#ifndef __ACSLG_SRC_ANALYZER_CROSSTU_H__
#define __ACSLG_SRC_ANALYZER_CROSSTU_H__

#include "clang/AST/Decl.h"
#include "clang/Tooling/CompilationDatabase.h"

namespace acslg::context {
    class ACSLGContext;
}

namespace acslg::analyzer::ctu {

    void init(const clang::tooling::CompilationDatabase &db, bool enableFramacCompat);

    const clang::FunctionDecl *importDefinitionIfAvailable(const clang::FunctionDecl *callee,
                                                           context::ACSLGContext &ctx);

} // namespace acslg::analyzer::ctu

#endif
