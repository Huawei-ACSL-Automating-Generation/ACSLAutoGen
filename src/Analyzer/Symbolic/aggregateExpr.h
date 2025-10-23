#ifndef __ACSLG_SRC_ANALYZER_SYMBOLIC_AGGREGATEEXPR_H__
#define __ACSLG_SRC_ANALYZER_SYMBOLIC_AGGREGATEEXPR_H__

#include <memory>

#include "expr.h"
#include "utils.h"

namespace acslg::analyzer::symbolic {

    class OverRangeExpr : public SymbolicExpr, public Symbol {
      protected:
        class RangeElement;

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

        utils::not_null<std::unique_ptr<RangeElement>> getElement() const {
            return std::make_unique<RangeElement>(indexName_,
                                                  std::make_unique<const SymbolAddress>(*range_));
        };

        // SymbolicExpr
        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override;
        bool isLinear() const override { return true; };
        int getMaxDegree() const override { return 1; };

        // Symbol
        std::optional<utils::not_null<std::unique_ptr<const Address>>> getFromAddr() const override {
            return std::nullopt;
        }
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; };

      protected:
        OverRangeExpr(ExprKind kind,
                      Type type,
                      utils::not_null<std::unique_ptr<const SymbolAddress>> range,
                      std::string_view indexName,
                      SourcePoint fromPoint)
            : SymbolicExpr(kind, type), range_(std::move(range)), indexName_(indexName),
              fromPoint_(std::move(fromPoint)) {
            if (!range_->getLength())
                ERROR("`range_` is not a memory *range*.");
        }

        utils::not_null<std::unique_ptr<const SymbolAddress>> range_;
        std::string indexName_;
        SourcePoint fromPoint_;
    };

    class OverRangeExpr::RangeElement : public SymbolicExpr {
      public:
        RangeElement(std::string_view name,
                     utils::not_null<std::unique_ptr<const SymbolAddress>> range)
            : SymbolicExpr(ExprKind::K_RangeElement, Type{ScalarKind::UInt, 64}), indexName_(name),
              range_(std::move(range)) {
            if (range_->getLength() == std::nullopt)
                ERROR("range_ is not a memory *range*.");
        }
        RangeElement(const RangeElement &other)
            : SymbolicExpr(other), indexName_(other.indexName_),
              range_(std::make_unique<const SymbolAddress>(*other.range_)) {}
        RangeElement(RangeElement &&) = default;
        RangeElement &operator=(const RangeElement &);
        RangeElement &operator=(RangeElement &&) = default;

        static bool classof(const SymbolicExpr *e) {
            return e->getKind() == ExprKind::K_RangeElement;
        }

        // SymbolicExpr
        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        std::optional<std::string> regularForm(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        bool isLinear() const override { return true; };
        int getMaxDegree() const override { return 0; };

      private:
        friend class OverRangeExpr;
        std::string indexName_;
        utils::not_null<std::unique_ptr<const SymbolAddress>> range_;
    };

    class SumOverRange : public OverRangeExpr {
      public:
        static bool classof(const SymbolicExpr *e) {
            return e->getKind() == ExprKind::K_SumOverRange;
        }

        SumOverRange(const SumOverRange &)            = default;
        SumOverRange(SumOverRange &&)                 = default;
        SumOverRange &operator=(const SumOverRange &) = default;
        SumOverRange &operator=(SumOverRange &&)      = default;

        SumOverRange(utils::not_null<std::unique_ptr<const SymbolAddress>> range,
                     std::string_view indexName,
                     SourcePoint fromPoint)
            : OverRangeExpr(ExprKind::K_SumOverRange,
                            deriveType(range->getPointeeType()),
                            std::move(range),
                            indexName,
                            std::move(fromPoint)) {}

        // SymbolicExpr
        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override {
            return std::make_unique<SumOverRange>(*this);
        };
        std::string dump() const override;
        std::optional<std::string> regularForm(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        bool equal(const SymbolicExpr &) const override { return OverRangeExpr::equal(*this); };
        std::size_t hash() const override {
            return utils::hash_val(getKind(), OverRangeExpr::hash());
        };
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
    };

    template <class F>
    concept CallableFromExprToExpr =
        std::invocable<F, utils::not_null<std::unique_ptr<SymbolicExpr>>> &&
        std::convertible_to<std::invoke_result_t<F, utils::not_null<std::unique_ptr<SymbolicExpr>>>,
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
            : OverRangeExpr(other), pred_(other.pred_->clone().into_underlying()) {}
        QuantifierOverRange(QuantifierOverRange &&) = default;
        QuantifierOverRange &operator=(const QuantifierOverRange &);
        QuantifierOverRange &operator=(QuantifierOverRange &&) = default;

        QuantifierOverRange(utils::not_null<std::unique_ptr<const SymbolAddress>> range,
                            std::string_view indexName,
                            SourcePoint fromPoint,
                            Quantifier quant,
                            CallableFromExprToExpr auto &&predBuilder)
            : OverRangeExpr(ExprKind::K_SumOverRange,
                            Type{ScalarKind::Bool, 8},
                            std::move(range),
                            indexName,
                            std::move(fromPoint)),
              quant_(quant),
              pred_(std::invoke(
                  std::forward<decltype(predBuilder)>(predBuilder),
                  /* a placeholder representing an element of the range */ getElement())) {}

        // SymbolicExpr
        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override {
            return std::make_unique<QuantifierOverRange>(*this);
        };
        std::string dump() const override;
        std::optional<std::string> regularForm(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override {
            return utils::hash_val(getKind(), OverRangeExpr::hash(), quant_, pred_->hash());
        };
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;

      private:
        Quantifier quant_;
        utils::not_null<std::unique_ptr<const SymbolicExpr>> pred_;
    };
} // namespace acslg::analyzer::symbolic

#endif
