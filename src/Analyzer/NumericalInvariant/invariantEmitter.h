#ifndef ACSLG_ANALYZER_NUMERICAL_INVARIANT_INVARIANT_EMITTER_H
#define ACSLG_ANALYZER_NUMERICAL_INVARIANT_INVARIANT_EMITTER_H

#include "Analyzer/NumericalInvariant/clauseCombiner.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace acslg::analyzer::invariant {

    enum class InvariantEmissionStatus {
        Emitted,
        Refuted,
        Unknown,
        Unsupported,
        BudgetExceeded,
        InvalidModel,
        SolverUnavailable,
        FormattingError
    };

    struct InvariantEmissionResult {
        InvariantEmissionStatus status;
        std::optional<std::string> acsl;
        std::unordered_set<symbolic::SourcePoint> usedPoints;
        VerificationResult verification;
        std::string reason;
    };

    InvariantEmissionResult verifyAndEmitLoopInvariant(const TransitionModel &model,
                                                       const symbolic::Expr &candidate,
                                                       const symbolic::SourcePoint &currentPoint,
                                                       const ClauseLimits &limits = {},
                                                       std::string_view label     = {});

} // namespace acslg::analyzer::invariant

#endif
