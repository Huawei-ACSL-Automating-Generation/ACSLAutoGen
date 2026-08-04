#include "Analyzer/NumericalInvariant/invariantEmitter.h"

#include <cctype>
#include <utility>

namespace acslg::analyzer::invariant {
    namespace {
        InvariantEmissionStatus emissionStatus(VerificationStatus status) {
            switch (status) {
                case VerificationStatus::Proved: return InvariantEmissionStatus::Emitted;
                case VerificationStatus::Refuted: return InvariantEmissionStatus::Refuted;
                case VerificationStatus::Unknown: return InvariantEmissionStatus::Unknown;
                case VerificationStatus::Unsupported: return InvariantEmissionStatus::Unsupported;
                case VerificationStatus::BudgetExceeded:
                    return InvariantEmissionStatus::BudgetExceeded;
                case VerificationStatus::InvalidModel: return InvariantEmissionStatus::InvalidModel;
                case VerificationStatus::SolverUnavailable:
                    return InvariantEmissionStatus::SolverUnavailable;
            }
            return InvariantEmissionStatus::InvalidModel;
        }

        std::string formattingError(symbolic::ACSLError error) {
            switch (error) {
                case symbolic::ACSLError::HeapAddress:
                    return "candidate contains a heap address that cannot be emitted";
                case symbolic::ACSLError::PartiallyModifiedStruct:
                    return "candidate contains a partially modified structure";
                case symbolic::ACSLError::UnknownExpr:
                    return "candidate contains an unknown expression";
            }
            return "candidate cannot be emitted as ACSL";
        }

        bool validLabel(std::string_view label) {
            if (label.empty())
                return true;
            const auto validFirst = [](unsigned char character) {
                return std::isalpha(character) || character == '_';
            };
            const auto validRest = [](unsigned char character) {
                return std::isalnum(character) || character == '_';
            };
            if (!validFirst(static_cast<unsigned char>(label.front())))
                return false;
            for (const auto character : label.substr(1))
                if (!validRest(static_cast<unsigned char>(character)))
                    return false;
            return true;
        }
    } // namespace

    InvariantEmissionResult verifyAndEmitLoopInvariant(const TransitionModel &model,
                                                       const symbolic::Expr &candidate,
                                                       const symbolic::SourcePoint &currentPoint,
                                                       const ClauseLimits &limits,
                                                       std::string_view label) {
        if (!validLabel(label)) {
            return {InvariantEmissionStatus::FormattingError,
                    std::nullopt,
                    {},
                    {},
                    "invariant label is not a valid ACSL identifier"};
        }

        auto verification = verifyInvariant(model, candidate, limits);
        const auto status = emissionStatus(verification.status);
        if (verification.status != VerificationStatus::Proved) {
            const auto reason = verification.reason;
            return {status, std::nullopt, {}, std::move(verification), reason};
        }

        symbolic::ACSLConfig config;
        config.UnknownExprAsError = true;
        auto rendered             = candidate.getACSL(config, currentPoint);
        if (!rendered) {
            auto reason = formattingError(rendered.error());
            return {InvariantEmissionStatus::FormattingError,
                    std::nullopt,
                    {},
                    std::move(verification),
                    std::move(reason)};
        }

        auto [expression, usedPoints] = std::move(rendered.value());
        auto prefix                   = std::string{"loop invariant "};
        if (!label.empty())
            prefix += std::string{label} + ": ";
        return {InvariantEmissionStatus::Emitted,
                std::move(prefix) + std::move(expression) + ";",
                std::move(usedPoints),
                std::move(verification),
                {}};
    }

} // namespace acslg::analyzer::invariant
