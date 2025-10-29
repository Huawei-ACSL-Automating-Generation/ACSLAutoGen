#ifndef __ACSLG_SRC_ANALYZER_SYMBOLIC_AGGREGATEEXPR_H__
#define __ACSLG_SRC_ANALYZER_SYMBOLIC_AGGREGATEEXPR_H__

#include <memory>
#include <string_view>

#include "expr.h"
#include "Utils/utils.h"

namespace acslg::analyzer::symbolic {
    class SymbolAddress::RangeIndex : public SymbolicExpr {
      public:
        RangeIndex(const RangeIndex &)            = default;
        RangeIndex(RangeIndex &&)                 = default;
        RangeIndex &operator=(const RangeIndex &) = default;
        RangeIndex &operator=(RangeIndex &&)      = default;

        static bool classof(const SymbolicExpr *e) {
            return e->getKind() == ExprKind::K_RangeIndex;
        }

        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeElement(SourcePoint fromPoint) const {
            auto elementFromAddr =
                rangeBase_.fromAddr_
                    ? std::make_unique<SymbolAddress>(
                          rangeBase_.pointeeType_,
                          rangeBase_.fromAddr_.value()->addressClone().into_underlying(),
                          rangeBase_.fromPoint_, clone().into_underlying())
                    : std::make_unique<SymbolAddress>(rangeBase_.pointeeType_, std::nullopt,
                                                      rangeBase_.fromPoint_,
                                                      clone().into_underlying());
            return getSymbol(rangeBase_.pointeeType_, std::move(elementFromAddr), fromPoint);
        }

      private:
        friend SymbolAddress;
        RangeIndex(SymbolAddrBaseInfo rangeBase, std::string_view name)
            : SymbolicExpr(ExprKind::K_RangeIndex, Type{ScalarKind::UInt, 64}),
              rangeBase_(std::move(rangeBase)), name_(name) {}

        // SymbolicExpr
      public:
        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override;
        std::size_t hash() const override;
        bool isLinear() const override { return true; };
        int getMaxDegree() const override { return 0; };
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &,
            const SourcePoint &) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(const GetACSLConfig &,
                                                             std::unordered_set<SourcePoint> &,
                                                             std::optional<SourcePoint>,
                                                             unsigned,
                                                             bool) const override {
            return name_;
        };

      private:
        SymbolAddrBaseInfo rangeBase_;
        std::string name_;
    };

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
                     SourcePoint fromPoint)
            : OverRangeExpr(ExprKind::K_SumOverRange,
                            deriveType(range->getPointeeType()),
                            std::move(range),
                            indexName),
              Symbol(Kind::K_SumOverRange), fromPoint_(std::move(fromPoint)) {}

        // SymbolicExpr
        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override {
            return std::make_unique<SumOverRange>(*this);
        };
        std::string dump() const override;
        bool equal(const SymbolicExpr &) const override { return OverRangeExpr::equal(*this); };
        std::size_t hash() const override {
            return utils::hash_val(SymbolicExpr::getKind(), OverRangeExpr::hash());
        };
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;
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
    };

    template <class F>
    concept CallableFromIndexToExpr =
        std::invocable<F, utils::not_null<std::unique_ptr<SymbolicExpr>>> &&
        std::convertible_to<
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
            : OverRangeExpr(other), pred_(other.pred_->clone().into_underlying()) {}
        QuantifierOverRange(QuantifierOverRange &&) = default;
        QuantifierOverRange &operator=(const QuantifierOverRange &);
        QuantifierOverRange &operator=(QuantifierOverRange &&) = default;

        QuantifierOverRange(utils::not_null<std::unique_ptr<const SymbolAddress>> range,
                            std::string_view indexName,
                            Quantifier quant,
                            CallableFromIndexToExpr auto &&predBuilder)
            : OverRangeExpr(ExprKind::K_SumOverRange,
                            Type{ScalarKind::Bool, 8},
                            std::move(range),
                            indexName),
              quant_(quant),
              pred_(std::invoke(
                  std::forward<decltype(predBuilder)>(predBuilder),
                  /* a placeholder representing index of the range */ range_->getRangeIndex(
                      indexName_))) {}

        // SymbolicExpr
        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override {
            return std::make_unique<QuantifierOverRange>(*this);
        };
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
} // namespace acslg::analyzer::symbolic

#endif
