#include "Analyzer/NumericalInvariant/clauseCombiner.h"

#include <algorithm>
#include <utility>

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
#include <gmpxx.h>
#include <z3++.h>
#endif

namespace acslg::analyzer::invariant {
    namespace {
        bool expressionDependsOn(const symbolic::Expr &expression,
                                 const symbolic::Expr &target) {
            if (expression.structurallyEqual(target))
                return true;
            if (auto unary = symbolic::UnaryExpr::tryFrom(expression))
                return expressionDependsOn(unary->operand(), target);
            if (auto cast = symbolic::CastExpr::tryFrom(expression))
                return expressionDependsOn(cast->operand(), target);
            if (auto binary = symbolic::BinaryExpr::tryFrom(expression))
                return expressionDependsOn(binary->left(), target) ||
                       expressionDependsOn(binary->right(), target);
            return false;
        }
    } // namespace

    bool dependsOnChangedStateVariable(const TransitionModel &model,
                                       const symbolic::Expr &expression) {
        for (std::size_t index = 0; index < model.variables.size(); ++index) {
            const auto changed = std::any_of(
                model.branches.begin(), model.branches.end(), [&](const auto &branch) {
                    return index < branch.nextValues.size() &&
                           !branch.nextValues[index].structurallyEqual(
                               model.variables[index].current);
                });
            if (changed && expressionDependsOn(expression, model.variables[index].current))
                return true;
        }
        return false;
    }

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
    namespace {
        struct QueryBudget {
            std::size_t maximum;
            std::size_t used = 0;

            bool consume() {
                if (used >= maximum)
                    return false;
                ++used;
                return true;
            }
        };

        enum class TranslationMode {
            Current,
            Next
        };

        class ExprToZ3 {
          public:
            ExprToZ3(z3::context &context, const TransitionModel &model)
                : context_(context), model_(model) {
                for (std::size_t index = 0; index < model.variables.size(); ++index) {
                    current_.push_back(
                        makeConstant("v" + std::to_string(index), model.variables[index].current));
                    if (isSymbolicLeaf(model.variables[index].entry)) {
                        entry_.push_back(makeConstant("e" + std::to_string(index),
                                                      model.variables[index].entry));
                    } else {
                        entry_.push_back(std::nullopt);
                    }
                }
                for (std::size_t index = 0; index < model.parameters.size(); ++index) {
                    parameters_.push_back(
                        makeConstant("p" + std::to_string(index), model.parameters[index]));
                }
            }

            std::optional<z3::expr> translate(const symbolic::Expr &expression,
                                              TranslationMode mode = TranslationMode::Current,
                                              const TransitionBranch *branch           = nullptr,
                                              const std::vector<z3::expr> *machineNext = nullptr) {
                for (std::size_t index = 0; index < model_.variables.size(); ++index) {
                    if (!expression.structurallyEqual(model_.variables[index].current))
                        continue;
                    if (mode == TranslationMode::Next) {
                        if (machineNext)
                            return machineNext->at(index);
                        if (!branch) {
                            reason_ = "next-state translation has no branch";
                            return std::nullopt;
                        }
                        return translate(branch->nextValues[index], TranslationMode::Current,
                                         nullptr);
                    }
                    return current_[index];
                }

                for (std::size_t index = 0; index < model_.parameters.size(); ++index) {
                    if (expression.structurallyEqual(model_.parameters[index]))
                        return parameters_[index];
                }

                for (std::size_t index = 0; index < model_.variables.size(); ++index) {
                    if (entry_[index] &&
                        expression.structurallyEqual(model_.variables[index].entry))
                        return *entry_[index];
                }

                if (auto literal = symbolic::LiteralExpr::tryFrom(expression)) {
                    if (expression.getValType().kind == symbolic::ExprScalarKind::Bool)
                        return context_.bool_val(literal->value() != 0);
                    return std::visit(
                        [this](auto value) { return context_.int_val(value); },
                        literal->integerValue());
                }

                if (auto unary = symbolic::UnaryExpr::tryFrom(expression)) {
                    auto operand = translate(unary->operand(), mode, branch, machineNext);
                    if (!operand)
                        return std::nullopt;
                    switch (unary->operation()) {
                        case symbolic::UnaryOp::Plus: return operand;
                        case symbolic::UnaryOp::Minus:
                            if (!operand->is_arith())
                                return unsupported("unary minus requires an integer operand");
                            return -*operand;
                        case symbolic::UnaryOp::LogicalNot:
                            if (!operand->is_bool())
                                return unsupported("logical not requires a boolean operand");
                            return !*operand;
                        default: return unsupported("unsupported unary operation");
                    }
                }

                if (auto binary = symbolic::BinaryExpr::tryFrom(expression)) {
                    auto left = translate(binary->left(), mode, branch, machineNext);
                    if (!left)
                        return std::nullopt;
                    auto right = translate(binary->right(), mode, branch, machineNext);
                    if (!right)
                        return std::nullopt;

                    switch (binary->operation()) {
                        case symbolic::BinaryOp::Add:
                            return arithmetic(*left, *right,
                                              [](auto lhs, auto rhs) { return lhs + rhs; });
                        case symbolic::BinaryOp::Subtract:
                            return arithmetic(*left, *right,
                                              [](auto lhs, auto rhs) { return lhs - rhs; });
                        case symbolic::BinaryOp::Multiply:
                            return arithmetic(*left, *right,
                                              [](auto lhs, auto rhs) { return lhs * rhs; });
                        case symbolic::BinaryOp::LessThan:
                            return comparison(*left, *right,
                                              [](auto lhs, auto rhs) { return lhs < rhs; });
                        case symbolic::BinaryOp::GreaterThan:
                            return comparison(*left, *right,
                                              [](auto lhs, auto rhs) { return lhs > rhs; });
                        case symbolic::BinaryOp::LessEqual:
                            return comparison(*left, *right,
                                              [](auto lhs, auto rhs) { return lhs <= rhs; });
                        case symbolic::BinaryOp::GreaterEqual:
                            return comparison(*left, *right,
                                              [](auto lhs, auto rhs) { return lhs >= rhs; });
                        case symbolic::BinaryOp::Equal:
                            if (left->get_sort().sort_kind() != right->get_sort().sort_kind())
                                return unsupported("equality operands have different sorts");
                            return *left == *right;
                        case symbolic::BinaryOp::NotEqual:
                            if (left->get_sort().sort_kind() != right->get_sort().sort_kind())
                                return unsupported("inequality operands have different sorts");
                            return *left != *right;
                        case symbolic::BinaryOp::LogicalAnd:
                            if (!left->is_bool() || !right->is_bool())
                                return unsupported("logical and requires boolean operands");
                            return *left && *right;
                        case symbolic::BinaryOp::LogicalOr:
                            if (!left->is_bool() || !right->is_bool())
                                return unsupported("logical or requires boolean operands");
                            return *left || *right;
                        default: return unsupported("unsupported binary operation");
                    }
                }

                return unsupported("unsupported symbolic expression leaf");
            }

            const std::string &reason() const { return reason_; }
            const std::vector<z3::expr> &current() const { return current_; }
            const std::vector<std::optional<z3::expr>> &entry() const { return entry_; }
            const std::vector<z3::expr> &parameters() const { return parameters_; }

          private:
            bool isSymbolicLeaf(const symbolic::Expr &expression) const {
                return expression.isSymbolValue() || expression.isRangeIndex();
            }

            z3::expr makeConstant(const std::string &name, const symbolic::Expr &expression) {
                if (expression.getValType().kind == symbolic::ExprScalarKind::Bool)
                    return context_.bool_const(name.c_str());
                return context_.int_const(name.c_str());
            }

            std::optional<z3::expr> unsupported(std::string reason) {
                reason_ = std::move(reason);
                return std::nullopt;
            }

            template <typename Operation>
            std::optional<z3::expr> arithmetic(const z3::expr &left,
                                               const z3::expr &right,
                                               Operation operation) {
                if (!left.is_arith() || !right.is_arith())
                    return unsupported("arithmetic operation requires integer operands");
                return operation(left, right);
            }

            template <typename Operation>
            std::optional<z3::expr> comparison(const z3::expr &left,
                                               const z3::expr &right,
                                               Operation operation) {
                if (!left.is_arith() || !right.is_arith())
                    return unsupported("comparison requires integer operands");
                return operation(left, right);
            }

            z3::context &context_;
            const TransitionModel &model_;
            std::vector<z3::expr> current_;
            std::vector<std::optional<z3::expr>> entry_;
            std::vector<z3::expr> parameters_;
            std::string reason_;
        };

        struct CMachineValue {
            z3::expr value;
            z3::expr defined;
            symbolic::ExprType type;
        };

        class CMachineToZ3 {
          public:
            CMachineToZ3(z3::context &context,
                         const TransitionModel &model,
                         const ExprToZ3 &logicTranslator)
                : context_(context), model_(model), logicTranslator_(logicTranslator) {}

            std::optional<z3::expr> ranges() {
                auto result = context_.bool_val(true);
                for (std::size_t index = 0; index < model_.variables.size(); ++index) {
                    auto current = rangeConstraint(logicTranslator_.current()[index],
                                                   model_.variables[index].current.getValType());
                    if (!current)
                        return std::nullopt;
                    result = result && *current;
                    if (logicTranslator_.entry()[index]) {
                        auto entry = rangeConstraint(*logicTranslator_.entry()[index],
                                                     model_.variables[index].entry.getValType());
                        if (!entry)
                            return std::nullopt;
                        result = result && *entry;
                    }
                }
                for (std::size_t index = 0; index < model_.parameters.size(); ++index) {
                    auto parameter = rangeConstraint(logicTranslator_.parameters()[index],
                                                     model_.parameters[index].getValType());
                    if (!parameter)
                        return std::nullopt;
                    result = result && *parameter;
                }
                return result;
            }

            std::optional<CMachineValue> translate(const symbolic::Expr &expression) {
                for (std::size_t index = 0; index < model_.variables.size(); ++index) {
                    if (expression.structurallyEqual(model_.variables[index].current)) {
                        return leaf(logicTranslator_.current()[index], expression.getValType());
                    }
                }
                for (std::size_t index = 0; index < model_.parameters.size(); ++index) {
                    if (expression.structurallyEqual(model_.parameters[index])) {
                        return leaf(logicTranslator_.parameters()[index], expression.getValType());
                    }
                }
                for (std::size_t index = 0; index < model_.variables.size(); ++index) {
                    if (logicTranslator_.entry()[index] &&
                        expression.structurallyEqual(model_.variables[index].entry)) {
                        return leaf(*logicTranslator_.entry()[index], expression.getValType());
                    }
                }

                if (auto literal = symbolic::LiteralExpr::tryFrom(expression)) {
                    const auto type = expression.getValType();
                    if (!supported(type))
                        return unsupported("unsupported machine-integer literal type");
                    if (type.kind == symbolic::ExprScalarKind::Bool) {
                        return CMachineValue{context_.bool_val(literal->value() != 0),
                                             context_.bool_val(true), type};
                    }
                    return CMachineValue{std::visit(
                                             [this](auto value) {
                                                 return context_.int_val(value);
                                             },
                                             literal->integerValue()),
                                         context_.bool_val(true), type};
                }

                if (auto unary = symbolic::UnaryExpr::tryFrom(expression))
                    return translateUnary(expression, *unary);
                if (auto cast = symbolic::CastExpr::tryFrom(expression))
                    return translateCast(*cast);
                if (auto binary = symbolic::BinaryExpr::tryFrom(expression))
                    return translateBinary(expression, *binary);
                return unsupported("unsupported machine-integer expression leaf");
            }

            std::optional<std::pair<std::vector<z3::expr>, z3::expr>> lowerNext(
                const TransitionBranch &branch) {
                std::vector<z3::expr> values;
                values.reserve(branch.nextValues.size());
                auto defined = context_.bool_val(true);
                for (std::size_t index = 0; index < branch.nextValues.size(); ++index) {
                    auto next = translate(branch.nextValues[index]);
                    if (!next)
                        return std::nullopt;
                    if (!sameType(next->type, model_.variables[index].current.getValType()))
                        return unsupportedPair(
                            "next-state value requires an unsupported assignment conversion");
                    values.push_back(next->value);
                    defined = defined && next->defined;
                }
                return std::pair{std::move(values), std::move(defined)};
            }

            const std::string &reason() const { return reason_; }

          private:
            static bool sameType(symbolic::ExprType left, symbolic::ExprType right) {
                return left == right;
            }

            static bool supported(symbolic::ExprType type) {
                if (type.kind == symbolic::ExprScalarKind::Bool)
                    return type.bitWidth == 1;
                return (type.kind == symbolic::ExprScalarKind::Int ||
                        type.kind == symbolic::ExprScalarKind::UInt) &&
                       (type.bitWidth == 8 || type.bitWidth == 16 || type.bitWidth == 32 ||
                        type.bitWidth == 64);
            }

            z3::expr integer(const mpz_class &value) {
                return context_.int_val(value.get_str().c_str());
            }

            std::pair<z3::expr, z3::expr> bounds(symbolic::ExprType type) {
                mpz_class modulus = 1;
                modulus <<= type.bitWidth;
                if (type.kind == symbolic::ExprScalarKind::UInt)
                    return {context_.int_val(0), integer(modulus - 1)};
                mpz_class half = modulus >> 1;
                return {integer(-half), integer(half - 1)};
            }

            z3::expr modulus(symbolic::ExprType type) {
                mpz_class value = 1;
                value <<= type.bitWidth;
                return integer(value);
            }

            z3::expr signedQuotient(const z3::expr &left, const z3::expr &right) {
                auto leftMagnitude  = z3::ite(left >= 0, left, -left);
                auto rightMagnitude = z3::ite(right >= 0, right, -right);
                auto magnitude      = leftMagnitude / rightMagnitude;
                return z3::ite((left < 0) != (right < 0), -magnitude, magnitude);
            }

            z3::expr shiftFactor(const z3::expr &count, unsigned bitWidth) {
                auto result      = context_.int_val(1);
                mpz_class factor = 1;
                for (unsigned value = 1; value < bitWidth; ++value) {
                    factor <<= 1;
                    result = z3::ite(count == static_cast<int>(value), integer(factor), result);
                }
                return result;
            }

            z3::expr fromBitVector(const z3::expr &value, symbolic::ExprType type) {
                return z3::bv2int(value, type.kind == symbolic::ExprScalarKind::Int);
            }

            std::optional<z3::expr> rangeConstraint(const z3::expr &value,
                                                    symbolic::ExprType type) {
                if (!supported(type)) {
                    reason_ =
                        "machine-integer model currently supports only bool and 8/16/32/64-bit "
                        "signed/unsigned values";
                    return std::nullopt;
                }
                if (type.kind == symbolic::ExprScalarKind::Bool)
                    return context_.bool_val(true);
                auto [minimum, maximum] = bounds(type);
                return value >= minimum && value <= maximum;
            }

            std::optional<CMachineValue> leaf(const z3::expr &value, symbolic::ExprType type) {
                if (!supported(type))
                    return unsupported("unsupported machine-integer symbolic type");
                return CMachineValue{value, context_.bool_val(true), type};
            }

            std::optional<CMachineValue> translateUnary(const symbolic::Expr &expression,
                                                        const symbolic::UnaryExpr &unary) {
                auto operand = translate(unary.operand());
                if (!operand)
                    return std::nullopt;
                auto resultType = expression.getValType();
                if (unary.operation() == symbolic::UnaryOp::LogicalNot)
                    resultType = {symbolic::ExprScalarKind::Bool, 1};
                if (unary.operation() != symbolic::UnaryOp::LogicalNot &&
                    operand->type.bitWidth < 32) {
                    return unsupported("narrow unary operation requires integer promotion");
                }
                switch (unary.operation()) {
                    case symbolic::UnaryOp::Plus:
                        if (!sameType(resultType, operand->type))
                            return unsupported("unary plus requires an unsupported conversion");
                        return CMachineValue{operand->value, operand->defined, resultType};
                    case symbolic::UnaryOp::Minus: {
                        if (!sameType(resultType, operand->type) ||
                            resultType.kind == symbolic::ExprScalarKind::Bool)
                            return unsupported("unary minus requires matching integer types");
                        auto raw = -operand->value;
                        if (resultType.kind == symbolic::ExprScalarKind::UInt) {
                            return CMachineValue{raw % modulus(resultType), operand->defined,
                                                 resultType};
                        }
                        auto [minimum, maximum] = bounds(resultType);
                        return CMachineValue{
                            raw, operand->defined && raw >= minimum && raw <= maximum, resultType};
                    }
                    case symbolic::UnaryOp::BitwiseNot:
                        if (!sameType(resultType, operand->type) ||
                            resultType.kind == symbolic::ExprScalarKind::Bool)
                            return unsupported("bitwise not requires a matching integer type");
                        return CMachineValue{
                            fromBitVector(~z3::int2bv(resultType.bitWidth, operand->value),
                                          resultType),
                            operand->defined, resultType};
                    case symbolic::UnaryOp::LogicalNot:
                        if (operand->type.kind != symbolic::ExprScalarKind::Bool ||
                            resultType.kind != symbolic::ExprScalarKind::Bool)
                            return unsupported("logical not requires boolean values");
                        return CMachineValue{!operand->value, operand->defined, resultType};
                    default: return unsupported("unsupported machine-integer unary operation");
                }
            }

            std::optional<CMachineValue> translateCast(const symbolic::CastExpr &cast) {
                auto operand = translate(cast.operand());
                if (!operand)
                    return std::nullopt;
                const auto target = cast.targetType();
                if (!supported(target))
                    return unsupported("unsupported machine-integer cast target type");

                if (target.kind == symbolic::ExprScalarKind::Bool) {
                    auto value = operand->type.kind == symbolic::ExprScalarKind::Bool
                                     ? operand->value
                                     : operand->value != 0;
                    return CMachineValue{std::move(value), operand->defined, target};
                }

                if (operand->type.kind == symbolic::ExprScalarKind::Bool) {
                    return CMachineValue{z3::ite(operand->value, context_.int_val(1),
                                                context_.int_val(0)),
                                         operand->defined, target};
                }

                if (target.kind == symbolic::ExprScalarKind::UInt) {
                    auto value = operand->value % modulus(target);
                    return CMachineValue{std::move(value), operand->defined, target};
                }

                if (operand->type.kind == symbolic::ExprScalarKind::UInt &&
                    operand->type.bitWidth >= target.bitWidth) {
                    return unsupported("unsigned-to-signed conversion is implementation-defined");
                }

                auto [minimum, maximum] = bounds(target);
                return CMachineValue{operand->value,
                                     operand->defined && operand->value >= minimum &&
                                         operand->value <= maximum,
                                     target};
            }

            std::optional<CMachineValue> translateBinary(const symbolic::Expr &expression,
                                                         const symbolic::BinaryExpr &binary) {
                auto left = translate(binary.left());
                if (!left)
                    return std::nullopt;
                auto right = translate(binary.right());
                if (!right)
                    return std::nullopt;
                auto resultType = expression.getValType();

                if (binary.operation() == symbolic::BinaryOp::LogicalAnd ||
                    binary.operation() == symbolic::BinaryOp::LogicalOr) {
                    resultType = {symbolic::ExprScalarKind::Bool, 1};
                    if (left->type.kind != symbolic::ExprScalarKind::Bool ||
                        right->type.kind != symbolic::ExprScalarKind::Bool ||
                        resultType.kind != symbolic::ExprScalarKind::Bool)
                        return unsupported("logical operation requires boolean values");
                    if (binary.operation() == symbolic::BinaryOp::LogicalAnd) {
                        return CMachineValue{left->value && right->value,
                                             left->defined && (!left->value || right->defined),
                                             resultType};
                    }
                    return CMachineValue{left->value || right->value,
                                         left->defined && (left->value || right->defined),
                                         resultType};
                }

                if (binary.operation() == symbolic::BinaryOp::ShiftLeft ||
                    binary.operation() == symbolic::BinaryOp::ShiftRight) {
                    if (left->type.kind == symbolic::ExprScalarKind::Bool ||
                        right->type.kind == symbolic::ExprScalarKind::Bool ||
                        (left->type.bitWidth != 32 && left->type.bitWidth != 64) ||
                        (right->type.bitWidth != 32 && right->type.bitWidth != 64) ||
                        !sameType(resultType, left->type)) {
                        return unsupported(
                            "shift requires independently promoted 32/64-bit integer operands");
                    }
                    if (binary.operation() == symbolic::BinaryOp::ShiftRight &&
                        left->type.kind == symbolic::ExprScalarKind::Int) {
                        return unsupported(
                            "signed right shift has implementation-defined semantics");
                    }

                    auto defined = left->defined && right->defined && right->value >= 0 &&
                                   right->value < static_cast<int>(left->type.bitWidth);
                    auto factor = shiftFactor(right->value, left->type.bitWidth);
                    if (binary.operation() == symbolic::BinaryOp::ShiftRight) {
                        return CMachineValue{left->value / factor, std::move(defined), resultType};
                    }

                    auto raw = left->value * factor;
                    if (resultType.kind == symbolic::ExprScalarKind::UInt) {
                        return CMachineValue{raw % modulus(resultType), std::move(defined),
                                             resultType};
                    }
                    auto [minimum, maximum] = bounds(resultType);
                    (void)minimum;
                    return CMachineValue{raw, defined && left->value >= 0 && raw <= maximum,
                                         resultType};
                }

                if (!sameType(left->type, right->type))
                    return unsupported(
                        "mixed machine-integer operand types require C conversion lowering");
                const auto operandType = left->type;
                if (operandType.kind != symbolic::ExprScalarKind::Bool &&
                    operandType.bitWidth < 32) {
                    return unsupported("narrow binary operation requires integer promotion");
                }
                auto operandsDefined   = left->defined && right->defined;

                switch (binary.operation()) {
                    case symbolic::BinaryOp::Equal:
                    case symbolic::BinaryOp::NotEqual:
                    case symbolic::BinaryOp::LessThan:
                    case symbolic::BinaryOp::GreaterThan:
                    case symbolic::BinaryOp::LessEqual:
                    case symbolic::BinaryOp::GreaterEqual: {
                        resultType = {symbolic::ExprScalarKind::Bool, 1};
                        if (resultType.kind != symbolic::ExprScalarKind::Bool)
                            return unsupported("comparison result is not boolean");
                        z3::expr value = left->value == right->value;
                        switch (binary.operation()) {
                            case symbolic::BinaryOp::Equal: break;
                            case symbolic::BinaryOp::NotEqual:
                                value = left->value != right->value;
                                break;
                            case symbolic::BinaryOp::LessThan:
                                value = left->value < right->value;
                                break;
                            case symbolic::BinaryOp::GreaterThan:
                                value = left->value > right->value;
                                break;
                            case symbolic::BinaryOp::LessEqual:
                                value = left->value <= right->value;
                                break;
                            case symbolic::BinaryOp::GreaterEqual:
                                value = left->value >= right->value;
                                break;
                            default: break;
                        }
                        return CMachineValue{std::move(value), std::move(operandsDefined),
                                             resultType};
                    }
                    case symbolic::BinaryOp::Add:
                    case symbolic::BinaryOp::Subtract:
                    case symbolic::BinaryOp::Multiply: {
                        if (!sameType(resultType, operandType) ||
                            resultType.kind == symbolic::ExprScalarKind::Bool)
                            return unsupported(
                                "arithmetic result requires an unsupported C conversion");
                        z3::expr raw = left->value + right->value;
                        if (binary.operation() == symbolic::BinaryOp::Subtract)
                            raw = left->value - right->value;
                        else if (binary.operation() == symbolic::BinaryOp::Multiply)
                            raw = left->value * right->value;
                        if (resultType.kind == symbolic::ExprScalarKind::UInt) {
                            return CMachineValue{raw % modulus(resultType),
                                                 std::move(operandsDefined), resultType};
                        }
                        auto [minimum, maximum] = bounds(resultType);
                        return CMachineValue{
                            raw, operandsDefined && raw >= minimum && raw <= maximum, resultType};
                    }
                    case symbolic::BinaryOp::Divide:
                    case symbolic::BinaryOp::Remainder: {
                        if (!sameType(resultType, operandType) ||
                            resultType.kind == symbolic::ExprScalarKind::Bool)
                            return unsupported(
                                "division result requires an unsupported C conversion");

                        auto defined      = operandsDefined && right->value != 0;
                        z3::expr quotient = resultType.kind == symbolic::ExprScalarKind::UInt
                                                ? left->value / right->value
                                                : signedQuotient(left->value, right->value);
                        if (resultType.kind == symbolic::ExprScalarKind::Int) {
                            auto [minimum, maximum] = bounds(resultType);
                            (void)maximum;
                            defined = defined && !(left->value == minimum && right->value == -1);
                        }
                        auto value = binary.operation() == symbolic::BinaryOp::Divide
                                         ? quotient
                                         : left->value - quotient * right->value;
                        return CMachineValue{std::move(value), std::move(defined), resultType};
                    }
                    case symbolic::BinaryOp::BitAnd:
                    case symbolic::BinaryOp::BitOr:
                    case symbolic::BinaryOp::BitXor: {
                        if (!sameType(resultType, operandType) ||
                            resultType.kind == symbolic::ExprScalarKind::Bool)
                            return unsupported(
                                "bitwise result requires an unsupported C conversion");
                        auto leftBits  = z3::int2bv(resultType.bitWidth, left->value);
                        auto rightBits = z3::int2bv(resultType.bitWidth, right->value);
                        z3::expr bits  = leftBits & rightBits;
                        if (binary.operation() == symbolic::BinaryOp::BitOr)
                            bits = leftBits | rightBits;
                        else if (binary.operation() == symbolic::BinaryOp::BitXor)
                            bits = leftBits ^ rightBits;
                        return CMachineValue{fromBitVector(bits, resultType),
                                             std::move(operandsDefined), resultType};
                    }
                    default: return unsupported("unsupported machine-integer binary operation");
                }
            }

            std::optional<CMachineValue> unsupported(std::string reason) {
                reason_ = std::move(reason);
                return std::nullopt;
            }

            std::optional<std::pair<std::vector<z3::expr>, z3::expr>> unsupportedPair(
                std::string reason) {
                reason_ = std::move(reason);
                return std::nullopt;
            }

            z3::context &context_;
            const TransitionModel &model_;
            const ExprToZ3 &logicTranslator_;
            std::string reason_;
        };

        Counterexample collectCounterexample(const z3::model &model, const ExprToZ3 &translator) {
            Counterexample result;
            auto collect = [&](CounterexampleSymbolKind kind, std::size_t index,
                               const z3::expr &symbol) {
                const auto value = model.eval(symbol, true);
                if (symbol.is_bool()) {
                    if (value.is_true())
                        result.bindings.push_back({kind, index, true});
                    else if (value.is_false())
                        result.bindings.push_back({kind, index, false});
                    return;
                }
                std::int64_t integer = 0;
                if (value.is_numeral_i64(integer))
                    result.bindings.push_back({kind, index, integer});
            };

            for (std::size_t index = 0; index < translator.current().size(); ++index)
                collect(CounterexampleSymbolKind::Current, index, translator.current()[index]);
            for (std::size_t index = 0; index < translator.entry().size(); ++index) {
                if (translator.entry()[index])
                    collect(CounterexampleSymbolKind::Entry, index, *translator.entry()[index]);
            }
            for (std::size_t index = 0; index < translator.parameters().size(); ++index)
                collect(CounterexampleSymbolKind::Parameter, index, translator.parameters()[index]);
            return result;
        }

        struct QueryResult {
            VerificationStatus status;
            std::optional<Counterexample> counterexample;
            std::string reason;
        };

        QueryResult queryCounterexample(z3::context &context,
                                        const z3::expr &formula,
                                        const ExprToZ3 &translator,
                                        QueryBudget &budget,
                                        const ClauseLimits &limits) {
            if (!budget.consume())
                return {VerificationStatus::BudgetExceeded, std::nullopt,
                        "solver query budget exhausted"};

            z3::solver solver(context);
            solver.set("timeout", limits.solverTimeoutMs);
            if (limits.solverResourceLimit != 0)
                solver.set("rlimit", limits.solverResourceLimit);
            solver.add(formula.simplify());
            const auto result = solver.check();
            if (result == z3::unsat)
                return {VerificationStatus::Proved, std::nullopt, {}};
            if (result == z3::sat) {
                return {VerificationStatus::Refuted,
                        collectCounterexample(solver.get_model(), translator),
                        {}};
            }
            return {VerificationStatus::Unknown, std::nullopt, solver.reason_unknown()};
        }

        VerificationResult verifyWithBudget(const TransitionModel &model,
                                            const symbolic::Expr &invariant,
                                            const ClauseLimits &limits,
                                            QueryBudget &budget) {
            if (auto error = validateTransitionModel(model)) {
                return {VerificationStatus::InvalidModel,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        budget.used,
                        *error};
            }
            if (&invariant.factory() != &model.variables.front().current.factory()) {
                return {VerificationStatus::InvalidModel,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        budget.used,
                        "invariant uses a different expression factory"};
            }

            try {
                z3::context context;
                ExprToZ3 translator(context, model);
                auto precondition = translator.translate(model.precondition);
                auto candidate    = translator.translate(invariant);
                if (!precondition || !candidate) {
                    return {VerificationStatus::Unsupported,
                            std::nullopt,
                            std::nullopt,
                            std::nullopt,
                            budget.used,
                            translator.reason()};
                }
                if (!precondition->is_bool() || !candidate->is_bool()) {
                    return {VerificationStatus::Unsupported,
                            std::nullopt,
                            std::nullopt,
                            std::nullopt,
                            budget.used,
                            "precondition and invariant must be boolean"};
                }

                const bool usesMachineSemantics =
                    model.integerSemantics == TransitionIntegerSemantics::CMachine;
                std::optional<CMachineToZ3> machineTranslator;
                auto stateRanges = context.bool_val(true);
                if (usesMachineSemantics) {
                    machineTranslator.emplace(context, model, translator);
                    auto ranges = machineTranslator->ranges();
                    if (!ranges) {
                        return {VerificationStatus::Unsupported,
                                std::nullopt,
                                std::nullopt,
                                std::nullopt,
                                budget.used,
                                machineTranslator->reason()};
                    }
                    stateRanges = *ranges;
                }

                auto initiation =
                    queryCounterexample(context, stateRanges && *precondition && !*candidate,
                                        translator, budget, limits);
                if (initiation.status != VerificationStatus::Proved) {
                    return {initiation.status, ObligationKind::Initiation,
                            std::nullopt,      std::move(initiation.counterexample),
                            budget.used,       std::move(initiation.reason)};
                }

                auto loopConditionValue   = context.bool_val(false);
                auto loopConditionDefined = context.bool_val(true);
                if (usesMachineSemantics) {
                    auto loopCondition = machineTranslator->translate(model.loopCondition);
                    if (!loopCondition ||
                        loopCondition->type.kind != symbolic::ExprScalarKind::Bool) {
                        return {VerificationStatus::Unsupported,
                                std::nullopt,
                                std::nullopt,
                                std::nullopt,
                                budget.used,
                                machineTranslator->reason().empty()
                                    ? "loop condition must be a supported C boolean expression"
                                    : machineTranslator->reason()};
                    }
                    loopConditionValue   = loopCondition->value;
                    loopConditionDefined = loopCondition->defined;
                    auto conditionSafety = queryCounterexample(
                        context, stateRanges && *candidate && !loopConditionDefined, translator,
                        budget, limits);
                    if (conditionSafety.status != VerificationStatus::Proved) {
                        return {conditionSafety.status,
                                ObligationKind::Definedness,
                                std::nullopt,
                                std::move(conditionSafety.counterexample),
                                budget.used,
                                std::move(conditionSafety.reason)};
                    }
                } else {
                    auto loopCondition = translator.translate(model.loopCondition);
                    if (!loopCondition || !loopCondition->is_bool()) {
                        return {VerificationStatus::Unsupported,
                                std::nullopt,
                                std::nullopt,
                                std::nullopt,
                                budget.used,
                                translator.reason().empty() ? "loop condition must be boolean"
                                                            : translator.reason()};
                    }
                    loopConditionValue = *loopCondition;
                }

                for (std::size_t branchIndex = 0; branchIndex < model.branches.size();
                     ++branchIndex) {
                    const auto &branch  = model.branches[branchIndex];
                    auto guardValue     = context.bool_val(false);
                    auto guardDefined   = context.bool_val(true);
                    auto updatesDefined = context.bool_val(true);
                    auto accessesDefined = context.bool_val(true);
                    auto accessesInBounds = context.bool_val(true);
                    std::optional<z3::expr> nextCandidate;

                    if (usesMachineSemantics) {
                        auto guard = machineTranslator->translate(branch.guard);
                        auto next  = machineTranslator->lowerNext(branch);
                        if (!guard || guard->type.kind != symbolic::ExprScalarKind::Bool || !next) {
                            return {VerificationStatus::Unsupported,
                                    std::nullopt,
                                    std::nullopt,
                                    std::nullopt,
                                    budget.used,
                                    machineTranslator->reason().empty()
                                        ? "branch uses an unsupported C machine-integer expression"
                                        : machineTranslator->reason()};
                        }
                        guardValue     = guard->value;
                        guardDefined   = guard->defined;
                        updatesDefined = next->second;
                        for (const auto &condition : branch.definednessConditions) {
                            auto translated = machineTranslator->translate(condition);
                            if (!translated ||
                                translated->type.kind != symbolic::ExprScalarKind::Bool) {
                                return {
                                    VerificationStatus::Unsupported,
                                    std::nullopt,
                                    std::nullopt,
                                    std::nullopt,
                                    budget.used,
                                    machineTranslator->reason().empty()
                                        ? "memory access bound must be a supported C boolean"
                                        : machineTranslator->reason()};
                            }
                            accessesDefined =
                                accessesDefined && translated->defined;
                            accessesInBounds =
                                accessesInBounds && translated->value;
                        }
                        nextCandidate  = translator.translate(invariant, TranslationMode::Next,
                                                              nullptr, &next->first);

                        auto branchSafety = queryCounterexample(
                            context,
                            stateRanges && *candidate && loopConditionValue &&
                                (!guardDefined ||
                                 (guardValue &&
                                  (!updatesDefined || !accessesDefined ||
                                   !accessesInBounds))),
                            translator, budget, limits);
                        if (branchSafety.status != VerificationStatus::Proved) {
                            return {branchSafety.status, ObligationKind::Definedness,
                                    branchIndex,         std::move(branchSafety.counterexample),
                                    budget.used,         std::move(branchSafety.reason)};
                        }
                    } else {
                        auto guard = translator.translate(branch.guard);
                        if (guard)
                            guardValue = *guard;
                        for (const auto &condition : branch.definednessConditions) {
                            auto translated = translator.translate(condition);
                            if (!translated || !translated->is_bool()) {
                                return {
                                    VerificationStatus::Unsupported,
                                    std::nullopt,
                                    std::nullopt,
                                    std::nullopt,
                                    budget.used,
                                    translator.reason().empty()
                                        ? "memory access bound must be boolean"
                                        : translator.reason()};
                            }
                            accessesInBounds = accessesInBounds && *translated;
                        }
                        nextCandidate =
                            translator.translate(invariant, TranslationMode::Next, &branch);
                        if (!guard || !guard->is_bool()) {
                            return {VerificationStatus::Unsupported,
                                    std::nullopt,
                                    std::nullopt,
                                    std::nullopt,
                                    budget.used,
                                    translator.reason().empty() ? "branch guard must be boolean"
                                                                : translator.reason()};
                        }
                        if (!branch.definednessConditions.empty()) {
                            auto branchSafety = queryCounterexample(
                                context,
                                *candidate && loopConditionValue && guardValue &&
                                    !accessesInBounds,
                                translator, budget, limits);
                            if (branchSafety.status != VerificationStatus::Proved) {
                                return {branchSafety.status,
                                        ObligationKind::Definedness,
                                        branchIndex,
                                        std::move(branchSafety.counterexample),
                                        budget.used,
                                        std::move(branchSafety.reason)};
                            }
                        }
                    }
                    if (!nextCandidate || !nextCandidate->is_bool()) {
                        return {VerificationStatus::Unsupported,
                                std::nullopt,
                                std::nullopt,
                                std::nullopt,
                                budget.used,
                                translator.reason().empty() ? "next invariant must be boolean"
                                                            : translator.reason()};
                    }
                    auto consecution = queryCounterexample(
                        context,
                        stateRanges && *candidate && loopConditionDefined && loopConditionValue &&
                            guardDefined && guardValue && updatesDefined && !*nextCandidate,
                        translator, budget, limits);
                    if (consecution.status != VerificationStatus::Proved) {
                        return {consecution.status, ObligationKind::Consecution,
                                branchIndex,        std::move(consecution.counterexample),
                                budget.used,        std::move(consecution.reason)};
                    }
                }

                if (model.postcondition) {
                    auto postcondition = translator.translate(*model.postcondition);
                    if (!postcondition || !postcondition->is_bool()) {
                        return {VerificationStatus::Unsupported,
                                std::nullopt,
                                std::nullopt,
                                std::nullopt,
                                budget.used,
                                translator.reason().empty() ? "postcondition must be boolean"
                                                            : translator.reason()};
                    }
                    auto exitPost =
                        queryCounterexample(context,
                                            stateRanges && *candidate && loopConditionDefined &&
                                                !loopConditionValue && !*postcondition,
                                            translator, budget, limits);
                    if (exitPost.status != VerificationStatus::Proved) {
                        return {exitPost.status, ObligationKind::ExitPost,
                                std::nullopt,    std::move(exitPost.counterexample),
                                budget.used,     std::move(exitPost.reason)};
                    }
                }

                return {VerificationStatus::Proved,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        budget.used,
                        {}};
            } catch (const z3::exception &error) {
                return {VerificationStatus::Unsupported,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        budget.used,
                        error.msg()};
            }
        }

        std::size_t expressionSize(const symbolic::Expr &expression) {
            if (auto unary = symbolic::UnaryExpr::tryFrom(expression))
                return 1 + expressionSize(unary->operand());
            if (auto cast = symbolic::CastExpr::tryFrom(expression))
                return 1 + expressionSize(cast->operand());
            if (auto binary = symbolic::BinaryExpr::tryFrom(expression))
                return 1 + expressionSize(binary->left()) + expressionSize(binary->right());
            return 1;
        }

        bool containsExpression(const std::vector<symbolic::Expr> &expressions,
                                const symbolic::Expr &candidate) {
            return std::any_of(expressions.begin(), expressions.end(), [&](const auto &existing) {
                return existing.structurallyEqual(candidate);
            });
        }

        std::optional<bool> evaluateWithCounterexample(z3::expr expression,
                                                       const Counterexample &counterexample,
                                                       const ExprToZ3 &translator) {
            z3::expr_vector sources(expression.ctx());
            z3::expr_vector values(expression.ctx());

            for (const auto &binding : counterexample.bindings) {
                const z3::expr *source = nullptr;
                switch (binding.kind) {
                    case CounterexampleSymbolKind::Current:
                        source = &translator.current().at(binding.index);
                        break;
                    case CounterexampleSymbolKind::Entry:
                        if (translator.entry().at(binding.index))
                            source = &*translator.entry().at(binding.index);
                        break;
                    case CounterexampleSymbolKind::Parameter:
                        source = &translator.parameters().at(binding.index);
                        break;
                }
                if (!source)
                    continue;
                sources.push_back(*source);
                if (const auto *integer = std::get_if<std::int64_t>(&binding.value))
                    values.push_back(expression.ctx().int_val(*integer));
                else
                    values.push_back(expression.ctx().bool_val(std::get<bool>(binding.value)));
            }

            const auto simplified = expression.substitute(sources, values).simplify();
            if (simplified.is_true())
                return true;
            if (simplified.is_false())
                return false;
            return std::nullopt;
        }

        bool repairsCounterexample(const TransitionModel &model,
                                   const symbolic::Expr &candidate,
                                   const VerificationResult &failure) {
            if (!failure.counterexample || !failure.failedObligation)
                return true;
            if (model.integerSemantics == TransitionIntegerSemantics::CMachine)
                return true;
            try {
                z3::context context;
                ExprToZ3 translator(context, model);
                auto current = translator.translate(candidate);
                if (!current || !current->is_bool())
                    return true;
                const auto currentValue =
                    evaluateWithCounterexample(*current, *failure.counterexample, translator);

                switch (*failure.failedObligation) {
                    case ObligationKind::Initiation:
                        return !currentValue.has_value() || *currentValue;
                    case ObligationKind::Definedness:
                    case ObligationKind::ExitPost:
                        return !currentValue.has_value() || !*currentValue;
                    case ObligationKind::Consecution: {
                        if (!failure.failedBranch)
                            return true;
                        auto next = translator.translate(candidate, TranslationMode::Next,
                                                         &model.branches.at(*failure.failedBranch));
                        if (!next || !next->is_bool())
                            return true;
                        const auto nextValue =
                            evaluateWithCounterexample(*next, *failure.counterexample, translator);
                        if (!currentValue.has_value() || !nextValue.has_value())
                            return true;
                        return !*currentValue || *nextValue;
                    }
                }
            } catch (const z3::exception &) { return true; }
            return true;
        }

        CombinationStatus combinationStatus(VerificationStatus status) {
            switch (status) {
                case VerificationStatus::BudgetExceeded: return CombinationStatus::BudgetExceeded;
                case VerificationStatus::InvalidModel: return CombinationStatus::InvalidModel;
                case VerificationStatus::SolverUnavailable:
                    return CombinationStatus::SolverUnavailable;
                case VerificationStatus::Unsupported: return CombinationStatus::Unsupported;
                case VerificationStatus::Unknown: return CombinationStatus::Unknown;
                case VerificationStatus::Proved: return CombinationStatus::Found;
                case VerificationStatus::Refuted: return CombinationStatus::NoInvariant;
            }
            return CombinationStatus::NoInvariant;
        }
    } // namespace

    VerificationResult verifyInvariant(const TransitionModel &model,
                                       const symbolic::Expr &invariant,
                                       const ClauseLimits &limits) {
        QueryBudget budget{limits.maxSolverQueries};
        return verifyWithBudget(model, invariant, limits, budget);
    }

    ClauseCombinationResult combineInvariantClauses(const TransitionModel &model,
                                                    const std::vector<symbolic::Expr> &atomicClauses,
                                                    const ClauseLimits &limits) {
        if (auto error = validateTransitionModel(model)) {
            return {CombinationStatus::InvalidModel, std::nullopt, 0, 0, 0, 0, *error};
        }
        if (atomicClauses.empty())
            return {CombinationStatus::NoInvariant, std::nullopt, 0, 0, 0, 0, "no atomic clauses"};
        if (atomicClauses.size() > limits.maxAtomicClauses) {
            return {CombinationStatus::BudgetExceeded, std::nullopt, 0, 0, 0, 0,
                    "atomic clause limit exceeded"};
        }

        const auto &factory = model.variables.front().current.factory();
        std::vector<symbolic::Expr> candidates;
        candidates.reserve(limits.maxCandidateExpressions);
        auto addCandidate = [&](const symbolic::Expr &candidate) {
            if (candidates.size() >= limits.maxCandidateExpressions ||
                containsExpression(candidates, candidate) ||
                (limits.requireChangedStateVariable &&
                 !dependsOnChangedStateVariable(model, candidate)))
                return;
            candidates.push_back(candidate);
        };

        for (const auto &atom : atomicClauses) {
            if (&atom.factory() != &factory) {
                return {CombinationStatus::InvalidModel,
                        std::nullopt,
                        candidates.size(),
                        0,
                        0,
                        0,
                        "atomic clause uses a different expression factory"};
            }
            addCandidate(atom);
        }
        for (std::size_t left = 0; left < atomicClauses.size(); ++left) {
            for (std::size_t right = left + 1; right < atomicClauses.size(); ++right) {
                addCandidate(atomicClauses[left].logicalAnd(atomicClauses[right]));
                addCandidate(atomicClauses[left].logicalOr(atomicClauses[right]));
            }
        }

        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const auto &left, const auto &right) {
                             return expressionSize(left) < expressionSize(right);
                         });

        QueryBudget budget{limits.maxSolverQueries};
        std::vector<VerificationResult> failures;
        std::size_t attempted = 0;
        std::size_t filtered  = 0;
        bool sawUnknown       = false;
        bool sawUnsupported   = false;
        std::string lastReason;
        std::optional<VerificationResult> lastFailure;

        for (const auto &candidate : candidates) {
            const auto repairsAll =
                std::all_of(failures.begin(), failures.end(), [&](const auto &failure) {
                    return repairsCounterexample(model, candidate, failure);
                });
            if (!repairsAll) {
                ++filtered;
                continue;
            }

            ++attempted;
            auto verification = verifyWithBudget(model, candidate, limits, budget);
            lastReason        = verification.reason;
            if (verification.status == VerificationStatus::Proved) {
                return {CombinationStatus::Found,
                        candidate,
                        candidates.size(),
                        attempted,
                        filtered,
                        budget.used,
                        {}};
            }
            if (verification.status == VerificationStatus::BudgetExceeded) {
                return {CombinationStatus::BudgetExceeded,
                        std::nullopt,
                        candidates.size(),
                        attempted,
                        filtered,
                        budget.used,
                        verification.reason};
            }
            if (verification.status == VerificationStatus::InvalidModel ||
                verification.status == VerificationStatus::SolverUnavailable) {
                return {combinationStatus(verification.status),
                        std::nullopt,
                        candidates.size(),
                        attempted,
                        filtered,
                        budget.used,
                        verification.reason};
            }
            sawUnknown |= verification.status == VerificationStatus::Unknown;
            sawUnsupported |= verification.status == VerificationStatus::Unsupported;
            if (verification.status == VerificationStatus::Refuted)
                lastFailure = verification;
            if (verification.counterexample)
                failures.push_back(std::move(verification));
        }

        const auto status = sawUnknown ? CombinationStatus::Unknown
                                       : (sawUnsupported ? CombinationStatus::Unsupported
                                                         : CombinationStatus::NoInvariant);
        return {status,   std::nullopt, candidates.size(),     attempted,
                filtered, budget.used,  std::move(lastReason), std::move(lastFailure)};
    }
#else
    VerificationResult verifyInvariant(const TransitionModel &,
                                       const symbolic::Expr &,
                                       const ClauseLimits &) {
        return {VerificationStatus::SolverUnavailable, std::nullopt, std::nullopt, std::nullopt, 0,
                "Z3 support is not available"};
    }

    ClauseCombinationResult combineInvariantClauses(const TransitionModel &,
                                                    const std::vector<symbolic::Expr> &,
                                                    const ClauseLimits &) {
        return {CombinationStatus::SolverUnavailable, std::nullopt, 0, 0, 0, 0,
                "Z3 support is not available"};
    }
#endif
} // namespace acslg::analyzer::invariant
