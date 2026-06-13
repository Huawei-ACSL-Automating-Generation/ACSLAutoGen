/**
 * @file aggregateExpr.h
 * @brief Declares symbolic aggregate expressions such as range sums and quantified ranges.
 */
#ifndef __ACSLG_SRC_ANALYZER_SYMBOLIC_AGGREGATEEXPR_H__
#define __ACSLG_SRC_ANALYZER_SYMBOLIC_AGGREGATEEXPR_H__

#include <memory>
#include <string_view>

#include "expr.h"
#include "Utils/utils.h"

namespace acslg::analyzer::symbolic {
    /**
     * @class SymbolAddress::RangeIndex
     * @brief Placeholder representing the induction variable when reasoning over address ranges.
     */
    class SymbolAddress::RangeIndex : public SymbolicExpr {
      public:
        RangeIndex(const RangeIndex &)            = default;
        RangeIndex(RangeIndex &&)                 = default;
        RangeIndex &operator=(const RangeIndex &) = default;
        RangeIndex &operator=(RangeIndex &&)      = default;

        static bool classof(const SymbolicExpr *e) {
            return e->getKind() == ExprKind::K_RangeIndex;
        }

        RangeIndex(std::string_view name)
            : SymbolicExpr(ExprKind::K_RangeIndex, Type{ScalarKind::UInt, 64}), name_(name) {}

        // SymbolicExpr
      public:
        /**
         * @brief Clone the range index.
         * @return Newly allocated RangeIndex.
         */
        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
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
        /**
         * @brief Substitute using path/point context; range indices are stable and just clone.
         */
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &,
            const SourcePoint &) const override;
        /**
         * @brief Replace the range index with a concrete expression when range is instantiated.
         */
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;
        /**
         * @brief Substitute value expressions using a map keyed by expression hashes.
         */
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedValueExpr(
            const std::unordered_map<size_t, utils::not_null<std::unique_ptr<SymbolicExpr>>>
                &hashToExprMap) const override;

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(const GetACSLConfig &,
                                                             std::unordered_set<SourcePoint> &,
                                                             std::optional<SourcePoint>,
                                                             unsigned,
                                                             bool) const override {
            return name_;
        };

      private:
        std::string name_;
    };

    /**
     * @class OverRangeExpr
     * @brief Base class for expressions that quantify or aggregate over a symbolic address range.
     */
    class OverRangeExpr : public SymbolicExpr {
      public:
        OverRangeExpr(const OverRangeExpr &);
        OverRangeExpr(OverRangeExpr &&) = default;
        OverRangeExpr &operator=(const OverRangeExpr &);
        OverRangeExpr &operator=(OverRangeExpr &&) = default;
        virtual ~OverRangeExpr()                   = default;

        static bool classof(const SymbolicExpr *e) {
            auto k = e->getKind();
            return (k > ExprKind::K_FirstOverRange) && (k < ExprKind::K_LastOverRange);
        }

        // SymbolicExpr
        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override;

      protected:
        OverRangeExpr(ExprKind kind,
                      Type type,
                      utils::not_null<std::unique_ptr<const SymbolAddress>> range,
                      std::string_view indexName)
            : SymbolicExpr(kind, type), range_(std::move(range)), indexName_(indexName) {
            if (!range_->getLength())
                ERROR("`range_` is not a memory *range*.");
        }

        utils::not_null<std::unique_ptr<const SymbolAddress>> range_;
        std::string indexName_;
    };

    class SumOverRange : public OverRangeExpr, public Symbol {
      public:
        static bool classof(const SymbolicExpr *e) {
            return e->getKind() == ExprKind::K_SumOverRange;
        }
        static bool classof(const Symbol *e) {
            return e->getKind() == Symbol::Kind::K_SumOverRange;
        }

        SumOverRange(const SumOverRange &)            = default;
        SumOverRange(SumOverRange &&)                 = default;
        SumOverRange &operator=(const SumOverRange &) = default;
        SumOverRange &operator=(SumOverRange &&)      = default;

        SumOverRange(utils::not_null<std::unique_ptr<const SymbolAddress>> range,
                     std::string_view indexName,
                     SourcePoint fromPoint);

        // SymbolicExpr
        /**
         * @brief Clone the sum-over-range expression.
         */
        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override {
            return std::make_unique<SumOverRange>(*this);
        };
        /// @brief Dump a readable description of the sum.
        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override;
        /**
         * @brief Substitute symbols using a path/point context and rebuild the range.
         */
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        /**
         * @brief Replace the range index with a concrete expression.
         */
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;
        /**
         * @brief Substitute nested value expressions by hash mapping.
         */
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedValueExpr(
            const std::unordered_map<size_t, utils::not_null<std::unique_ptr<SymbolicExpr>>>
                &hashToExprMap) const override;
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
        std::optional<utils::not_null<std::unique_ptr<const Address>>> getFromAddr() const override {
            return std::nullopt;
        }
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; };

      private:
        SourcePoint fromPoint_;

      private:
        struct Init {
            Type type;
            utils::not_null<std::unique_ptr<const SymbolAddress>> range;
        };
        static Init makeInit(utils::not_null<std::unique_ptr<const SymbolAddress>> range);
        SumOverRange(Init init, std::string_view indexName, SourcePoint fromPoint);
    };

    template <class F>
    concept CallableFromIndexToExpr =
        std::invocable<F, utils::not_null<std::unique_ptr<SymbolicExpr>>> && std::convertible_to<
            std::invoke_result_t<F, utils::not_null<std::unique_ptr<SymbolAddress::RangeIndex>>>,
            utils::not_null<std::unique_ptr<const SymbolicExpr>>>;

    class QuantifierOverRange : public OverRangeExpr {
      public:
        static bool classof(const SymbolicExpr *e) {
            return e->getKind() == ExprKind::K_QuantifierOverRange;
        }

        enum class Quantifier {
            ForAll,
            Exist
        };

        QuantifierOverRange(const QuantifierOverRange &other)
            : OverRangeExpr(other), quant_(other.quant_),
              pred_(other.pred_->clone().into_underlying()) {}
        QuantifierOverRange(QuantifierOverRange &&) = default;
        QuantifierOverRange &operator=(const QuantifierOverRange &);
        QuantifierOverRange &operator=(QuantifierOverRange &&) = default;

        QuantifierOverRange(utils::not_null<std::unique_ptr<const SymbolAddress>> range,
                            std::string_view indexName,
                            Quantifier quant,
                            utils::not_null<std::unique_ptr<const SymbolicExpr>> pred)
            : OverRangeExpr(ExprKind::K_QuantifierOverRange,
                            Type{ScalarKind::Bool, 8},
                            std::move(range),
                            indexName),
              quant_(quant), pred_(std::move(pred)) {}

        // SymbolicExpr
        /// @brief Clone the quantified expression.
        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override {
            return std::make_unique<QuantifierOverRange>(*this);
        };
        /// @brief Dump the quantifier, range, and predicate.
        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override {
            return utils::hash_val(getKind(), OverRangeExpr::hash(), quant_, pred_->hash());
        };
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedValueExpr(
            const std::unordered_map<size_t, utils::not_null<std::unique_ptr<SymbolicExpr>>>
                &hashToExprMap) const override;
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
        Quantifier quant_;
        utils::not_null<std::unique_ptr<const SymbolicExpr>> pred_;
    };

    class MaxMinOverRange : public OverRangeExpr, public Symbol {
      public:
        static bool classof(const SymbolicExpr *e) {
            return e->getKind() == ExprKind::K_MaxMinOverRange;
        }
        static bool classof(const Symbol *e) {
            return e->getKind() == Symbol::Kind::K_MaxMinOverRange;
        }

        enum class Extremum {
            Max,
            Min
        };

        MaxMinOverRange(const MaxMinOverRange &other)
            : OverRangeExpr(other), Symbol(other), extremum_(other.extremum_),
              expr_(other.expr_->clone().into_underlying()), fromPoint_(other.fromPoint_) {}
        MaxMinOverRange(MaxMinOverRange &&) = default;
        MaxMinOverRange &operator=(const MaxMinOverRange &);
        MaxMinOverRange &operator=(MaxMinOverRange &&) = default;

        MaxMinOverRange(utils::not_null<std::unique_ptr<const SymbolAddress>> range,
                        std::string_view indexName,
                        Extremum extremum,
                        SourcePoint fromPoint);
        MaxMinOverRange(utils::not_null<std::unique_ptr<const SymbolAddress>> range,
                        std::string_view indexName,
                        Extremum extremum,
                        utils::not_null<std::unique_ptr<const SymbolicExpr>> expr,
                        SourcePoint fromPoint)
            : OverRangeExpr(ExprKind::K_MaxMinOverRange,
                            expr->getValType(),
                            std::move(range),
                            indexName),
              Symbol(Kind::K_MaxMinOverRange), extremum_(extremum), expr_(std::move(expr)),
              fromPoint_(std::move(fromPoint)) {}

        // SymbolicExpr
        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override {
            return std::make_unique<MaxMinOverRange>(*this);
        };
        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override {
            return utils::hash_val(SymbolicExpr::getKind(), OverRangeExpr::hash(), extremum_,
                                   expr_->hash(), fromPoint_.hash());
        };
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedValueExpr(
            const std::unordered_map<size_t, utils::not_null<std::unique_ptr<SymbolicExpr>>>
                &hashToExprMap) const override;
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return -1; }

        // Symbol
      public:
        std::optional<utils::not_null<std::unique_ptr<const Address>>> getFromAddr() const override {
            return std::nullopt;
        }
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; };

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

      private:
        Extremum extremum_;
        utils::not_null<std::unique_ptr<const SymbolicExpr>> expr_;
        SourcePoint fromPoint_;

        static utils::not_null<std::unique_ptr<const SymbolicExpr>> makeDefaultExpr(
            const SymbolAddress &range,
            std::string_view indexName,
            const SourcePoint &fromPoint);

      private:
        struct Init {
            Type type;
            utils::not_null<std::unique_ptr<const SymbolAddress>> range;
        };
        static Init makeInit(utils::not_null<std::unique_ptr<const SymbolAddress>> range);
        MaxMinOverRange(Init init,
                        std::string_view indexName,
                        Extremum extremum,
                        SourcePoint fromPoint);
    };
} // namespace acslg::analyzer::symbolic

#endif
