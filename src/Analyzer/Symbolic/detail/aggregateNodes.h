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

    class OverRangeExprNode : public SymbolicExpr {
      public:
        OverRangeExprNode(const OverRangeExprNode &)            = delete;
        OverRangeExprNode(OverRangeExprNode &&)                 = default;
        OverRangeExprNode &operator=(const OverRangeExprNode &) = delete;
        OverRangeExprNode &operator=(OverRangeExprNode &&)      = delete;
        ~OverRangeExprNode() override                           = default;

        static bool classof(const SymbolicExpr *expr) {
            auto kind = expr->getKind();
            return kind > ExprKind::K_FirstOverRange && kind < ExprKind::K_LastOverRange;
        }

        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override;

        SymbolAddressView getRange() const { return range(); }
        std::string_view getIndexName() const { return indexName_; }

      protected:
        OverRangeExprNode(ExprKind kind,
                          Type naturalType,
                          AddrHandle range,
                          std::string_view indexName,
                          std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(kind, naturalType, explicitType), range_(range.asExpr()),
              indexName_(indexName) {
            if (!this->range().length())
                ERROR("`range_` is not a memory *range*.");
        }

        SymbolAddressView range() const;

        ExprChild range_;
        std::string indexName_;
    };

    class SumOverRangeNode : public OverRangeExprNode, public Symbol {
      public:
        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_SumOverRange;
        }
        static bool classof(const Symbol *symbol) {
            return symbol->getKind() == Symbol::Kind::K_SumOverRange;
        }

        SumOverRangeNode(const SumOverRangeNode &)            = delete;
        SumOverRangeNode(SumOverRangeNode &&)                 = default;
        SumOverRangeNode &operator=(const SumOverRangeNode &) = delete;
        SumOverRangeNode &operator=(SumOverRangeNode &&)      = delete;

        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override;
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 1; }
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &hashIdMap) const override;
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; }

      private:
        friend struct ExprFactoryInternals;

        SumOverRangeNode(AddrHandle range,
                         std::string_view indexName,
                         SourcePoint fromPoint,
                         std::optional<Type> explicitType = std::nullopt);

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        SourcePoint fromPoint_;
    };

    class QuantifierOverRangeNode : public OverRangeExprNode {
      public:
        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_QuantifierOverRange;
        }

        QuantifierOverRangeNode(const QuantifierOverRangeNode &)            = delete;
        QuantifierOverRangeNode(QuantifierOverRangeNode &&)                 = default;
        QuantifierOverRangeNode &operator=(const QuantifierOverRangeNode &) = delete;
        QuantifierOverRangeNode &operator=(QuantifierOverRangeNode &&)      = delete;

        RangeQuantifier getQuantifier() const { return quant_; }
        const SymbolicExpr &getPredicate() const { return *pred_; }

        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override {
            return utils::hash_val(getKind(), OverRangeExprNode::hash(), quant_, pred_->hash());
        }
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return -1; }

      private:
        friend struct ExprFactoryInternals;

        QuantifierOverRangeNode(AddrHandle range,
                                std::string_view indexName,
                                RangeQuantifier quant,
                                ExprHandle pred,
                                std::optional<Type> explicitType = std::nullopt)
            : OverRangeExprNode(ExprKind::K_QuantifierOverRange,
                                Type{ScalarKind::Bool, 8},
                                range,
                                indexName,
                                explicitType),
              quant_(quant), pred_(pred) {}

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        RangeQuantifier quant_;
        ExprChild pred_;
    };

    class MaxMinOverRangeNode : public OverRangeExprNode, public Symbol {
      public:
        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_MaxMinOverRange;
        }
        static bool classof(const Symbol *symbol) {
            return symbol->getKind() == Symbol::Kind::K_MaxMinOverRange;
        }

        MaxMinOverRangeNode(const MaxMinOverRangeNode &)            = delete;
        MaxMinOverRangeNode(MaxMinOverRangeNode &&)                 = default;
        MaxMinOverRangeNode &operator=(const MaxMinOverRangeNode &) = delete;
        MaxMinOverRangeNode &operator=(MaxMinOverRangeNode &&)      = delete;

        RangeExtremum getExtremum() const { return extremum_; }
        const SymbolicExpr &getExpr() const { return *expr_; }

        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override {
            return utils::hash_val(SymbolicExpr::getKind(), OverRangeExprNode::hash(), extremum_,
                                   expr_->hash(), fromPoint_.hash());
        }
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return -1; }
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; }

      private:
        friend struct ExprFactoryInternals;

        MaxMinOverRangeNode(AddrHandle range,
                            std::string_view indexName,
                            RangeExtremum extremum,
                            ExprHandle expr,
                            SourcePoint fromPoint,
                            std::optional<Type> explicitType = std::nullopt)
            : OverRangeExprNode(ExprKind::K_MaxMinOverRange,
                                expr.getValType(),
                                range,
                                indexName,
                                explicitType),
              Symbol(Kind::K_MaxMinOverRange), extremum_(extremum), expr_(expr),
              fromPoint_(std::move(fromPoint)) {}

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        RangeExtremum extremum_;
        ExprChild expr_;
        SourcePoint fromPoint_;
    };

} // namespace acslg::analyzer::symbolic::detail
