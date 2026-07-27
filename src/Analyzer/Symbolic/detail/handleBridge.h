#pragma once

#include "../expr.h"

namespace acslg::analyzer::symbolic::detail {

    const SymbolicExprNode *rawNode(ExprHandle handle);
    const AddressNode *rawNode(AddrHandle handle);

} // namespace acslg::analyzer::symbolic::detail
