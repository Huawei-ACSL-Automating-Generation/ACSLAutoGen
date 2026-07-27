#pragma once

#include "handles.h"

namespace acslg::analyzer::symbolic::detail {
    /// Internal bridge between owning facades and factory-owned DAG handles.
    struct FacadeAccess {
        static ExprHandle exprHandle(const Expr &expression) { return expression.handle(); }
        static AddrHandle addressHandle(const Addr &address) { return address.handle(); }
        static AddrHandle addressBoxHandle(const AddressBox &address) { return address.handle(); }

        static Expr makeExpr(ExprFactory &factory, ExprHandle handle) {
            return Expr{factory, handle};
        }

        static Addr makeAddress(ExprFactory &factory, AddrHandle handle) {
            return Addr{factory, handle};
        }

        static AddressBox makeAddressBox(AddrHandle handle) { return AddressBox{handle}; }
    };
} // namespace acslg::analyzer::symbolic::detail
