#pragma once

#include "../aggregateExpr.h"
#include "exprViews.h"
#include "nodeBase.h"
#include "symbolNode.h"

namespace acslg::analyzer::symbolic::detail {

    /// Internal placeholder for an induction variable over an address range.
    class RangeIndexNode : public SymbolicExprNode {
      public:
        RangeIndexNode(const RangeIndexNode &)            = delete;
        RangeIndexNode(RangeIndexNode &&)                 = default;
        RangeIndexNode &operator=(const RangeIndexNode &) = delete;
        RangeIndexNode &operator=(RangeIndexNode &&)      = delete;

        std::string_view getName() const { return name_; }

        std::string dump() const override;
        bool equal(const SymbolicExprNode &) const override;
        std::size_t hash() const override;
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return -1; }

      private:
        friend struct ExprFactoryInternals;

        explicit RangeIndexNode(std::string_view name,
                                std::optional<ExprType> explicitType = std::nullopt)
            : SymbolicExprNode(ExprKind::K_RangeIndex, ExprType{ExprScalarKind::UInt, 64}, explicitType),
              name_(name) {}

        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &,
            std::unordered_set<SourcePoint> &,
            std::optional<SourcePoint>,
            unsigned,
            bool) const override {
            return name_;
        }

        std::string name_;
    };

    class OverRangeExprNode : public SymbolicExprNode {
      public:
        OverRangeExprNode(const OverRangeExprNode &)            = delete;
        OverRangeExprNode(OverRangeExprNode &&)                 = default;
        OverRangeExprNode &operator=(const OverRangeExprNode &) = delete;
        OverRangeExprNode &operator=(OverRangeExprNode &&)      = delete;
        ~OverRangeExprNode() override                           = default;

        std::string dump() const override;
        bool equal(const SymbolicExprNode &) const override;
        std::size_t hash() const override;

        SymbolAddressView getRange() const { return range(); }
        std::string_view getIndexName() const { return indexName_; }

      protected:
        OverRangeExprNode(ExprKind kind,
                          ExprType naturalType,
                          AddrHandle range,
                          std::string_view indexName,
                          std::optional<ExprType> explicitType = std::nullopt)
            : SymbolicExprNode(kind, naturalType, explicitType), range_(range.asExpr()),
              indexName_(indexName) {
            if (!this->range().length())
                ERROR("`range_` is not a memory *range*.");
        }

        SymbolAddressView range() const;

        ExprHandle range_;
        std::string indexName_;
    };

    class SumOverRangeNode : public OverRangeExprNode, public Symbol {
      public:
        SumOverRangeNode(const SumOverRangeNode &)            = delete;
        SumOverRangeNode(SumOverRangeNode &&)                 = default;
        SumOverRangeNode &operator=(const SumOverRangeNode &) = delete;
        SumOverRangeNode &operator=(SumOverRangeNode &&)      = delete;

        std::string dump() const override;
        bool equal(const SymbolicExprNode &) const override;
        std::size_t hash() const override;
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 1; }
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const ExprHandleIndexMap &expressionIndexMap) const override;
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; }

      private:
        friend struct ExprFactoryInternals;

        SumOverRangeNode(AddrHandle range,
                         std::string_view indexName,
                         SourcePoint fromPoint,
                         std::optional<ExprType> explicitType = std::nullopt);

        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        SourcePoint fromPoint_;
    };

    class QuantifierOverRangeNode : public OverRangeExprNode {
      public:
        QuantifierOverRangeNode(const QuantifierOverRangeNode &)            = delete;
        QuantifierOverRangeNode(QuantifierOverRangeNode &&)                 = default;
        QuantifierOverRangeNode &operator=(const QuantifierOverRangeNode &) = delete;
        QuantifierOverRangeNode &operator=(QuantifierOverRangeNode &&)      = delete;

        RangeQuantifier getQuantifier() const { return quant_; }
        ExprHandle getPredicate() const { return pred_; }

        std::string dump() const override;
        bool equal(const SymbolicExprNode &) const override;
        std::size_t hash() const override {
            return utils::hash_val(getKind(), OverRangeExprNode::hash(), quant_, pred_.hash());
        }
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return -1; }

      private:
        friend struct ExprFactoryInternals;

        QuantifierOverRangeNode(AddrHandle range,
                                std::string_view indexName,
                                RangeQuantifier quant,
                                ExprHandle pred,
                                std::optional<ExprType> explicitType = std::nullopt)
            : OverRangeExprNode(ExprKind::K_QuantifierOverRange,
                                ExprType{ExprScalarKind::Bool, 8},
                                range,
                                indexName,
                                explicitType),
              quant_(quant), pred_(pred) {}

        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        RangeQuantifier quant_;
        ExprHandle pred_;
    };

    class MaxMinOverRangeNode : public OverRangeExprNode, public Symbol {
      public:
        MaxMinOverRangeNode(const MaxMinOverRangeNode &)            = delete;
        MaxMinOverRangeNode(MaxMinOverRangeNode &&)                 = default;
        MaxMinOverRangeNode &operator=(const MaxMinOverRangeNode &) = delete;
        MaxMinOverRangeNode &operator=(MaxMinOverRangeNode &&)      = delete;

        RangeExtremum getExtremum() const { return extremum_; }
        ExprHandle getExpr() const { return expr_; }

        std::string dump() const override;
        bool equal(const SymbolicExprNode &) const override;
        std::size_t hash() const override {
            return utils::hash_val(SymbolicExprNode::getKind(), OverRangeExprNode::hash(), extremum_,
                                   expr_.hash(), fromPoint_.hash());
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
                            std::optional<ExprType> explicitType = std::nullopt)
            : OverRangeExprNode(ExprKind::K_MaxMinOverRange,
                                expr.getValType(),
                                range,
                                indexName,
                                explicitType),
              Symbol(Kind::K_MaxMinOverRange), extremum_(extremum), expr_(expr),
              fromPoint_(std::move(fromPoint)) {}

        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        RangeExtremum extremum_;
        ExprHandle expr_;
        SourcePoint fromPoint_;
    };

} // namespace acslg::analyzer::symbolic::detail
