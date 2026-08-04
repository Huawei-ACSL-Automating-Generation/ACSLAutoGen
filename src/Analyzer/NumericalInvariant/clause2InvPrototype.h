#ifndef ACSLG_ANALYZER_NUMERICAL_INVARIANT_CLAUSE2INV_PROTOTYPE_H
#define ACSLG_ANALYZER_NUMERICAL_INVARIANT_CLAUSE2INV_PROTOTYPE_H

#include "Analyzer/NumericalInvariant/clauseSynthesis.h"
#include "Analyzer/NumericalInvariant/invariantEmitter.h"
#include "Analyzer/NumericalInvariant/transitionModelExtractor.h"

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace acslg::analyzer::invariant {

    enum class Clause2InvPipelineStatus {
        Emitted,
        ExtractionFailed,
        SynthesisFailed,
        EmissionFailed,
        BudgetExceeded
    };

    struct Clause2InvPipelineResult {
        Clause2InvPipelineStatus status;
        std::optional<std::string> acsl;
        std::unordered_set<symbolic::SourcePoint> usedPoints;
        std::optional<TransitionExtractionStatus> extractionStatus;
        std::optional<ClauseSynthesisStatus> synthesisStatus;
        std::optional<InvariantEmissionStatus> emissionStatus;
        std::size_t providerCalls = 0;
        std::size_t solverQueries = 0;
        std::string reason;
    };

    Clause2InvPipelineResult runClause2InvPrototype(
        const Path &concreteEntry,
        const Path &symbolicEntry,
        const ProgramState &symbolicCurrent,
        const symbolic::Expr &loopCondition,
        const symbolic::SourcePoint &currentPoint,
        ClauseProvider &provider,
        const std::vector<symbolic::Expr> &initialClauses  = {},
        const TransitionExtractionLimits &extractionLimits = {},
        const ClauseSynthesisLimits &synthesisLimits       = {});

} // namespace acslg::analyzer::invariant

#endif
