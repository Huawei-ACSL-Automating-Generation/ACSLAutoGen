#pragma once

#include "../aggregateExpr.h"
#include "exprViews.h"

namespace acslg::analyzer::symbolic::detail {

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

} // namespace acslg::analyzer::symbolic::detail
