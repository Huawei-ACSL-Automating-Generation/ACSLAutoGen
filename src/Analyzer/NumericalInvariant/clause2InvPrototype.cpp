#include "Analyzer/NumericalInvariant/clause2InvPrototype.h"

#include <utility>

namespace acslg::analyzer::invariant {

    Clause2InvPipelineResult runClause2InvPrototype(
        const Path &concreteEntry,
        const Path &symbolicEntry,
        const ProgramState &symbolicCurrent,
        const symbolic::Expr &loopCondition,
        const symbolic::SourcePoint &currentPoint,
        ClauseProvider &provider,
        const std::vector<symbolic::Expr> &initialClauses,
        const TransitionExtractionLimits &extractionLimits,
        const ClauseSynthesisLimits &synthesisLimits) {
        auto extraction = extractTransitionModel(concreteEntry, symbolicEntry, symbolicCurrent,
                                                 loopCondition, currentPoint, extractionLimits);
        if (!extraction.extracted) {
            return {Clause2InvPipelineStatus::ExtractionFailed,
                    std::nullopt,
                    {},
                    extraction.status,
                    std::nullopt,
                    std::nullopt,
                    0,
                    0,
                    std::move(extraction.reason)};
        }

        auto &extracted = *extraction.extracted;
        const bool machine =
            extracted.model.integerSemantics == TransitionIntegerSemantics::CMachine;
        const std::size_t finalProofQueries = 1 + (machine ? 1 : 0) +
                                              (machine ? 2 : 1) * extracted.model.branches.size() +
                                              (extracted.model.postcondition ? 1 : 0);
        if (synthesisLimits.clauseLimits.maxSolverQueries <= finalProofQueries) {
            return {Clause2InvPipelineStatus::BudgetExceeded,
                    std::nullopt,
                    {},
                    extraction.status,
                    std::nullopt,
                    std::nullopt,
                    0,
                    0,
                    "solver query budget cannot cover synthesis and final emission proof"};
        }

        auto generationLimits = synthesisLimits;
        generationLimits.clauseLimits.maxSolverQueries -= finalProofQueries;
        auto synthesis =
            synthesizeInvariantClauses(extracted.model, provider, initialClauses, generationLimits);
        if (!synthesis.invariant) {
            return {Clause2InvPipelineStatus::SynthesisFailed,
                    std::nullopt,
                    {},
                    extraction.status,
                    synthesis.status,
                    std::nullopt,
                    synthesis.providerCalls,
                    synthesis.solverQueries,
                    std::move(synthesis.reason)};
        }

        auto emissionLimits             = synthesisLimits.clauseLimits;
        emissionLimits.maxSolverQueries = finalProofQueries;
        auto emission =
            verifyAndEmitLoopInvariant(extracted.model, *synthesis.invariant,
                                       extracted.currentPoint, emissionLimits, "acslg_clause2inv");
        const auto totalQueries = synthesis.solverQueries + emission.verification.solverQueries;
        if (!emission.acsl) {
            return {Clause2InvPipelineStatus::EmissionFailed,
                    std::nullopt,
                    {},
                    extraction.status,
                    synthesis.status,
                    emission.status,
                    synthesis.providerCalls,
                    totalQueries,
                    std::move(emission.reason)};
        }

        return {Clause2InvPipelineStatus::Emitted,
                std::move(emission.acsl),
                std::move(emission.usedPoints),
                extraction.status,
                synthesis.status,
                emission.status,
                synthesis.providerCalls,
                totalQueries,
                {}};
    }

} // namespace acslg::analyzer::invariant
