#pragma once

#include "../aggregateExpr.h"

namespace acslg::analyzer::symbolic::detail {

    /// Internal placeholder for an induction variable over an address range.
    class RangeIndexNode : public SymbolicExpr {
      public:
        RangeIndexNode(const RangeIndexNode &)            = delete;
        RangeIndexNode(RangeIndexNode &&)                 = default;
        RangeIndexNode &operator=(const RangeIndexNode &) = delete;
        RangeIndexNode &operator=(RangeIndexNode &&)      = delete;

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_RangeIndex;
        }

        std::string_view getName() const { return name_; }

        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override;
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return -1; }

      private:
        friend struct ExprFactoryInternals;

        explicit RangeIndexNode(std::string_view name,
                                std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_RangeIndex, Type{ScalarKind::UInt, 64}, explicitType),
              name_(name) {}

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &,
            std::unordered_set<SourcePoint> &,
            std::optional<SourcePoint>,
            unsigned,
            bool) const override {
            return name_;
        }

        std::string name_;
    };

} // namespace acslg::analyzer::symbolic::detail
