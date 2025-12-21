/**
 * @file function.h
 * @brief Declares a lightweight wrapper around `clang::FunctionDecl` used during analysis.
 */
#ifndef __ACSLG_SRC_ANALYZER_FUNCTION_H__
#define __ACSLG_SRC_ANALYZER_FUNCTION_H__

#include <clang/AST/Decl.h>

namespace acslg::analyzer {
    /**
     * @class ACSLFunction
     * @brief Represents a function under analysis and exposes cloning helpers.
     */
    class ACSLFunction {
      public:
        /**
         * @brief Construct from a Clang function declaration.
         * @param FD [in] Underlying function declaration pointer.
         */
        ACSLFunction(const clang::FunctionDecl *FD) : FuncDecl(FD) {}

        /**
         * @brief Access the wrapped Clang declaration.
         * @return Pointer to the underlying `clang::FunctionDecl`.
         */
        const clang::FunctionDecl *getFunctionDecl() const { return FuncDecl; }
        /**
         * @brief Create a shallow clone that references the same `clang::FunctionDecl`.
         * @return Newly allocated `ACSLFunction` owning the same declaration pointer.
         */
        std::unique_ptr<ACSLFunction> clone() const;

        /**
         * @brief Equality comparison based on underlying declaration pointer identity.
         * @param RHS [in] Another `ACSLFunction` to compare.
         * @return True if both wrappers reference the same declaration.
         */
        bool operator==(const ACSLFunction &RHS) const { return FuncDecl == RHS.getFunctionDecl(); }

      private:
        const clang::FunctionDecl *FuncDecl;
    };
} // namespace acslg::analyzer

#endif
