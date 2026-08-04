#ifndef ACSLG_ANALYZER_NUMERICAL_INVARIANT_TRANSITION_MODEL_H
#define ACSLG_ANALYZER_NUMERICAL_INVARIANT_TRANSITION_MODEL_H

#include "Analyzer/Symbolic/expr.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace acslg::analyzer::invariant {

    enum class TransitionIntegerSemantics {
        Mathematical,
        CMachine
    };

    struct TransitionVariable {
        symbolic::Expr current;
        symbolic::Expr entry;
    };

    struct TransitionBranch {
        TransitionBranch(symbolic::Expr branchGuard,
                         std::vector<symbolic::Expr> branchNextValues,
                         std::vector<symbolic::Expr> branchDefinednessConditions = {})
            : guard(std::move(branchGuard)),
              nextValues(std::move(branchNextValues)),
              definednessConditions(std::move(branchDefinednessConditions)) {}

        symbolic::Expr guard;
        std::vector<symbolic::Expr> nextValues;
        std::vector<symbolic::Expr> definednessConditions;
    };

    struct TransitionModel {
        std::vector<TransitionVariable> variables;
        std::vector<symbolic::Expr> parameters;
        symbolic::Expr precondition;
        symbolic::Expr loopCondition;
        std::vector<TransitionBranch> branches;
        std::optional<symbolic::Expr> postcondition;
        TransitionIntegerSemantics integerSemantics = TransitionIntegerSemantics::Mathematical;
    };

    std::optional<std::string> validateTransitionModel(const TransitionModel &model);

} // namespace acslg::analyzer::invariant

#endif
