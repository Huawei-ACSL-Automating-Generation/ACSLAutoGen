#pragma once

#include "addressNode.h"
#include "nativeRtti.h"

namespace acslg::analyzer::symbolic::detail {

    /// Internal node inspection for symbolic implementation code.
    struct HandleAccess {
        static ExprHandle handle(const SymbolicExprNode *node) { return ExprHandle{node}; }
        static AddrHandle handle(const AddressNode *node) { return AddrHandle{node}; }

        static const SymbolicExprNode &node(ExprHandle handle) { return *handle.expr_; }
        static const AddressNode &node(AddrHandle handle) { return *handle.ptr_; }

        template <typename T> static bool isa(ExprHandle handle) {
            return detail::isa<T>(handle.expr_.get());
        }

        template <typename T> static const T *dynCast(ExprHandle handle) {
            return detail::dyn_cast<T>(handle.expr_.get());
        }

        template <typename T> static const T &cast(ExprHandle handle) {
            return *detail::cast<T>(handle.expr_.get());
        }

        template <typename T> static bool isa(AddrHandle handle) {
            return detail::isa<T>(handle.ptr_);
        }

        template <typename T> static const T *dynCast(AddrHandle handle) {
            return detail::dyn_cast<T>(handle.ptr_);
        }

        template <typename T> static const T &cast(AddrHandle handle) {
            return *detail::cast<T>(handle.ptr_);
        }
    };

} // namespace acslg::analyzer::symbolic::detail
