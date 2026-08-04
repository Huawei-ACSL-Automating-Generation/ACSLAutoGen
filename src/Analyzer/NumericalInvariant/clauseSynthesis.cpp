#include "Analyzer/NumericalInvariant/clauseSynthesis.h"

#include <algorithm>
#include <utility>

namespace acslg::analyzer::invariant {
    namespace {
        bool containsClause(const std::vector<symbolic::Expr> &clauses,
                            const symbolic::Expr &candidate) {
            return std::any_of(clauses.begin(), clauses.end(), [&](const auto &existing) {
                return existing.structurallyEqual(candidate);
            });
        }

        ClauseSynthesisStatus mapCombinationStatus(CombinationStatus status) {
            switch (status) {
                case CombinationStatus::Found: return ClauseSynthesisStatus::Found;
                case CombinationStatus::NoInvariant: return ClauseSynthesisStatus::NoInvariant;
                case CombinationStatus::Unknown: return ClauseSynthesisStatus::Unknown;
                case CombinationStatus::Unsupported: return ClauseSynthesisStatus::Unsupported;
                case CombinationStatus::BudgetExceeded:
                    return ClauseSynthesisStatus::BudgetExceeded;
                case CombinationStatus::InvalidModel: return ClauseSynthesisStatus::InvalidModel;
                case CombinationStatus::SolverUnavailable:
                    return ClauseSynthesisStatus::SolverUnavailable;
            }
            return ClauseSynthesisStatus::NoInvariant;
        }

        ClauseSynthesisStatus mapProviderStatus(ClauseProviderStatus status) {
            switch (status) {
                case ClauseProviderStatus::InvalidModel: return ClauseSynthesisStatus::InvalidModel;
                case ClauseProviderStatus::UnsupportedModel:
                    return ClauseSynthesisStatus::Unsupported;
                case ClauseProviderStatus::BudgetExceeded:
                    return ClauseSynthesisStatus::BudgetExceeded;
                case ClauseProviderStatus::Success:
                case ClauseProviderStatus::InvalidResponse:
                case ClauseProviderStatus::TransportError:
                case ClauseProviderStatus::ConfigurationError:
                    return ClauseSynthesisStatus::ProviderError;
            }
            return ClauseSynthesisStatus::ProviderError;
        }

        ClauseSynthesisResult result(ClauseSynthesisStatus status,
                                     std::optional<symbolic::Expr> invariant,
                                     std::vector<symbolic::Expr> clauses,
                                     std::size_t rounds,
                                     std::size_t providerCalls,
                                     std::size_t solverQueries,
                                     std::string reason) {
            return {status,        std::move(invariant), std::move(clauses), rounds,
                    providerCalls, solverQueries,        std::move(reason)};
        }

        std::optional<ClauseFeedback> feedbackFrom(
            const std::optional<VerificationResult> &failure) {
            if (!failure || failure->status != VerificationStatus::Refuted ||
                !failure->failedObligation)
                return std::nullopt;
            return ClauseFeedback{*failure->failedObligation, failure->failedBranch,
                                  failure->counterexample};
        }
    } // namespace

    ClauseSynthesisResult synthesizeInvariantClauses(
        const TransitionModel &model,
        ClauseProvider &provider,
        const std::vector<symbolic::Expr> &initialClauses,
        const ClauseSynthesisLimits &limits) {
        if (auto modelError = validateTransitionModel(model)) {
            return result(ClauseSynthesisStatus::InvalidModel, std::nullopt, {}, 0, 0, 0,
                          *modelError);
        }
        if (limits.maxRounds == 0 || limits.clauseLimits.maxAtomicClauses == 0 ||
            limits.clauseLimits.maxSolverQueries == 0) {
            return result(ClauseSynthesisStatus::BudgetExceeded, std::nullopt, {}, 0, 0, 0,
                          "synthesis budget is zero");
        }

        const auto &factory = model.variables.front().current.factory();
        std::vector<symbolic::Expr> clauses;
        clauses.reserve(limits.clauseLimits.maxAtomicClauses);
        for (const auto &clause : initialClauses) {
            if (&clause.factory() != &factory) {
                return result(ClauseSynthesisStatus::InvalidModel, std::nullopt, {}, 0, 0, 0,
                              "initial clause uses a different expression factory");
            }
            if (!containsClause(clauses, clause))
                clauses.push_back(clause);
        }
        if (clauses.size() > limits.clauseLimits.maxAtomicClauses) {
            return result(ClauseSynthesisStatus::BudgetExceeded, std::nullopt, std::move(clauses),
                          0, 0, 0, "initial atomic clause limit exceeded");
        }

        std::size_t rounds        = 0;
        std::size_t providerCalls = 0;
        std::size_t solverQueries = 0;
        std::optional<ClauseFeedback> feedback;
        std::string lastReason;

        auto combine = [&]() -> std::optional<ClauseSynthesisResult> {
            if (rounds >= limits.maxRounds)
                return result(ClauseSynthesisStatus::BudgetExceeded, std::nullopt,
                              std::move(clauses), rounds, providerCalls, solverQueries,
                              "synthesis round budget exhausted");
            if (solverQueries >= limits.clauseLimits.maxSolverQueries) {
                return result(ClauseSynthesisStatus::BudgetExceeded, std::nullopt,
                              std::move(clauses), rounds, providerCalls, solverQueries,
                              "total solver query budget exhausted");
            }

            auto combinationLimits = limits.clauseLimits;
            combinationLimits.maxSolverQueries -= solverQueries;
            auto combination = combineInvariantClauses(model, clauses, combinationLimits);
            ++rounds;
            solverQueries += combination.solverQueries;
            lastReason = combination.reason;
            feedback   = feedbackFrom(combination.lastFailure);

            if (combination.status == CombinationStatus::Found) {
                return result(ClauseSynthesisStatus::Found, std::move(combination.invariant),
                              std::move(clauses), rounds, providerCalls, solverQueries, {});
            }
            if (combination.status != CombinationStatus::NoInvariant) {
                return result(mapCombinationStatus(combination.status), std::nullopt,
                              std::move(clauses), rounds, providerCalls, solverQueries,
                              std::move(combination.reason));
            }
            return std::nullopt;
        };

        if (!clauses.empty()) {
            if (auto finished = combine())
                return std::move(*finished);
        }

        while (providerCalls < limits.maxProviderCalls) {
            if (rounds >= limits.maxRounds) {
                return result(ClauseSynthesisStatus::BudgetExceeded, std::nullopt,
                              std::move(clauses), rounds, providerCalls, solverQueries,
                              "synthesis round budget exhausted");
            }
            if (clauses.size() >= limits.clauseLimits.maxAtomicClauses) {
                return result(ClauseSynthesisStatus::BudgetExceeded, std::nullopt,
                              std::move(clauses), rounds, providerCalls, solverQueries,
                              "atomic clause budget exhausted");
            }

            const auto remainingClauses = limits.clauseLimits.maxAtomicClauses - clauses.size();
            auto generated              = provider.generate({model, feedback, remainingClauses});
            ++providerCalls;
            if (generated.status != ClauseProviderStatus::Success) {
                return result(mapProviderStatus(generated.status), std::nullopt, std::move(clauses),
                              rounds, providerCalls, solverQueries, std::move(generated.reason));
            }

            bool madeProgress = false;
            for (auto &clause : generated.clauses) {
                if (&clause.factory() != &factory) {
                    return result(ClauseSynthesisStatus::ProviderError, std::nullopt,
                                  std::move(clauses), rounds, providerCalls, solverQueries,
                                  "provider returned a clause from a different expression factory");
                }
                if (containsClause(clauses, clause))
                    continue;
                if (clauses.size() >= limits.clauseLimits.maxAtomicClauses) {
                    return result(ClauseSynthesisStatus::BudgetExceeded, std::nullopt,
                                  std::move(clauses), rounds, providerCalls, solverQueries,
                                  "provider exceeded the requested atomic clause budget");
                }
                clauses.push_back(std::move(clause));
                madeProgress = true;
            }
            if (!madeProgress) {
                return result(ClauseSynthesisStatus::NoInvariant, std::nullopt, std::move(clauses),
                              rounds, providerCalls, solverQueries,
                              "provider produced no new atomic clauses");
            }

            if (auto finished = combine())
                return std::move(*finished);
        }

        return result(ClauseSynthesisStatus::BudgetExceeded, std::nullopt, std::move(clauses),
                      rounds, providerCalls, solverQueries,
                      lastReason.empty() ? "provider call budget exhausted" : lastReason);
    }

} // namespace acslg::analyzer::invariant
