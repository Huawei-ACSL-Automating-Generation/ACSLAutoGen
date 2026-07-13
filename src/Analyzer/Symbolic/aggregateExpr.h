/**
 * @file aggregateExpr.h
 * @brief Declares symbolic aggregate expressions such as range sums and quantified ranges.
 */
#ifndef __ACSLG_SRC_ANALYZER_SYMBOLIC_AGGREGATEEXPR_H__
#define __ACSLG_SRC_ANALYZER_SYMBOLIC_AGGREGATEEXPR_H__

#include <string_view>

#include "expr.h"
#include "Utils/utils.h"

namespace acslg::analyzer::symbolic {
    enum class RangeQuantifier {
        ForAll,
        Exist
    };

    enum class RangeExtremum {
        Max,
        Min
    };

    /**
     * @class SymbolAddress::RangeIndex
     * @brief Placeholder representing the induction variable when reasoning over address ranges.
     */
    class SymbolAddress::RangeIndex : public SymbolicExpr {
      public:
        RangeIndex(const RangeIndex &)            = delete;
        RangeIndex(RangeIndex &&)                 = default;
        RangeIndex &operator=(const RangeIndex &) = delete;
        RangeIndex &operator=(RangeIndex &&)      = delete;

        static bool classof(const SymbolicExpr *e) {
            return e->getKind() == ExprKind::K_RangeIndex;
        }

        std::string_view getName() const { return name_; }

        // SymbolicExpr
      public:
        /**
         * @brief Dump a human-readable representation.
         * @return Textual description.
         */
        std::string dump() const override;
        /**
         * @brief Equality check ignoring the placeholder name (names do not affect semantics).
         * @param other Expression to compare.
         */
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override;
        bool isLinear() const override { return false; };
        int getMaxDegree() const override { return -1; };
      private:
        utils::expected<std::string, GetACSLError> doGetACSL(const GetACSLConfig &,
                                                             std::unordered_set<SourcePoint> &,
                                                             std::optional<SourcePoint>,
                                                             unsigned,
                                                             bool) const override {
            return name_;
        };

      private:
        friend struct detail::ExprFactoryInternals;

        explicit RangeIndex(std::string_view name)
            : SymbolicExpr(ExprKind::K_RangeIndex, Type{ScalarKind::UInt, 64}), name_(name) {}

        std::string name_;
    };

    namespace detail {

    /**
     * @class OverRangeExprNode
     * @brief Base class for expressions that quantify or aggregate over a symbolic address range.
     */
    class OverRangeExprNode : public SymbolicExpr {
      public:
        OverRangeExprNode(const OverRangeExprNode &) = delete;
        OverRangeExprNode(OverRangeExprNode &&) = default;
        OverRangeExprNode &operator=(const OverRangeExprNode &) = delete;
        OverRangeExprNode &operator=(OverRangeExprNode &&) = delete;
        virtual ~OverRangeExprNode()                   = default;

        static bool classof(const SymbolicExpr *e) {
            auto k = e->getKind();
            return (k > ExprKind::K_FirstOverRange) && (k < ExprKind::K_LastOverRange);
        }

        // SymbolicExpr
        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override;

        const SymbolAddress &getRange() const { return range(); }
        std::string_view getIndexName() const { return indexName_; }

      protected:
        OverRangeExprNode(ExprKind kind, Type type, AddrHandle range, std::string_view indexName)
            : SymbolicExpr(kind, type), range_(range.asExpr()), indexName_(indexName) {
            if (!this->range().getLength())
                ERROR("`range_` is not a memory *range*.");
        }

        const SymbolAddress &range() const;

        ExprChild range_;
        std::string indexName_;
    };

    class SumOverRangeNode : public OverRangeExprNode, public Symbol {
      public:
        static bool classof(const SymbolicExpr *e) {
            return e->getKind() == ExprKind::K_SumOverRange;
        }
        static bool classof(const Symbol *e) {
            return e->getKind() == Symbol::Kind::K_SumOverRange;
        }

        SumOverRangeNode(const SumOverRangeNode &)            = delete;
        SumOverRangeNode(SumOverRangeNode &&)                 = default;
        SumOverRangeNode &operator=(const SumOverRangeNode &) = delete;
        SumOverRangeNode &operator=(SumOverRangeNode &&)      = delete;

        // SymbolicExpr
        /// @brief Dump a readable description of the sum.
        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override;
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 1; }
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &hashIdMap) const override;

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        // Symbol
      public:
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; };

      private:
        friend struct ExprFactoryInternals;

        SumOverRangeNode(AddrHandle range, std::string_view indexName, SourcePoint fromPoint);

        SourcePoint fromPoint_;

    };

    class QuantifierOverRangeNode : public OverRangeExprNode {
      public:
        static bool classof(const SymbolicExpr *e) {
            return e->getKind() == ExprKind::K_QuantifierOverRange;
        }

        QuantifierOverRangeNode(const QuantifierOverRangeNode &) = delete;
        QuantifierOverRangeNode(QuantifierOverRangeNode &&) = default;
        QuantifierOverRangeNode &operator=(const QuantifierOverRangeNode &) = delete;
        QuantifierOverRangeNode &operator=(QuantifierOverRangeNode &&) = delete;

        RangeQuantifier getQuantifier() const { return quant_; }
        const SymbolicExpr &getPredicate() const { return *pred_; }

        // SymbolicExpr
        /// @brief Dump the quantifier, range, and predicate.
        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override {
            return utils::hash_val(getKind(), OverRangeExprNode::hash(), quant_, pred_->hash());
        };
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return -1; }

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

      private:
        friend struct ExprFactoryInternals;

        QuantifierOverRangeNode(AddrHandle range,
                            std::string_view indexName,
                            RangeQuantifier quant,
                            ExprHandle pred)
            : OverRangeExprNode(ExprKind::K_QuantifierOverRange,
                            Type{ScalarKind::Bool, 8},
                            range,
                            indexName),
              quant_(quant), pred_(pred) {}

        RangeQuantifier quant_;
        ExprChild pred_;
    };

    class MaxMinOverRangeNode : public OverRangeExprNode, public Symbol {
      public:
        static bool classof(const SymbolicExpr *e) {
            return e->getKind() == ExprKind::K_MaxMinOverRange;
        }
        static bool classof(const Symbol *e) {
            return e->getKind() == Symbol::Kind::K_MaxMinOverRange;
        }

        MaxMinOverRangeNode(const MaxMinOverRangeNode &) = delete;
        MaxMinOverRangeNode(MaxMinOverRangeNode &&) = default;
        MaxMinOverRangeNode &operator=(const MaxMinOverRangeNode &) = delete;
        MaxMinOverRangeNode &operator=(MaxMinOverRangeNode &&) = delete;

        RangeExtremum getExtremum() const { return extremum_; }
        const SymbolicExpr &getExpr() const { return *expr_; }

        // SymbolicExpr
        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override {
            return utils::hash_val(SymbolicExpr::getKind(), OverRangeExprNode::hash(), extremum_,
                                   expr_->hash(), fromPoint_.hash());
        };
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return -1; }

        // Symbol
      public:
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; };

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

      private:
        friend struct ExprFactoryInternals;

        MaxMinOverRangeNode(AddrHandle range,
                        std::string_view indexName,
                        RangeExtremum extremum,
                        ExprHandle expr,
                        SourcePoint fromPoint)
            : OverRangeExprNode(ExprKind::K_MaxMinOverRange,
                            expr.getValType(),
                            range,
                            indexName),
              Symbol(Kind::K_MaxMinOverRange), extremum_(extremum), expr_(expr),
              fromPoint_(std::move(fromPoint)) {}

        RangeExtremum extremum_;
        ExprChild expr_;
        SourcePoint fromPoint_;
    };

    } // namespace detail

    ExprHandle makeSumOverRangeHandle(ExprFactory &factory,
                                      const SymbolAddress &range,
                                      std::string_view indexName,
                                      SourcePoint fromPoint);

    ExprHandle makeQuantifierOverRangeHandle(ExprFactory &factory,
                                             const SymbolAddress &range,
                                             std::string_view indexName,
                                             RangeQuantifier quantifier,
                                             const SymbolicExpr &predicate);
    ExprHandle makeQuantifierOverRangeHandle(ExprFactory &factory,
                                             AddrHandle range,
                                             std::string_view indexName,
                                             RangeQuantifier quantifier,
                                             ExprHandle predicate);

    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         const SymbolAddress &range,
                                         std::string_view indexName,
                                         RangeExtremum extremum,
                                         SourcePoint fromPoint);
    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         AddrHandle range,
                                         std::string_view indexName,
                                         RangeExtremum extremum,
                                         SourcePoint fromPoint);

    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         const SymbolAddress &range,
                                         std::string_view indexName,
                                         RangeExtremum extremum,
                                         ExprHandle body,
                                         SourcePoint fromPoint);
    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         AddrHandle range,
                                         std::string_view indexName,
                                         RangeExtremum extremum,
                                         ExprHandle body,
                                         SourcePoint fromPoint);

    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         const SymbolAddress &range,
                                         std::string_view indexName,
                                         RangeExtremum extremum,
                                         const SymbolicExpr &body,
                                         SourcePoint fromPoint);
} // namespace acslg::analyzer::symbolic

#endif
