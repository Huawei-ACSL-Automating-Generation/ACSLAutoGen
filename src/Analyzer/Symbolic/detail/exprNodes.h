#pragma once

#include "../expr.h"

namespace acslg::analyzer::symbolic::detail {

    /// Internal node representing a symbolic value that cannot be modeled.
    class UnknownExprNode : public SymbolicExpr {
      public:
        ~UnknownExprNode() = default;

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_UnknownExpr;
        }

        std::string dump() const override;
        std::size_t hash() const override;
        bool equal(const SymbolicExpr &expr) const override;
        bool isUnknown() const override { return true; }
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return 0; }

      private:
        friend class ::acslg::analyzer::symbolic::ExprFactory;

        explicit UnknownExprNode(std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_UnknownExpr, {ScalarKind::Void, 0}, explicitType) {}

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;
    };

} // namespace acslg::analyzer::symbolic::detail
