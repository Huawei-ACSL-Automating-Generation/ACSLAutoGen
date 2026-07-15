#pragma once

#include "../expr.h"

namespace acslg::analyzer::symbolic {

    template <typename Node, typename... Args>
    std::unique_ptr<Node> ExprFactory::makeNode(Args &&...args) {
        return std::unique_ptr<Node>{new Node(std::forward<Args>(args)...)};
    }

    namespace detail {

        struct ExprFactoryInternals {
            template <typename Node, typename... Args>
            static std::unique_ptr<Node> makeNode(Args &&...args) {
                return std::unique_ptr<Node>{new Node(std::forward<Args>(args)...)};
            }

            static ExprHandle intern(
                ExprFactory &factory,
                utils::not_null<std::unique_ptr<SymbolicExpr>> node) {
                return factory.intern(std::move(node));
            }
        };

    } // namespace detail
} // namespace acslg::analyzer::symbolic
