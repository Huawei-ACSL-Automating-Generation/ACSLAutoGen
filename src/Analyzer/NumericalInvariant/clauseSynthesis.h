#ifndef ACSLG_ANALYZER_NUMERICAL_INVARIANT_CLAUSE_SYNTHESIS_H
#define ACSLG_ANALYZER_NUMERICAL_INVARIANT_CLAUSE_SYNTHESIS_H

#include "Analyzer/NumericalInvariant/clauseProvider.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace acslg::analyzer::invariant {

    struct ClauseSynthesisLimits {
        std::size_t maxRounds        = 4;
        std::size_t maxProviderCalls = 4;
        ClauseLimits clauseLimits;
    };

    enum class ClauseSynthesisStatus {
        Found,
        NoInvariant,
        ProviderError,
        Unknown,
        Unsupported,
        BudgetExceeded,
        InvalidModel,
        SolverUnavailable
    };

    struct ClauseSynthesisResult {
        ClauseSynthesisStatus status;
        std::optional<symbolic::Expr> invariant;
        std::vector<symbolic::Expr> atomicClauses;
        std::size_t rounds;
        std::size_t providerCalls;
        std::size_t solverQueries;
        std::string reason;
    };

    ClauseSynthesisResult synthesizeInvariantClauses(
        const TransitionModel &model,
        ClauseProvider &provider,
        const std::vector<symbolic::Expr> &initialClauses = {},
        const ClauseSynthesisLimits &limits               = {});

} // namespace acslg::analyzer::invariant

#endif
