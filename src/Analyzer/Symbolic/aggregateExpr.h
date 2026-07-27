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

    class SumOverRangeExpr : public Expr {
      public:
        SumOverRangeExpr(const Addr &range, std::string_view indexName, SourcePoint fromPoint);

        explicit SumOverRangeExpr(const Expr &expression);
        static std::optional<SumOverRangeExpr> tryFrom(const Expr &expression);

        SymbolAddress range() const;
        std::string_view indexName() const;
        SourcePoint fromPoint() const;
    };

    class QuantifierOverRangeExpr : public Expr {
      public:
        QuantifierOverRangeExpr(const Addr &range,
                                std::string_view indexName,
                                RangeQuantifier quantifier,
                                const Expr &predicate);

        explicit QuantifierOverRangeExpr(const Expr &expression);
        static std::optional<QuantifierOverRangeExpr> tryFrom(const Expr &expression);

        SymbolAddress range() const;
        std::string_view indexName() const;
        RangeQuantifier quantifier() const;
        Expr predicate() const;

      private:
        static Expr make(const Addr &range,
                         std::string_view indexName,
                         RangeQuantifier quantifier,
                         const Expr &predicate);
    };

    class MaxMinOverRangeExpr : public Expr {
      public:
        MaxMinOverRangeExpr(const Addr &range,
                            std::string_view indexName,
                            RangeExtremum extremum,
                            SourcePoint fromPoint);

        MaxMinOverRangeExpr(const Addr &range,
                            std::string_view indexName,
                            RangeExtremum extremum,
                            const Expr &body,
                            SourcePoint fromPoint);

        explicit MaxMinOverRangeExpr(const Expr &expression);
        static std::optional<MaxMinOverRangeExpr> tryFrom(const Expr &expression);

        SymbolAddress range() const;
        std::string_view indexName() const;
        RangeExtremum extremum() const;
        Expr body() const;
        SourcePoint fromPoint() const;

      private:
        static Expr make(const Addr &range,
                         std::string_view indexName,
                         RangeExtremum extremum,
                         const Expr &body,
                         SourcePoint fromPoint);
    };

} // namespace acslg::analyzer::symbolic

#endif
