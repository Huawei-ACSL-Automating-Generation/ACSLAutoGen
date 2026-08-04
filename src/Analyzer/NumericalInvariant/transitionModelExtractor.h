#ifndef ACSLG_ANALYZER_NUMERICAL_INVARIANT_TRANSITION_MODEL_EXTRACTOR_H
#define ACSLG_ANALYZER_NUMERICAL_INVARIANT_TRANSITION_MODEL_EXTRACTOR_H

#include "Analyzer/NumericalInvariant/transitionModel.h"
#include "Analyzer/state.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace acslg::analyzer::invariant {

    enum class CIntegerModel {
        RejectMachineIntegers,
        MathematicalIntegersForPrototype,
        ExactMachineIntegers
    };

    struct TransitionExtractionLimits {
        std::size_t maxVariables   = 8;
        std::size_t maxBranches    = 16;
        CIntegerModel integerModel = CIntegerModel::RejectMachineIntegers;
    };

    enum class TransitionExtractionStatus {
        Success,
        InvalidInput,
        Unsupported,
        LimitExceeded
    };

    struct ExtractedTransitionModel {
        TransitionModel model;
        std::vector<const clang::VarDecl *> variableDeclarations;
        symbolic::SourcePoint currentPoint;
    };

    struct TransitionExtractionResult {
        TransitionExtractionStatus status;
        std::optional<ExtractedTransitionModel> extracted;
        std::string reason;
    };

    TransitionExtractionResult extractTransitionModel(const Path &concreteEntry,
                                                      const Path &symbolicEntry,
                                                      const ProgramState &symbolicCurrent,
                                                      const symbolic::Expr &loopCondition,
                                                      symbolic::SourcePoint currentPoint,
                                                      const TransitionExtractionLimits &limits = {});

} // namespace acslg::analyzer::invariant

#endif
