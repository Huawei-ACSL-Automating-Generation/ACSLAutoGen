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

    class SumOverRangeView {
      public:
        explicit SumOverRangeView(ExprHandle handle);

        static std::optional<SumOverRangeView> tryFrom(ExprHandle handle);

        ExprHandle handle() const { return handle_; }
        SymbolAddressView range() const;
        std::string_view indexName() const;
        SourcePoint fromPoint() const;

      private:
        ExprHandle handle_;
    };

    class QuantifierOverRangeView {
      public:
        explicit QuantifierOverRangeView(ExprHandle handle);

        static std::optional<QuantifierOverRangeView> tryFrom(ExprHandle handle);

        ExprHandle handle() const { return handle_; }
        SymbolAddressView range() const;
        std::string_view indexName() const;
        RangeQuantifier quantifier() const;
        ExprHandle predicate() const;

      private:
        ExprHandle handle_;
    };

    class MaxMinOverRangeView {
      public:
        explicit MaxMinOverRangeView(ExprHandle handle);

        static std::optional<MaxMinOverRangeView> tryFrom(ExprHandle handle);

        ExprHandle handle() const { return handle_; }
        SymbolAddressView range() const;
        std::string_view indexName() const;
        RangeExtremum extremum() const;
        ExprHandle body() const;
        SourcePoint fromPoint() const;

      private:
        ExprHandle handle_;
    };

    ExprHandle makeSumOverRangeHandle(ExprFactory &factory,
                                      AddrHandle range,
                                      std::string_view indexName,
                                      SourcePoint fromPoint);

    ExprHandle makeQuantifierOverRangeHandle(ExprFactory &factory,
                                             AddrHandle range,
                                             std::string_view indexName,
                                             RangeQuantifier quantifier,
                                             ExprHandle predicate);

    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         AddrHandle range,
                                         std::string_view indexName,
                                         RangeExtremum extremum,
                                         SourcePoint fromPoint);

    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         AddrHandle range,
                                         std::string_view indexName,
                                         RangeExtremum extremum,
                                         ExprHandle body,
                                         SourcePoint fromPoint);

    class SumOverRangeExpr : public Expr {
      public:
        SumOverRangeExpr(const Addr &range,
                         std::string_view indexName,
                         SourcePoint fromPoint)
            : Expr(range.factory(), makeSumOverRangeHandle(
                                        range.factory(), range.handle(), indexName,
                                        std::move(fromPoint))) {}

        SumOverRangeExpr(ExprFactory &factory,
                         AddrHandle range,
                         std::string_view indexName,
                         SourcePoint fromPoint)
            : Expr(factory, makeSumOverRangeHandle(
                                factory, range, indexName, std::move(fromPoint))) {}
    };

    class QuantifierOverRangeExpr : public Expr {
      public:
        QuantifierOverRangeExpr(const Addr &range,
                                std::string_view indexName,
                                RangeQuantifier quantifier,
                                const Expr &predicate)
            : Expr(range.factory(), make(range, indexName, quantifier, predicate)) {}

        QuantifierOverRangeExpr(ExprFactory &factory,
                                AddrHandle range,
                                std::string_view indexName,
                                RangeQuantifier quantifier,
                                ExprHandle predicate)
            : Expr(factory, makeQuantifierOverRangeHandle(
                                factory, range, indexName, quantifier, predicate)) {}

      private:
        static ExprHandle make(const Addr &range,
                               std::string_view indexName,
                               RangeQuantifier quantifier,
                               const Expr &predicate) {
            if (&range.factory() != &predicate.factory())
                ERROR("Cannot build a quantifier from different factories.");
            return makeQuantifierOverRangeHandle(
                range.factory(), range.handle(), indexName, quantifier, predicate.handle());
        }
    };

    class MaxMinOverRangeExpr : public Expr {
      public:
        MaxMinOverRangeExpr(const Addr &range,
                            std::string_view indexName,
                            RangeExtremum extremum,
                            SourcePoint fromPoint)
            : Expr(range.factory(), makeMaxMinOverRangeHandle(
                                        range.factory(), range.handle(), indexName, extremum,
                                        std::move(fromPoint))) {}

        MaxMinOverRangeExpr(const Addr &range,
                            std::string_view indexName,
                            RangeExtremum extremum,
                            const Expr &body,
                            SourcePoint fromPoint)
            : Expr(range.factory(), make(range, indexName, extremum, body,
                                         std::move(fromPoint))) {}

        MaxMinOverRangeExpr(ExprFactory &factory,
                            AddrHandle range,
                            std::string_view indexName,
                            RangeExtremum extremum,
                            SourcePoint fromPoint)
            : Expr(factory, makeMaxMinOverRangeHandle(
                                factory, range, indexName, extremum,
                                std::move(fromPoint))) {}

        MaxMinOverRangeExpr(ExprFactory &factory,
                            AddrHandle range,
                            std::string_view indexName,
                            RangeExtremum extremum,
                            ExprHandle body,
                            SourcePoint fromPoint)
            : Expr(factory, makeMaxMinOverRangeHandle(
                                factory, range, indexName, extremum, body,
                                std::move(fromPoint))) {}

      private:
        static ExprHandle make(const Addr &range,
                               std::string_view indexName,
                               RangeExtremum extremum,
                               const Expr &body,
                               SourcePoint fromPoint) {
            if (&range.factory() != &body.factory())
                ERROR("Cannot build a max/min expression from different factories.");
            return makeMaxMinOverRangeHandle(range.factory(), range.handle(), indexName,
                                             extremum, body.handle(), std::move(fromPoint));
        }
    };

} // namespace acslg::analyzer::symbolic

#endif
