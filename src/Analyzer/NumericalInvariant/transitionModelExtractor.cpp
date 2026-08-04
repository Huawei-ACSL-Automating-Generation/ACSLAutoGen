#include "Analyzer/NumericalInvariant/transitionModelExtractor.h"

#include <algorithm>
#include <tuple>
#include <utility>

namespace acslg::analyzer::invariant {
    namespace {
        TransitionExtractionResult failure(TransitionExtractionStatus status, std::string reason) {
            return {status, std::nullopt, std::move(reason)};
        }

        bool supportedScalar(const symbolic::Expr &expression) {
            switch (expression.getValType().kind) {
                case symbolic::ExprScalarKind::Int:
                case symbolic::ExprScalarKind::UInt:
                case symbolic::ExprScalarKind::Bool: return true;
                case symbolic::ExprScalarKind::Void:
                case symbolic::ExprScalarKind::Structure: return false;
            }
            return false;
        }

        bool containsExpression(const std::vector<symbolic::Expr> &expressions,
                                const symbolic::Expr &candidate) {
            return std::any_of(expressions.begin(), expressions.end(), [&](const auto &existing) {
                return existing.structurallyEqual(candidate);
            });
        }

        std::optional<symbolic::Expr> readVariable(const Path &path,
                                                   const clang::VarDecl *declaration) {
            const auto canonical = declaration->getCanonicalDecl();
            const auto address   = path.getVarAddr().find(canonical);
            if (address == path.getVarAddr().end())
                return std::nullopt;
            return path.getMemoryState().read(address->second);
        }

        std::vector<symbolic::Expr> orderedConditions(const PathConditions &conditions) {
            std::vector<symbolic::Expr> ordered(conditions.begin(), conditions.end());
            std::stable_sort(ordered.begin(), ordered.end(),
                             [](const auto &left, const auto &right) {
                                 return std::tuple{left.hash(), left.dump()} <
                                        std::tuple{right.hash(), right.dump()};
                             });
            return ordered;
        }

        symbolic::Expr conjunction(symbolic::ExprFactory &factory,
                                   std::vector<symbolic::Expr> expressions) {
            if (expressions.empty())
                return symbolic::LiteralExpr{factory, true};
            auto result = std::move(expressions.front());
            for (std::size_t index = 1; index < expressions.size(); ++index)
                result = result.logicalAnd(expressions[index]);
            return result;
        }

        bool isSupportedOperation(symbolic::BinaryOp operation,
                                  bool allowExtendedMachineOperations) {
            switch (operation) {
                case symbolic::BinaryOp::Add:
                case symbolic::BinaryOp::Subtract:
                case symbolic::BinaryOp::Multiply:
                case symbolic::BinaryOp::LessThan:
                case symbolic::BinaryOp::GreaterThan:
                case symbolic::BinaryOp::LessEqual:
                case symbolic::BinaryOp::GreaterEqual:
                case symbolic::BinaryOp::Equal:
                case symbolic::BinaryOp::NotEqual:
                case symbolic::BinaryOp::LogicalAnd:
                case symbolic::BinaryOp::LogicalOr: return true;
                case symbolic::BinaryOp::Divide:
                case symbolic::BinaryOp::Remainder:
                case symbolic::BinaryOp::ShiftLeft:
                case symbolic::BinaryOp::ShiftRight:
                case symbolic::BinaryOp::BitAnd:
                case symbolic::BinaryOp::BitOr:
                case symbolic::BinaryOp::BitXor: return allowExtendedMachineOperations;
                default: return false;
            }
        }

        bool usesOnlyCurrentSymbols(const symbolic::Expr &expression,
                                    const std::vector<symbolic::Expr> &currentSymbols,
                                    bool allowExtendedMachineOperations) {
            if (containsExpression(currentSymbols, expression) ||
                symbolic::LiteralExpr::tryFrom(expression))
                return true;
            if (auto unary = symbolic::UnaryExpr::tryFrom(expression)) {
                const auto operation = unary->operation();
                if (operation != symbolic::UnaryOp::Plus && operation != symbolic::UnaryOp::Minus &&
                    operation != symbolic::UnaryOp::LogicalNot &&
                    !(allowExtendedMachineOperations &&
                      operation == symbolic::UnaryOp::BitwiseNot))
                    return false;
                return usesOnlyCurrentSymbols(unary->operand(), currentSymbols,
                                              allowExtendedMachineOperations);
            }
            if (auto cast = symbolic::CastExpr::tryFrom(expression)) {
                return allowExtendedMachineOperations &&
                       usesOnlyCurrentSymbols(cast->operand(), currentSymbols,
                                              allowExtendedMachineOperations);
            }
            if (auto binary = symbolic::BinaryExpr::tryFrom(expression)) {
                return isSupportedOperation(binary->operation(), allowExtendedMachineOperations) &&
                       usesOnlyCurrentSymbols(binary->left(), currentSymbols,
                                              allowExtendedMachineOperations) &&
                       usesOnlyCurrentSymbols(binary->right(), currentSymbols,
                                              allowExtendedMachineOperations);
            }
            return false;
        }

        bool supportedMachineType(symbolic::ExprType type) {
            if (type.kind == symbolic::ExprScalarKind::Bool)
                return type.bitWidth == 1;
            return (type.kind == symbolic::ExprScalarKind::Int ||
                    type.kind == symbolic::ExprScalarKind::UInt) &&
                   (type.bitWidth == 8 || type.bitWidth == 16 || type.bitWidth == 32 ||
                    type.bitWidth == 64);
        }

        std::optional<symbolic::ExprType> exactMachineExpressionType(
            const symbolic::Expr &expression,
            const std::vector<symbolic::Expr> &currentSymbols,
            std::string &reason) {
            for (const auto &current : currentSymbols) {
                if (expression.structurallyEqual(current)) {
                    if (!supportedMachineType(current.getValType())) {
                        reason = "machine model currently supports only bool and "
                                 "8/16/32/64-bit integers";
                        return std::nullopt;
                    }
                    return current.getValType();
                }
            }
            if (symbolic::LiteralExpr::tryFrom(expression)) {
                if (!supportedMachineType(expression.getValType())) {
                    reason = "machine model contains an unsupported literal type";
                    return std::nullopt;
                }
                return expression.getValType();
            }
            if (auto cast = symbolic::CastExpr::tryFrom(expression)) {
                auto source =
                    exactMachineExpressionType(cast->operand(), currentSymbols, reason);
                if (!source)
                    return std::nullopt;
                const auto target = cast->targetType();
                if (!supportedMachineType(target)) {
                    reason = "machine model contains an unsupported cast target type";
                    return std::nullopt;
                }
                if (target.kind == symbolic::ExprScalarKind::Int &&
                    source->kind == symbolic::ExprScalarKind::UInt &&
                    source->bitWidth >= target.bitWidth) {
                    reason = "unsigned-to-signed conversion is implementation-defined";
                    return std::nullopt;
                }
                return target;
            }
            if (auto unary = symbolic::UnaryExpr::tryFrom(expression)) {
                auto operand = exactMachineExpressionType(unary->operand(), currentSymbols, reason);
                if (!operand)
                    return std::nullopt;
                switch (unary->operation()) {
                    case symbolic::UnaryOp::Plus:
                    case symbolic::UnaryOp::Minus:
                    case symbolic::UnaryOp::BitwiseNot:
                        if (operand->kind == symbolic::ExprScalarKind::Bool) {
                            reason = "arithmetic unary operation has a boolean operand";
                            return std::nullopt;
                        }
                        if (operand->bitWidth < 32) {
                            reason = "narrow unary operation requires integer promotion";
                            return std::nullopt;
                        }
                        return operand;
                    case symbolic::UnaryOp::LogicalNot:
                        if (operand->kind != symbolic::ExprScalarKind::Bool) {
                            reason = "integer-to-boolean conversion is not lowered";
                            return std::nullopt;
                        }
                        return symbolic::ExprType{symbolic::ExprScalarKind::Bool, 1};
                    default:
                        reason = "machine model contains an unsupported unary operation";
                        return std::nullopt;
                }
            }
            if (auto binary = symbolic::BinaryExpr::tryFrom(expression)) {
                auto left = exactMachineExpressionType(binary->left(), currentSymbols, reason);
                if (!left)
                    return std::nullopt;
                auto right = exactMachineExpressionType(binary->right(), currentSymbols, reason);
                if (!right)
                    return std::nullopt;

                switch (binary->operation()) {
                    case symbolic::BinaryOp::LogicalAnd:
                    case symbolic::BinaryOp::LogicalOr:
                        if (left->kind != symbolic::ExprScalarKind::Bool ||
                            right->kind != symbolic::ExprScalarKind::Bool) {
                            reason = "logical operation requires lowered boolean operands";
                            return std::nullopt;
                        }
                        return symbolic::ExprType{symbolic::ExprScalarKind::Bool, 1};
                    case symbolic::BinaryOp::Equal:
                    case symbolic::BinaryOp::NotEqual:
                    case symbolic::BinaryOp::LessThan:
                    case symbolic::BinaryOp::GreaterThan:
                    case symbolic::BinaryOp::LessEqual:
                    case symbolic::BinaryOp::GreaterEqual:
                        if (*left != *right) {
                            reason = "comparison requires an unsupported C integer conversion";
                            return std::nullopt;
                        }
                        if (left->kind != symbolic::ExprScalarKind::Bool &&
                            left->bitWidth < 32) {
                            reason = "narrow comparison requires integer promotion";
                            return std::nullopt;
                        }
                        return symbolic::ExprType{symbolic::ExprScalarKind::Bool, 1};
                    case symbolic::BinaryOp::Add:
                    case symbolic::BinaryOp::Subtract:
                    case symbolic::BinaryOp::Multiply:
                    case symbolic::BinaryOp::Divide:
                    case symbolic::BinaryOp::Remainder:
                        if (*left != *right || left->kind == symbolic::ExprScalarKind::Bool) {
                            reason = "arithmetic requires an unsupported C integer conversion";
                            return std::nullopt;
                        }
                        if (left->bitWidth < 32) {
                            reason = "narrow arithmetic requires integer promotion";
                            return std::nullopt;
                        }
                        return left;
                    case symbolic::BinaryOp::ShiftLeft:
                    case symbolic::BinaryOp::ShiftRight:
                        if (left->kind == symbolic::ExprScalarKind::Bool ||
                            right->kind == symbolic::ExprScalarKind::Bool ||
                            (left->bitWidth != 32 && left->bitWidth != 64) ||
                            (right->bitWidth != 32 && right->bitWidth != 64)) {
                            reason =
                                "shift requires independently promoted 32/64-bit integer operands";
                            return std::nullopt;
                        }
                        if (binary->operation() == symbolic::BinaryOp::ShiftRight &&
                            left->kind == symbolic::ExprScalarKind::Int) {
                            reason = "signed right shift has implementation-defined semantics";
                            return std::nullopt;
                        }
                        return left;
                    case symbolic::BinaryOp::BitAnd:
                    case symbolic::BinaryOp::BitOr:
                    case symbolic::BinaryOp::BitXor:
                        if (*left != *right || left->kind == symbolic::ExprScalarKind::Bool) {
                            reason = "bitwise operation requires matching integer types";
                            return std::nullopt;
                        }
                        if (left->bitWidth < 32) {
                            reason = "narrow bitwise operation requires integer promotion";
                            return std::nullopt;
                        }
                        return left;
                    default:
                        reason = "machine model contains an unsupported binary operation";
                        return std::nullopt;
                }
            }
            reason = "machine model contains an unsupported expression leaf";
            return std::nullopt;
        }

        void collectParameters(const symbolic::Expr &expression,
                               const std::vector<symbolic::Expr> &currentSymbols,
                               std::vector<symbolic::Expr> &parameters) {
            for (const auto &symbol : expression.collectUsedSymbols()) {
                if (containsExpression(currentSymbols, symbol) ||
                    containsExpression(parameters, symbol))
                    continue;
                parameters.push_back(symbol);
            }
        }
    } // namespace

    TransitionExtractionResult extractTransitionModel(const Path &concreteEntry,
                                                      const Path &symbolicEntry,
                                                      const ProgramState &symbolicCurrent,
                                                      const symbolic::Expr &loopCondition,
                                                      symbolic::SourcePoint currentPoint,
                                                      const TransitionExtractionLimits &limits) {
        auto &factory = symbolicEntry.getExprFactory();
        if (&concreteEntry.getExprFactory() != &factory ||
            &symbolicCurrent.getExprFactory() != &factory || &loopCondition.factory() != &factory) {
            return failure(TransitionExtractionStatus::InvalidInput,
                           "transition snapshots use different expression factories");
        }
        if (symbolicCurrent.getPaths().empty())
            return failure(TransitionExtractionStatus::InvalidInput,
                           "symbolic current state has no active paths");
        if (symbolicCurrent.getPaths().size() > limits.maxBranches)
            return failure(TransitionExtractionStatus::LimitExceeded,
                           "transition branch limit exceeded");
        if (limits.maxVariables == 0 || limits.maxBranches == 0)
            return failure(TransitionExtractionStatus::LimitExceeded,
                           "transition extraction budget is zero");

        std::vector<const clang::VarDecl *> declarations;
        for (const auto &[declaration, _] : symbolicEntry.getVarAddr()) {
            const auto *canonical = declaration->getCanonicalDecl();
            if (!canonical->getType()->isIntegerType())
                continue;
            declarations.push_back(canonical);
        }
        std::stable_sort(declarations.begin(), declarations.end(),
                         [](const auto *left, const auto *right) {
                             return std::tuple{left->getLocation().getRawEncoding(),
                                               left->getQualifiedNameAsString()} <
                                    std::tuple{right->getLocation().getRawEncoding(),
                                               right->getQualifiedNameAsString()};
                         });
        declarations.erase(std::unique(declarations.begin(), declarations.end()),
                           declarations.end());
        if (declarations.empty())
            return failure(TransitionExtractionStatus::Unsupported,
                           "loop has no scalar integer or boolean state");
        if (declarations.size() > limits.maxVariables)
            return failure(TransitionExtractionStatus::LimitExceeded,
                           "transition variable limit exceeded");
        if (limits.integerModel == CIntegerModel::RejectMachineIntegers) {
            return failure(TransitionExtractionStatus::Unsupported,
                           "C machine-integer transition semantics are not modeled");
        }

        std::vector<TransitionVariable> variables;
        std::vector<symbolic::Expr> currentSymbols;
        variables.reserve(declarations.size());
        currentSymbols.reserve(declarations.size());
        for (const auto *declaration : declarations) {
            auto current = readVariable(symbolicEntry, declaration);
            auto entry   = readVariable(concreteEntry, declaration);
            if (!current || !entry)
                return failure(TransitionExtractionStatus::InvalidInput,
                               "transition snapshots have different scalar variables");
            if ((!current->isSymbolValue() && !current->isRangeIndex()) ||
                !supportedScalar(*current) || !supportedScalar(*entry) || entry->isUnknown()) {
                return failure(TransitionExtractionStatus::Unsupported,
                               "scalar transition state contains an unsupported value");
            }
            currentSymbols.push_back(*current);
            variables.push_back({*current, *entry});
        }

        const bool allowExtendedMachineOperations =
            limits.integerModel == CIntegerModel::ExactMachineIntegers;
        if (!usesOnlyCurrentSymbols(loopCondition, currentSymbols, allowExtendedMachineOperations))
            return failure(TransitionExtractionStatus::Unsupported,
                           "loop condition depends on unsupported memory or operations");
        if (limits.integerModel == CIntegerModel::ExactMachineIntegers) {
            std::string reason;
            auto conditionType = exactMachineExpressionType(loopCondition, currentSymbols, reason);
            if (!conditionType || conditionType->kind != symbolic::ExprScalarKind::Bool) {
                return failure(TransitionExtractionStatus::Unsupported,
                               reason.empty() ? "loop condition is not a supported C boolean"
                                              : std::move(reason));
            }
        }

        std::vector<TransitionBranch> branches;
        std::vector<symbolic::Expr> parameters;
        branches.reserve(symbolicCurrent.getPaths().size());
        for (const auto &path : symbolicCurrent.getPaths()) {
            if (path->getPathState() != Path::PathState::Step)
                return failure(TransitionExtractionStatus::InvalidInput,
                               "symbolic current state contains an inactive path");
            if (path->hasUnknownMemoryAccess())
                return failure(TransitionExtractionStatus::Unsupported,
                               "transition branch contains a memory access with unknown extent");

            auto guardParts = orderedConditions(path->getPathConditions());
            for (const auto &condition : guardParts) {
                if (!usesOnlyCurrentSymbols(condition, currentSymbols,
                                            allowExtendedMachineOperations))
                    return failure(TransitionExtractionStatus::Unsupported,
                                   "branch guard depends on unsupported memory or operations");
                if (limits.integerModel == CIntegerModel::ExactMachineIntegers) {
                    std::string reason;
                    auto conditionType =
                        exactMachineExpressionType(condition, currentSymbols, reason);
                    if (!conditionType || conditionType->kind != symbolic::ExprScalarKind::Bool) {
                        return failure(TransitionExtractionStatus::Unsupported,
                                       reason.empty() ? "branch guard is not a supported C boolean"
                                                      : std::move(reason));
                    }
                }
            }

            std::vector<symbolic::Expr> nextValues;
            nextValues.reserve(declarations.size());
            for (const auto *declaration : declarations) {
                auto next = readVariable(*path, declaration);
                if (!next || next->isUnknown() ||
                    !usesOnlyCurrentSymbols(*next, currentSymbols,
                                            allowExtendedMachineOperations)) {
                    return failure(TransitionExtractionStatus::Unsupported,
                                   "next-state update is incomplete or unsupported");
                }
                if (limits.integerModel == CIntegerModel::ExactMachineIntegers) {
                    std::string reason;
                    auto nextType = exactMachineExpressionType(*next, currentSymbols, reason);
                    const auto variableType = currentSymbols[nextValues.size()].getValType();
                    if (!nextType || *nextType != variableType) {
                        return failure(
                            TransitionExtractionStatus::Unsupported,
                            reason.empty()
                                ? "next-state assignment requires an unsupported C conversion"
                                : std::move(reason));
                    }
                }
                nextValues.push_back(*next);
            }

            auto definednessConditions =
                orderedConditions(path->getMemoryAccessConditions());
            for (const auto &condition : definednessConditions) {
                if (!usesOnlyCurrentSymbols(condition, currentSymbols,
                                            allowExtendedMachineOperations)) {
                    return failure(
                        TransitionExtractionStatus::Unsupported,
                        "memory access bound depends on unsupported memory or operations");
                }
                if (limits.integerModel == CIntegerModel::ExactMachineIntegers) {
                    std::string reason;
                    auto conditionType =
                        exactMachineExpressionType(condition, currentSymbols, reason);
                    if (!conditionType ||
                        conditionType->kind != symbolic::ExprScalarKind::Bool) {
                        return failure(
                            TransitionExtractionStatus::Unsupported,
                            reason.empty() ? "memory access bound is not a supported C boolean"
                                           : std::move(reason));
                    }
                }
                collectParameters(condition, currentSymbols, parameters);
            }
            branches.push_back({conjunction(factory, std::move(guardParts)),
                                std::move(nextValues),
                                std::move(definednessConditions)});
        }

        std::vector<symbolic::Expr> preconditions;
        preconditions.reserve(variables.size() + concreteEntry.getPathConditions().size());
        for (const auto &variable : variables) {
            preconditions.push_back(variable.current.equalTo(variable.entry));
            collectParameters(variable.entry, currentSymbols, parameters);
        }
        for (const auto &condition : orderedConditions(concreteEntry.getPathConditions())) {
            preconditions.push_back(condition);
            collectParameters(condition, currentSymbols, parameters);
        }
        std::stable_sort(parameters.begin(), parameters.end(),
                         [](const auto &left, const auto &right) {
                             return std::tuple{left.hash(), left.dump()} <
                                    std::tuple{right.hash(), right.dump()};
                         });
        for (const auto &parameter : parameters) {
            if (!supportedScalar(parameter))
                return failure(TransitionExtractionStatus::Unsupported,
                               "loop-entry condition uses a non-scalar parameter");
        }

        TransitionModel model{std::move(variables),
                              std::move(parameters),
                              conjunction(factory, std::move(preconditions)),
                              loopCondition,
                              std::move(branches),
                              std::nullopt,
                              limits.integerModel == CIntegerModel::ExactMachineIntegers
                                  ? TransitionIntegerSemantics::CMachine
                                  : TransitionIntegerSemantics::Mathematical};
        if (auto error = validateTransitionModel(model))
            return failure(TransitionExtractionStatus::InvalidInput, *error);

        return {TransitionExtractionStatus::Success,
                ExtractedTransitionModel{std::move(model), std::move(declarations),
                                         std::move(currentPoint)},
                {}};
    }

} // namespace acslg::analyzer::invariant
