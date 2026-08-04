#ifndef ACSLG_ANALYZER_NUMERICAL_INVARIANT_POLYNOMIAL_RECURRENCE_H
#define ACSLG_ANALYZER_NUMERICAL_INVARIANT_POLYNOMIAL_RECURRENCE_H

#include "Analyzer/NumericalInvariant/transitionModel.h"

#include <gmpxx.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace acslg::analyzer::invariant {

    struct PolynomialTerm {
        std::vector<unsigned> exponents;
        mpq_class coefficient;
    };

    class Polynomial {
      public:
        Polynomial() = default;
        Polynomial(std::size_t variableCount, std::vector<PolynomialTerm> terms);

        std::size_t variableCount() const { return variableCount_; }
        const std::vector<PolynomialTerm> &terms() const { return terms_; }
        mpq_class coefficient(std::span<const unsigned> exponents) const;
        unsigned degree() const;
        std::string dump() const;

      private:
        std::size_t variableCount_ = 0;
        std::vector<PolynomialTerm> terms_;
    };

    struct RecurrenceLimits {
        std::size_t maxStateVariables    = 4;
        std::size_t maxBranches          = 4;
        std::size_t maxSymbols           = 8;
        unsigned maxDegree               = 2;
        std::size_t maxMonomials         = 64;
        std::size_t maxCandidates        = 128;
        long maxEigenvalueMagnitude      = 16;
        bool requireChangedStateVariable = false;
    };

    enum class RecurrenceStatus {
        Success,
        InvalidModel,
        UnsupportedExpression,
        LimitExceeded
    };

    struct RecurrenceCandidate {
        Polynomial polynomial;
        mpq_class eigenvalue;
        std::optional<symbolic::Expr> invariant;
    };

    struct RecurrenceResult {
        RecurrenceStatus status;
        std::string reason;
        std::vector<RecurrenceCandidate> candidates;
    };

    RecurrenceResult discoverPolynomialRecurrences(const TransitionModel &model,
                                                   const RecurrenceLimits &limits = {});

} // namespace acslg::analyzer::invariant

#endif
