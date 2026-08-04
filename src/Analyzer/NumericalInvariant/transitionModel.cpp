#include "Analyzer/NumericalInvariant/transitionModel.h"

namespace acslg::analyzer::invariant {
    namespace {
        bool sameFactory(const symbolic::Expr &expression, const symbolic::ExprFactory &factory) {
            return &expression.factory() == &factory;
        }
    } // namespace

    std::optional<std::string> validateTransitionModel(const TransitionModel &model) {
        if (model.variables.empty())
            return "transition model has no state variables";
        if (model.branches.empty())
            return "transition model has no branches";

        const auto &factory = model.variables.front().current.factory();
        auto check          = [&](const symbolic::Expr &expression) {
            return sameFactory(expression, factory);
        };

        if (!check(model.precondition) || !check(model.loopCondition))
            return "transition model mixes expression factories";
        if (model.postcondition && !check(*model.postcondition))
            return "transition model mixes expression factories";

        std::vector<symbolic::Expr> leaves;
        leaves.reserve(model.variables.size() + model.parameters.size());
        for (const auto &variable : model.variables) {
            if (!check(variable.current) || !check(variable.entry))
                return "transition model mixes expression factories";
            leaves.push_back(variable.current);
        }
        for (const auto &parameter : model.parameters) {
            if (!check(parameter))
                return "transition model mixes expression factories";
            leaves.push_back(parameter);
        }

        for (std::size_t left = 0; left < leaves.size(); ++left) {
            for (std::size_t right = left + 1; right < leaves.size(); ++right) {
                if (leaves[left].structurallyEqual(leaves[right]))
                    return "transition model contains duplicate symbolic leaves";
            }
        }

        for (const auto &branch : model.branches) {
            if (!check(branch.guard))
                return "transition model mixes expression factories";
            if (branch.nextValues.size() != model.variables.size())
                return "branch update count does not match state variable count";
            for (const auto &next : branch.nextValues) {
                if (!check(next))
                    return "transition model mixes expression factories";
            }
            for (const auto &condition : branch.definednessConditions) {
                if (!check(condition))
                    return "transition model mixes expression factories";
                if (condition.getValType().kind != symbolic::ExprScalarKind::Bool)
                    return "branch definedness condition is not boolean";
            }
        }

        return std::nullopt;
    }
} // namespace acslg::analyzer::invariant
