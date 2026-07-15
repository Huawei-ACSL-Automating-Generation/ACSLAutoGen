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
        static std::optional<SumOverRangeView> tryFrom(const SymbolicExpr &expr);

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
        static std::optional<QuantifierOverRangeView> tryFrom(const SymbolicExpr &expr);

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
        static std::optional<MaxMinOverRangeView> tryFrom(const SymbolicExpr &expr);

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

} // namespace acslg::analyzer::symbolic

#endif
