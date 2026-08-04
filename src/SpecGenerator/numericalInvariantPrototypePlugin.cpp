#include "Analyzer/NumericalInvariant/clause2InvPrototype.h"
#include "Analyzer/NumericalInvariant/polynomialRecurrence.h"
#include "specGenerator.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace acslg::spec_generator {
    namespace invariant = analyzer::invariant;
    namespace symbolic  = analyzer::symbolic;

    namespace {
        std::optional<symbolic::Expr> evaluateLoopCondition(
            const analyzer::ProgramState &loopEntry,
            const LoopInfo::EntryAndCurrentInfo &state,
            const clang::Expr *conditionExpression) {
            symbolic::Expr condition{symbolic::LiteralExpr{loopEntry.getExprFactory(), true}};
            if (!conditionExpression)
                return condition;

            auto conditionPath = state.symbolicLoopEntry->getPaths().front()->clone();
            auto evaluated     = conditionPath->evalExpr(conditionExpression);
            if (!evaluated.first.empty() || evaluated.second.size() != 1)
                return std::nullopt;
            return std::move(evaluated.second.front());
        }

        class VerifiedPolynomialInvariantPlugin final : public PathInsensitiveLoopInvPlugin {
          public:
            explicit VerifiedPolynomialInvariantPlugin(std::string id) : id_(std::move(id)) {}

            std::string_view id() const override { return id_; }

            GenResultType generate(const analyzer::ProgramState &,
                                   const analyzer::ProgramState &loopEntry,
                                   const LoopInfo &loopInfo) const override {
                GenResultType empty;
                if (!loopInfo.entryAndCurrentInfo || loopEntry.getPaths().size() != 1)
                    return empty;

                const auto &state = *loopInfo.entryAndCurrentInfo;
                if (state.symbolicLoopEntry->getPaths().size() != 1 ||
                    state.symbolicLoopCurrent->getPaths().empty())
                    return empty;

                auto condition = evaluateLoopCondition(loopEntry, state, loopInfo.condExpr);
                if (!condition)
                    return empty;

                invariant::TransitionExtractionLimits extractionLimits;
                extractionLimits.integerModel = invariant::CIntegerModel::ExactMachineIntegers;
                auto extraction               = invariant::extractTransitionModel(
                    *loopEntry.getPaths().front(), *state.symbolicLoopEntry->getPaths().front(),
                    *state.symbolicLoopCurrent, *condition, state.loopEntryPoint, extractionLimits);
                if (!extraction.extracted) {
                    DEBUG("Verified polynomial invariant extraction skipped: " + extraction.reason);
                    return empty;
                }

                invariant::RecurrenceLimits recurrenceLimits;
                recurrenceLimits.maxCandidates               = 24;
                recurrenceLimits.requireChangedStateVariable = true;
                auto recurrences = invariant::discoverPolynomialRecurrences(
                    extraction.extracted->model, recurrenceLimits);
                if (recurrences.status != invariant::RecurrenceStatus::Success) {
                    DEBUG("Verified polynomial invariant discovery skipped: " + recurrences.reason);
                    return empty;
                }
                DEBUG("Verified polynomial invariant discovery produced " +
                      std::to_string(recurrences.candidates.size()) + " candidates");

                std::vector<const invariant::RecurrenceCandidate *> candidates;
                for (const auto &candidate : recurrences.candidates)
                    if (candidate.invariant)
                        candidates.push_back(&candidate);
                std::stable_sort(
                    candidates.begin(), candidates.end(), [](const auto *left, const auto *right) {
                        return std::tuple{left->polynomial.terms().size(),
                                          left->polynomial.degree(), left->polynomial.dump()} <
                               std::tuple{right->polynomial.terms().size(),
                                          right->polynomial.degree(), right->polynomial.dump()};
                    });

                constexpr std::size_t maxAttemptedCandidates = 12;
                constexpr std::size_t maxEmittedCandidates   = 1;
                invariant::ClauseLimits proofLimits;
                proofLimits.maxSolverQueries    = 8;
                proofLimits.solverTimeoutMs     = 250;
                proofLimits.solverResourceLimit = 1000000;

                std::optional<symbolic::Expr> indexMonotonicity;
                if (loopInfo.indexInfo && loopInfo.indexInfo->indexPattern.step != 0) {
                    const auto &indexCurrent = loopInfo.indexInfo->indexSymbolicValue;
                    const auto variable      = std::find_if(
                        extraction.extracted->model.variables.begin(),
                        extraction.extracted->model.variables.end(), [&](const auto &candidate) {
                            return candidate.current.structurallyEqual(indexCurrent);
                        });
                    if (variable != extraction.extracted->model.variables.end()) {
                        indexMonotonicity = loopInfo.indexInfo->indexPattern.step > 0
                                                ? variable->current.greaterEqual(variable->entry)
                                                : variable->current.lessEqual(variable->entry);
                    }
                }

                std::set<std::string> emittedClauses;
                std::size_t attempted = 0;
                for (const auto *candidate : candidates) {
                    if (attempted >= maxAttemptedCandidates ||
                        emittedClauses.size() >= maxEmittedCandidates)
                        break;
                    ++attempted;
                    auto proofCandidate = *candidate->invariant;
                    if (indexMonotonicity)
                        proofCandidate = indexMonotonicity->logicalAnd(proofCandidate);
                    auto emission = invariant::verifyAndEmitLoopInvariant(
                        extraction.extracted->model, proofCandidate,
                        extraction.extracted->currentPoint, proofLimits, "acslg_polynomial");
                    if (emission.acsl) {
                        emittedClauses.insert(std::move(*emission.acsl));
                        empty.acslUsedPoints.insert(
                            std::make_move_iterator(emission.usedPoints.begin()),
                            std::make_move_iterator(emission.usedPoints.end()));
                    } else {
                        std::string diagnostic =
                            "Verified polynomial invariant candidate rejected with status " +
                            std::to_string(static_cast<int>(emission.status));
                        if (emission.verification.failedObligation) {
                            diagnostic +=
                                ", obligation " + std::to_string(static_cast<int>(
                                                      *emission.verification.failedObligation));
                        }
                        if (!emission.reason.empty())
                            diagnostic += ": " + emission.reason;
                        DEBUG(diagnostic);
                    }
                }
                if (emittedClauses.empty())
                    return empty;

                std::string acsl;
                for (const auto &clause : emittedClauses) {
                    acsl += clause;
                    acsl += "\n";
                }
                empty.acsl = std::move(acsl);
                return empty;
            }

          private:
            std::string id_;
        };

        class Clause2InvPrototypePlugin final : public PathInsensitiveLoopInvPlugin {
          public:
            explicit Clause2InvPrototypePlugin(std::string id) : id_(std::move(id)) {}

            std::string_view id() const override { return id_; }

            GenResultType generate(const analyzer::ProgramState &,
                                   const analyzer::ProgramState &loopEntry,
                                   const LoopInfo &loopInfo) const override {
                GenResultType empty;
                if (!clause2InvPrototypeEnabled())
                    return empty;
                if (!loopInfo.entryAndCurrentInfo || loopEntry.getPaths().size() != 1)
                    return empty;

                const auto &state = *loopInfo.entryAndCurrentInfo;
                if (state.symbolicLoopEntry->getPaths().size() != 1 ||
                    state.symbolicLoopCurrent->getPaths().empty())
                    return empty;

                auto condition = evaluateLoopCondition(loopEntry, state, loopInfo.condExpr);
                if (!condition)
                    return empty;

                std::string configurationError;
                auto providerConfig =
                    invariant::OpenAICompatibleProviderConfig::fromEnvironment(configurationError);
                if (!providerConfig) {
                    WARN("Clause2Inv provider is not configured: " + configurationError);
                    return empty;
                }

                invariant::CurlProcessTransport transport;
                invariant::OpenAICompatibleClauseProvider provider{transport,
                                                                   std::move(*providerConfig)};
                invariant::TransitionExtractionLimits extractionLimits;
                extractionLimits.integerModel = invariant::CIntegerModel::ExactMachineIntegers;
                invariant::ClauseSynthesisLimits synthesisLimits;
                synthesisLimits.clauseLimits.requireChangedStateVariable = true;
                const auto result = invariant::runClause2InvPrototype(
                    *loopEntry.getPaths().front(), *state.symbolicLoopEntry->getPaths().front(),
                    *state.symbolicLoopCurrent, *condition, state.loopEntryPoint, provider, {},
                    extractionLimits, synthesisLimits);
                if (!result.acsl) {
                    WARN("Clause2Inv prototype produced no invariant: " + result.reason);
                    return empty;
                }

                return {.acsl                         = std::move(result.acsl),
                        .acslUsedPoints               = std::move(result.usedPoints),
                        .globalNormalPathPostInfo     = {},
                        .globalInterruptPathsPostInfo = {}};
            }

          private:
            std::string id_;
        };
    } // namespace

    REGISTER_ACSL_PLUGIN(VerifiedPolynomialInvariantPlugin, "VerifiedPolynomialInvariantPlugin");
    REGISTER_ACSL_PLUGIN(Clause2InvPrototypePlugin, "Clause2InvPrototypePlugin");

} // namespace acslg::spec_generator
