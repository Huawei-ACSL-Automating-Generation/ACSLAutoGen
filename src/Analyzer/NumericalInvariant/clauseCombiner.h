#ifndef ACSLG_ANALYZER_NUMERICAL_INVARIANT_CLAUSE_COMBINER_H
#define ACSLG_ANALYZER_NUMERICAL_INVARIANT_CLAUSE_COMBINER_H

#include "Analyzer/NumericalInvariant/transitionModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace acslg::analyzer::invariant {

    struct ClauseLimits {
        std::size_t maxAtomicClauses        = 16;
        std::size_t maxCandidateExpressions = 128;
        std::size_t maxSolverQueries        = 64;
        unsigned solverTimeoutMs            = 250;
        unsigned solverResourceLimit        = 0;
        bool requireChangedStateVariable    = false;
    };

    enum class ObligationKind {
        Initiation,
        Definedness,
        Consecution,
        ExitPost
    };

    enum class VerificationStatus {
        Proved,
        Refuted,
        Unknown,
        Unsupported,
        BudgetExceeded,
        InvalidModel,
        SolverUnavailable
    };

    enum class CounterexampleSymbolKind {
        Current,
        Entry,
        Parameter
    };

    struct CounterexampleBinding {
        CounterexampleSymbolKind kind;
        std::size_t index;
        std::variant<std::int64_t, bool> value;
    };

    struct Counterexample {
        std::vector<CounterexampleBinding> bindings;
    };

    struct VerificationResult {
        VerificationStatus status;
        std::optional<ObligationKind> failedObligation;
        std::optional<std::size_t> failedBranch;
        std::optional<Counterexample> counterexample;
        std::size_t solverQueries;
        std::string reason;
    };

    enum class CombinationStatus {
        Found,
        NoInvariant,
        Unknown,
        Unsupported,
        BudgetExceeded,
        InvalidModel,
        SolverUnavailable
    };

    struct ClauseCombinationResult {
        CombinationStatus status;
        std::optional<symbolic::Expr> invariant;
        std::size_t generatedCandidates;
        std::size_t attemptedCandidates;
        std::size_t counterexampleFilteredCandidates;
        std::size_t solverQueries;
        std::string reason;
        std::optional<VerificationResult> lastFailure = std::nullopt;
    };

    VerificationResult verifyInvariant(const TransitionModel &model,
                                       const symbolic::Expr &invariant,
                                       const ClauseLimits &limits = {});

    bool dependsOnChangedStateVariable(const TransitionModel &model,
                                       const symbolic::Expr &expression);

    ClauseCombinationResult combineInvariantClauses(const TransitionModel &model,
                                                    const std::vector<symbolic::Expr> &atomicClauses,
                                                    const ClauseLimits &limits = {});

} // namespace acslg::analyzer::invariant

#endif
