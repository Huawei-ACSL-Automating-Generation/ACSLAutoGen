#ifndef __ACSLG_SRC_ANALYZER_SYMBOLIC_AGGREGATEEXPR_H__
#define __ACSLG_SRC_ANALYZER_SYMBOLIC_AGGREGATEEXPR_H__

#include <memory>

#include "expr.h"
#include "utils.h"

namespace acslg::analyzer::symbolic {

    class OverRangeExpr : public SymbolicExpr, public Symbol {
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
        bool isLinear() const override { return true; };
        int getMaxDegree() const override { return 1; };

        // Symbol
        std::optional<utils::not_null<std::unique_ptr<const Address>>> getFromAddr() const override {
            return std::nullopt;
        }
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; };

      protected:
        class RangeIndex;
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

    class OverRangeExpr::RangeIndex : public SymbolicExpr {
      public:
        RangeIndex(std::string_view name)
            : SymbolicExpr(ExprKind::K_RangeIndex, Type{ScalarKind::UInt, 64}), name_(name) {}
        RangeIndex(const RangeIndex &)            = default;
        RangeIndex(RangeIndex &&)                 = default;
        RangeIndex &operator=(const RangeIndex &) = default;
        RangeIndex &operator=(RangeIndex &&)      = default;

        static bool classof(const SymbolicExpr *e) {
            return e->getKind() == ExprKind::K_RangeIndex;
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
        bool isLinear() const override { return true; };
        int getMaxDegree() const override { return 0; };

      private:
        friend class OverRangeExpr;
        std::string name_;
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
    };
} // namespace acslg::analyzer::symbolic

#endif
