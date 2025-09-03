#include <memory>
#include <sstream>
#include <cstring>
#include <variant>
#include <llvm/ADT/TypeSwitch.h>
#include "expr.h"
#include "macros.h"

using namespace std;
using namespace Symbolic;
using namespace clang;
using namespace llvm;

std::unique_ptr<UnknownExpr> UnknownExpr::makeUnknown() { return std::make_unique<UnknownExpr>(); }

std::unique_ptr<SymbolicExpr> SymbolicExpr::simplifiedExprIfLinear() const {
    if (!isLinear())
        return clone();
    auto idVarAndAddrMap = collectUsedVarsAndAddrs();
    auto linearExpr      = toLinearExpr();
    unique_ptr<SymbolicExpr> result{nullptr};

    using enum BinaryOpExpr::Operator;
    for (auto [id, varOrAddr] : idVarAndAddrMap) {
        std::visit(
            [&](auto &&arg) {
                auto C = linearExpr.coefficient(Parma_Polyhedra_Library::Variable{id}).get_si();
                if (C == 0)
                    return;

                if (result == nullptr) {
                    if (C == 1)
                        result = arg->clone();
                    else
                        result = make_unique<BinaryOpExpr>(make_unique<LiteralExpr>(C), Multiply,
                                                           arg->clone());
                } else {
                    unsigned absC = abs(C);
                    unique_ptr<SymbolicExpr> varExpr{nullptr};
                    if (absC != 1)
                        varExpr = make_unique<BinaryOpExpr>(make_unique<LiteralExpr>(absC),
                                                            Multiply, arg->clone());
                    else
                        varExpr = arg->clone();
                    result = make_unique<BinaryOpExpr>(std::move(result), (C > 0 ? Add : Subtract),
                                                       std::move(varExpr));
                }
            },
            varOrAddr);
    }
    if (auto inhomo = linearExpr.inhomogeneous_term().get_si(); inhomo || result == nullptr) {
        if (result != nullptr) {
            result = make_unique<BinaryOpExpr>(std::move(result), (inhomo > 0 ? Add : Subtract),
                                               std::make_unique<LiteralExpr>(abs(inhomo)));
        } else
            result = make_unique<LiteralExpr>(inhomo);
    }
    if (result == nullptr) {
        ERROR("Simplified expr is null! Something goes wrong.");
    }
    return result;
}

std::unique_ptr<SymbolicExpr> LiteralExpr::clone() const {
    switch (getLiteralType()) {
        case LiteralType::Boolean: return std::make_unique<LiteralExpr>(data_.boolValue);
        case LiteralType::Int: return std::make_unique<LiteralExpr>(data_.intValue);
        case LiteralType::UnsignedInt: return std::make_unique<LiteralExpr>(data_.uintValue);
        case LiteralType::Short: return std::make_unique<LiteralExpr>(data_.shortValue);
        case LiteralType::UnsignedShort: return std::make_unique<LiteralExpr>(data_.ushortValue);
        case LiteralType::Int64: return std::make_unique<LiteralExpr>(data_.int64Value);
        case LiteralType::UInt64: return std::make_unique<LiteralExpr>(data_.uint64Value);
    }

    UNREACHABLE();
}

unique_ptr<SymbolicExpr> BinaryOpExpr::clone() const {
    return make_unique<BinaryOpExpr>(left_->clone(), op_, right_->clone());
}

unique_ptr<SymbolicExpr> UnaryOpExpr::clone() const {
    return make_unique<UnaryOpExpr>(op_, expr_->clone());
}

unique_ptr<SymbolicExpr> UnknownExpr::clone() const { return make_unique<UnknownExpr>(); }

std::unique_ptr<SymbolicExpr> Symbolic::Variable::clone() const {
    return std::visit(
        [this](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return std::make_unique<Symbolic::Variable>(name_, varType_, id_, std::monostate{});
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                return std::make_unique<Symbolic::Variable>(name_, varType_, id_,
                                                            std::make_unique<Address>(*arg));
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                return std::make_unique<Symbolic::Variable>(name_, varType_, id_, arg);
            }
        },
        from_);
}

std::unique_ptr<SymbolicExpr> Address::clone() const { return make_unique<Address>(*this); }

std::unique_ptr<SymbolicExpr> Structure::clone() const {
    return std::make_unique<Structure>(*this);
}

int64_t LiteralExpr::getLiteralValue() const {
    switch (getLiteralType()) {
        case LiteralType::Boolean: return data_.boolValue;
        case LiteralType::Int: return data_.intValue;
        case LiteralType::UnsignedInt: return data_.uintValue;
        case LiteralType::Short: return data_.shortValue;
        case LiteralType::UnsignedShort: return data_.ushortValue;
        case LiteralType::Int64: return data_.int64Value;
        case LiteralType::UInt64: return data_.uint64Value;
    }

    UNREACHABLE();
    return 0;
}

std::size_t LiteralExpr::hash() const {
    std::size_t seed = hash_val(getType(), type_);

    switch (type_) {
        using enum LiteralType;
        case Boolean: return acslg::hash_val(seed, data_.boolValue);
        case Int: return acslg::hash_val(seed, data_.intValue);
        case UnsignedInt: return acslg::hash_val(seed, data_.uintValue);
        case Short: return acslg::hash_val(seed, data_.shortValue);
        case UnsignedShort: return acslg::hash_val(seed, data_.ushortValue);
        case Int64: return acslg::hash_val(seed, data_.int64Value);
        case UInt64: return acslg::hash_val(seed, data_.uint64Value);
        default: ERROR("Wrong type.");
    }
}

std::size_t Symbolic::Variable::hash() const {
    size_t seed = hash_val(getType());
    std::visit(
        [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                /* do nothing */
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                seed = acslg::hash_val(seed, arg->hash());
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                seed = acslg::hash_val(seed, arg.first->hash(), arg.second);
            }
        },
        from_);
    return seed;
}

std::size_t UnaryOpExpr::hash() const {
    return hash_val(getType(), static_cast<std::size_t>(op_), expr_->hash());
}

std::size_t BinaryOpExpr::hash() const {
    return hash_val(getType(), static_cast<std::size_t>(op_), left_->hash(), right_->hash());
}

std::size_t Address::hash() const {
    std::size_t seed = hash_val(getType(), offset_ ? offset_.value()->hash() : 0,
                                range_ ? range_.value().len_->hash() : 0);

    std::visit(
        [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                /* do nothing */
            } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                seed = acslg::hash_val(seed, arg.get());
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                seed = acslg::hash_val(seed, arg->hash());
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                seed = acslg::hash_val(seed, arg.first->hash(), arg.second);
            }
        },
        from_);
    return seed;
}

std::size_t Structure::Info::hash() const {
    std::size_t seed = acslg::hash_val(definition_.get());
    std::visit(
        [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                /* do nothing */
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                seed = acslg::hash_val(seed, arg->hash());
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                seed = acslg::hash_val(seed, arg.first->hash(), arg.second);
            }
        },
        from_);
    return seed;
}

std::size_t Structure::hash() const {
    if (!isComplete())
        ERROR("Should only hash complete structure");
    auto seed = acslg::hash_val(getType(), info_->hash());
    for (auto &field : fields_)
        seed = acslg::hash_val(seed, field.value()->hash());
    return seed;
}

std::size_t UnknownExpr::hash() const { return hash_val(getType()); }

std::string LiteralExpr::dump() const {
    std::ostringstream oss;
    switch (getLiteralType()) {
        case LiteralType::Boolean:
            oss << "Boolean(" << (data_.boolValue ? "true" : "false") << ")";
            break;
        case LiteralType::Int: oss << "Int(" << data_.intValue << ")"; break;
        case LiteralType::UnsignedInt: oss << "UnsignedInt(" << data_.uintValue << ")"; break;
        case LiteralType::Short: oss << "Short(" << data_.shortValue << ")"; break;
        case LiteralType::UnsignedShort: oss << "UnsignedShort(" << data_.ushortValue << ")"; break;
        case LiteralType::Int64: oss << "Int64(" << data_.int64Value << ")"; break;
        case LiteralType::UInt64: oss << "Uint64(" << data_.uint64Value << ")"; break;
    }

    return oss.str();
}

std::string BinaryOpExpr::dump() const {
    std::ostringstream oss;
    std::string opStr;
    switch (op_) {
        case Operator::Multiply: opStr = "*"; break;
        case Operator::Divide: opStr = "/"; break;
        case Operator::Remainder: opStr = "%"; break;
        case Operator::Add: opStr = "+"; break;
        case Operator::Subtract: opStr = "-"; break;
        case Operator::ShiftLeft: opStr = "<<"; break;
        case Operator::ShiftRight: opStr = ">>"; break;
        case Operator::LessThan: opStr = "<"; break;
        case Operator::GreaterThan: opStr = ">"; break;
        case Operator::LessEqual: opStr = "<="; break;
        case Operator::GreaterEqual: opStr = ">="; break;
        case Operator::Equal: opStr = "=="; break;
        case Operator::NotEqual: opStr = "!="; break;
        case Operator::BitAnd: opStr = "&"; break;
        case Operator::BitXor: opStr = "^"; break;
        case Operator::BitOr: opStr = "|"; break;
        case Operator::LogicalAnd: opStr = "&&"; break;
        case Operator::LogicalOr: opStr = "||"; break;
        default: opStr = "?"; break;
    }
    oss << "(" << left_->dump() << " " << opStr << " " << right_->dump() << ")";
    return oss.str();
}

std::string UnaryOpExpr::dump() const {
    std::ostringstream oss;
    std::string opStr;
    switch (op_) {
        case Operator::Plus: opStr = "+"; break;
        case Operator::Minus: opStr = "-"; break;
        case Operator::LogicalNot: opStr = "!"; break;
        case Operator::BitwiseNot: opStr = "~"; break;
        case Operator::PreInc: opStr = "++"; break;
        case Operator::PreDec: opStr = "--"; break;
        case Operator::PostInc: opStr = "++"; break;
        case Operator::PostDec: opStr = "--"; break;
        case Operator::AddrOf: opStr = "&"; break;
        case Operator::Dereference: opStr = "*"; break;
        default: opStr = "?"; break;
    }
    oss << opStr << "(" << expr_->dump() << ")";
    return oss.str();
}

std::string UnknownExpr::dump() const { return "{unknown}"; }

std::string Symbolic::Variable::dump() const {
    std::ostringstream oss;
    const auto &t = getValType();

    oss << "Var(" << name_ << "_" << id_ << ", ";

    switch (t.kind) {
        case ScalarKind::Int: oss << "int"; break;
        case ScalarKind::UInt: oss << "uint"; break;
        case ScalarKind::Bool: oss << "bool"; break;
        case ScalarKind::Void: oss << "void"; break;
        case ScalarKind::Structure: ERROR("Variable's ScalarKind should not be Structure");
    }

    oss << t.bitWidth << ")";
    return oss.str();
}

std::string Address::dump() const {
    std::ostringstream oss;
    oss << "Address(" << id_ << ")";
    if (isOffseted()) {
        auto off = getOffset();
        if (range_ == nullopt)
            oss << "[" << off->dump() << "]";
        else
            oss << "[" << off->dump() << "..." << range_.value().len_->dump() << "]";
    }
    return oss.str();
}

std::string Structure::Info::dump() const {
    std::ostringstream oss;
    std::string structName = definition_->getNameAsString();
    uint64_t sizeBits      = static_cast<uint64_t>(layout_.getSize().getQuantity()) * 8;

    oss << "Struct(" << structName << ", size=" << sizeBits << " bits";

    std::visit(
        [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                oss << ", from=Null";
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                oss << ", from=" << arg->dump();
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                oss << ", from={" << arg.first->dump() << ", " << arg.second << "}";
            }
        },
        from_);
    oss << ")";

    return oss.str();
}

std::string Structure::dump() const {
    std::ostringstream oss;

    oss << info_->dump();
    oss << ", fields=[";
    for (size_t i = 0; i < fields_.size(); ++i) {
        if (fields_[i]) {
            oss << fields_[i].value()->dump();
        } else {
            oss << "null";
        }
        if (i + 1 < fields_.size()) {
            oss << ", ";
        }
    }
    oss << "]";

    return oss.str();
}

std::string LiteralExpr::regularForm(std::optional<std::string_view>,
                                     std::optional<std::string_view>,
                                     int,
                                     bool) const {
    std::ostringstream oss;
    switch (getLiteralType()) {
        case LiteralType::Boolean: oss << (data_.boolValue ? "true" : "false"); break;
        case LiteralType::Int: oss << data_.intValue; break;
        case LiteralType::UnsignedInt: oss << data_.uintValue; break;
        case LiteralType::Short: oss << data_.shortValue; break;
        case LiteralType::UnsignedShort: oss << data_.ushortValue; break;
        case LiteralType::Int64: oss << data_.int64Value; break;
        case LiteralType::UInt64: oss << data_.uint64Value; break;
    }
    return oss.str();
}

std::string BinaryOpExpr::regularForm(std::optional<std::string_view> prefix,
                                      std::optional<std::string_view> suffix,
                                      int parentPrec,
                                      bool isRightChild) const {
    std::ostringstream oss;
    std::string opStr;
    switch (op_) {
#define BIN_OP(name, tok, prec, isRight)                                                           \
    case Operator::name: opStr = tok; break;
#include "operators.def"
        default: opStr = "?"; break;
    }

    int myPrec = getPrecedence(op_);
    bool needParens =
        (myPrec < parentPrec) || (myPrec == parentPrec && isRightChild && !isRightAssociative(op_));

    oss << (needParens ? "(" : "") << left_->regularForm(prefix, suffix, myPrec, false) << " "
        << opStr << " " << right_->regularForm(prefix, suffix, myPrec, true)
        << (needParens ? ")" : "");
    return oss.str();
}

std::string UnaryOpExpr::regularForm(std::optional<std::string_view> prefix,
                                     std::optional<std::string_view> suffix,
                                     int parentPrec,
                                     bool) const {
    std::ostringstream oss;
    std::string opStr;
    switch (op_) {
#define UN_OP(name, tok, prec, isRight)                                                            \
    case Operator::name: opStr = tok; break;
#include "operators.def"
        default: opStr = "?"; break;
    }

    int myPrec      = getPrecedence(op_);
    bool needParens = myPrec < parentPrec;

    if (op_ == Operator::PostInc || op_ == Operator::PostDec)
        oss << (needParens ? "(" : "") << expr_->regularForm(prefix, suffix, myPrec, false) << opStr
            << (needParens ? ")" : "");
    else
        oss << (needParens ? "(" : "") << opStr << expr_->regularForm(prefix, suffix, myPrec, true)
            << (needParens ? ")" : "");
    return oss.str();
}

std::string UnknownExpr::regularForm(std::optional<std::string_view>,
                                     std::optional<std::string_view>,
                                     int,
                                     bool) const {
    WARN("Output UnknownExpr's regular form, something may go wrong.");
    return "{unknown}";
}

std::string Symbolic::Variable::regularForm(std::optional<std::string_view> prefix,
                                            std::optional<std::string_view> suffix,
                                            int,
                                            bool) const {
    return std::visit(
        [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                ERROR("Trying to get regular form of Variable with nullptr from_.");
                // This path is unreachable, yet the compiler infers std::visit to return void in
                // the absence of a return statement—a strange error.
                return "[If you see this message, check Variable::regularForm.]"s;
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                auto addr = arg->regularForm(prefix, suffix);
                if (addr.length() == 0) {
                    ERROR("Empty regular from.");
                    // This path is unreachable, yet the compiler infers std::visit to return void
                    // in the absence of a return statement—a strange error.
                    return "[If you see this message, check Variable::regularForm.]"s;
                }

                if (addr[0] == '&')
                    return addr.substr(1);
                return "(*" + addr + ")";
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                auto &[st, index] = arg;
                return st->regularFormOfField(index, prefix, suffix);
            }
        },
        from_);
}

std::string Address::regularForm(std::optional<std::string_view> prefix,
                                 std::optional<std::string_view> suffix,
                                 int,
                                 bool) const {
    if (isRange())
        ERROR("Address range has no regularForm but regularFormOfValue.");
    return std::visit(
        [&, this](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                ERROR("Trying to get regular form of address without from_.");
                // This path is unreachable, yet the compiler infers std::visit to return void in
                // the absence of a return statement—a strange error.
                return "[If you see this message, check Address::regularForm.]"s;
            } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                if (isOffseted()) {
                    ERROR("Address from varDecl should not be offseted.");
                    // This path is unreachable, yet the compiler infers std::visit to return void
                    // in the absence of a return statement—a strange error.
                    return "[If you see this message, check Address::regularForm.]"s;
                }
                return "&" + (prefix ? (std::string)*prefix : "") + arg->getNameAsString() +
                       (suffix ? (string)*suffix : "");
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                auto nameStr = arg->regularForm(prefix, suffix);
                auto offsetStr =
                    ((isOffseted()) ? offset_.value()->regularForm(prefix, suffix) : "");
                if (offsetStr == "0")
                    offsetStr = "";
                if (nameStr.empty()) {
                    ERROR("Empty regular from.");
                    // This path is unreachable, yet the compiler infers std::visit to return void
                    // in the absence of a return statement—a strange error.
                    return "[If you see this message, check Address::regularForm.]"s;
                }

                if (nameStr[0] == '&')
                    nameStr = nameStr.substr(1);
                else
                    nameStr = "*" + nameStr;

                if (!offsetStr.empty())
                    return "(" + nameStr + "+" + offsetStr + ")";
                else
                    return nameStr;
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                auto &[st, index] = arg;
                auto nameStr      = st->regularFormOfField(index, prefix, suffix);
                auto offsetStr =
                    ((isOffseted()) ? offset_.value()->regularForm(prefix, suffix) : "");
                if (offsetStr == "0")
                    offsetStr = "";
                if (nameStr.empty()) {
                    ERROR("Empty regular from.");
                    // This path is unreachable, yet the compiler infers std::visit to return void
                    // in the absence of a return statement—a strange error.
                    return "[If you see this message, check Address::regularForm.]"s;
                }

                if (nameStr[0] == '&')
                    nameStr = nameStr.substr(1);
                else
                    nameStr = "*" + nameStr;

                if (!offsetStr.empty())
                    return "(" + nameStr + "+" + offsetStr + ")";
                else
                    return nameStr;
            }
        },
        from_);
}

std::string Address::regularFormOfValue(std::optional<std::string_view> prefix,
                                        std::optional<std::string_view> suffix,
                                        int,
                                        bool) const {
    if (!isRange()) {
        string s = regularForm(prefix, suffix);
        if (s.empty())
            ERROR("Empty regular form.");
        if (s[0] == '&')
            return s.substr(1);
        else
            return "*(" + s + ")";
    } else {
        return std::visit(
            [&, this](auto &&arg) -> std::string {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    ERROR("Trying to get regular form of address range without from_.");
                } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                    ERROR("Address range should not from varDecl*.");
                } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                    auto nameStr = arg->regularFormOfValue(prefix, suffix);
                    auto offsetStr =
                        "[" + getOffset()->regularForm(prefix, suffix) + "..." +
                        make_unique<BinaryOpExpr>(getOffset()->clone(), BinaryOpExpr::Operator::Add,
                                                  range_.value().len_->clone())
                            ->simplifiedExpr()
                            ->regularForm() +
                        "]";
                    if (nameStr.empty())
                        ERROR("Empty regular from.");

                    return nameStr + offsetStr;
                } else if constexpr (std::is_same_v<
                                         T, std::pair<not_null<shared_ptr<const Structure::Info>>,
                                                      const size_t>>) {
                    auto &[st, index] = arg;
                    auto nameStr      = st->regularFormOfField(index, prefix, suffix);
                    auto offsetStr    = "[" + getOffset()->regularForm(prefix, suffix) + "..." +
                                     range_.value().len_->regularForm(prefix, suffix) + "]";
                    if (nameStr.empty()) {
                        ERROR("Empty regular from.");
                    }

                    return nameStr + offsetStr;
                }
            },
            Address::getFrom());
    }
}

std::string Symbolic::Structure::Info::regularForm(std::optional<std::string_view> prefix,
                                                   std::optional<std::string_view> suffix,
                                                   int,
                                                   bool) const {
    return std::visit(
        [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                ERROR("Trying to get regular form of Structure with nullptr from_.");
                // This path is unreachable, yet the compiler infers std::visit to return void
                // in the absence of a return statement—a strange error.
                return "[If you see this message, check Structure::Info::regularForm.]"s;
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                auto addr = arg->regularForm(prefix, suffix);
                if (addr.length() == 0) {
                    ERROR("Empty regular from.");
                    // This path is unreachable, yet the compiler infers std::visit to return void
                    // in the absence of a return statement—a strange error.
                    return "[If you see this message, check Structure::Info::regularForm.]"s;
                }

                if (addr[0] == '&')
                    return addr.substr(1);
                return "(*" + addr + ")";
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                auto &[st, index] = arg;
                return st->regularFormOfField(index, prefix, suffix);
            }
        },
        from_);
}

std::string Symbolic::Structure::regularForm(std::optional<std::string_view> prefix,
                                             std::optional<std::string_view> suffix,
                                             int,
                                             bool) const {
    return info_->regularForm(prefix, suffix);
}

std::unique_ptr<SymbolicExpr> LiteralExpr::simplifiedExpr() const {
    return simplifiedExprIfLinear();
}

std::unique_ptr<SymbolicExpr> BinaryOpExpr::simplifiedExpr() const {
    if (isUnknown())
        return UnknownExpr::makeUnknown();
    if (isLinear())
        return simplifiedExprIfLinear();
    auto LHS = left_->simplifiedExpr();
    auto RHS = right_->simplifiedExpr();
    return make_unique<BinaryOpExpr>(std::move(LHS), op_, std::move(RHS));
}

std::unique_ptr<SymbolicExpr> UnaryOpExpr::simplifiedExpr() const {
    if (isUnknown())
        return UnknownExpr::makeUnknown();
    if (isLinear())
        return simplifiedExprIfLinear();
    auto subExpr = expr_->simplifiedExpr();
    return make_unique<UnaryOpExpr>(op_, std::move(subExpr));
}

std::unique_ptr<SymbolicExpr> UnknownExpr::simplifiedExpr() const { return makeUnknown(); }

std::unique_ptr<SymbolicExpr> Symbolic::Variable::simplifiedExpr() const {
    return simplifiedExprIfLinear();
}

std::unique_ptr<SymbolicExpr> Address::simplifiedExpr() const {
    if (isRange())
        ERROR("Address range is solely for address representation and should not be "
              "used as an expression.");
    return clone();
}
std::unique_ptr<SymbolicExpr> Structure::simplifiedExpr() const { return clone(); }

bool LiteralExpr::equal(const SymbolicExpr &expr) const {
    const auto liter = dynamic_cast<const LiteralExpr *>(&expr);
    if (!liter)
        return false;

    return type_ == liter->type_ && getLiteralValue() == liter->getLiteralValue();
}

bool BinaryOpExpr::equal(const SymbolicExpr &expr) const {
    const auto binary = dynamic_cast<const BinaryOpExpr *>(&expr);
    if (!binary)
        return false;

    return *left_ == *(binary->left_) && op_ == binary->op_ && *right_ == *(binary->right_);
}

bool UnaryOpExpr::equal(const SymbolicExpr &expr) const {
    const auto unary = dynamic_cast<const UnaryOpExpr *>(&expr);
    if (!unary)
        return false;

    return op_ == unary->op_ && *expr_ == *(unary->expr_);
}

bool UnknownExpr::equal(const SymbolicExpr &) const { return false; }

bool Symbolic::Variable::equal(const SymbolicExpr &expr) const {
    const auto var = dynamic_cast<const Variable *>(&expr);
    if (!var)
        return false;

    return std::visit(
        [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return false;
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                if (auto addr = std::get_if<not_null<std::unique_ptr<const Address>>>(&var->from_))
                    return *arg == **addr;
                else
                    return false;
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                if (auto addr = std::get_if<
                        std::pair<not_null<std::shared_ptr<const Structure::Info>>, const size_t>>(
                        &var->from_))
                    return arg.first->equal(*(addr->first)) && arg.second == addr->second;
                else
                    return false;
            }
        },
        from_);
}

bool Address::equal(const SymbolicExpr &expr) const {
    const auto addr = dynamic_cast<const Address *>(&expr);
    if (!addr)
        return false;

    bool flag = false;
    // compare base
    std::visit(
        [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                /* do nothing */
            } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                if (auto varAddr = std::get_if<not_null<const clang::VarDecl *>>(&addr->from_);
                    varAddr != nullptr && arg == *varAddr) {
                    flag = true;
                }
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                if (auto ptrAddr =
                        std::get_if<not_null<std::unique_ptr<const Address>>>(&addr->from_);
                    ptrAddr != nullptr && *arg == **ptrAddr) {
                    flag = true;
                }
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                if (auto pairAddr = std::get_if<
                        std::pair<not_null<std::shared_ptr<const Structure::Info>>, const size_t>>(
                        &addr->from_);
                    pairAddr != nullptr && arg.first->equal(*(pairAddr->first)) &&
                    arg.second == pairAddr->second)
                    flag = true;
            }
        },
        from_);
    if (!flag) {
        return false;
    };
    // compare offset
    if (isOffseted() && addr->isOffseted()) {
        if (*offset_.value() != *addr->getOffset())
            return false;
    } else if (isOffseted() ^ addr->isOffseted()) {
        return false;
    };

    // compare range
    if (range_ != nullopt && addr->range_ != nullopt) {
        if (*range_.value().len_ != *(addr->range_.value().len_))
            return false;
    } else if ((range_ == nullopt) ^ (addr->range_ == nullopt)) {
        return false;
    }
    return true;
}

const clang::VarDecl *Address::getFromRoot() const {
    return std::visit(
        [](auto &&arg) -> const clang::VarDecl * {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return nullptr;
            } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                return arg;
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                return arg->getFromRoot();
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                return arg.first->getFromRoot();
            }
        },
        from_);
}

const clang::VarDecl *Structure::Info::getFromRoot() const {
    return std::visit(
        [&](auto &&arg) -> const clang::VarDecl * {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return nullptr;
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                return arg->getFromRoot();
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                return arg.first->getFromRoot();
            }
        },
        from_);
}

const clang::VarDecl *Symbolic::Variable::getFromRoot() const {
    return std::visit(
        [&](auto &&arg) -> const clang::VarDecl * {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return nullptr;
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                return arg->getFromRoot();
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                return arg.first->getFromRoot();
            }
        },
        from_);
}

bool Structure::Info::equal(const Structure::Info &other) const {
    if (definition_ != other.definition_)
        return false;

    return std::visit(
        [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return false;
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                if (auto addr = std::get_if<not_null<std::unique_ptr<const Address>>>(&other.from_))
                    return *arg == **addr;
                else
                    return false;
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                if (auto addr = std::get_if<
                        std::pair<not_null<std::shared_ptr<const Structure::Info>>, const size_t>>(
                        &other.from_))
                    return arg.first->equal(*(addr->first)) && arg.second == addr->second;
                else
                    return false;
            }
        },
        from_);
}

bool Structure::equal(const SymbolicExpr &expr) const {
    const auto st = dynamic_cast<const Structure *>(&expr);
    if (!st)
        return false;
    if (!info_->equal(*(st->info_)))
        return false;
    if (!isComplete() || !st->isComplete())
        ERROR("Should only compare complete structure");
    return std::ranges::equal(fields_, st->fields_,
                              [](auto &lhs, auto &rhs) { return *lhs.value() == *rhs.value(); });
}

std::unique_ptr<Address> BinaryOpExpr::tryEvalAsOffsetedAddr() const {
    auto lhs = left_->tryEvalAsOffsetedAddr(), rhs = right_->tryEvalAsOffsetedAddr();
    if (lhs && rhs)
        return nullptr;
    if (lhs == nullptr && rhs == nullptr)
        return nullptr;

    std::unique_ptr<Address> addr;
    if (lhs) {
        addr = std::move(lhs);
        if (!isValidOffsetOrLength(*right_))
            return nullptr;
        std::unique_ptr<SymbolicExpr> expr = right_->clone();
        switch (op_) {
            using enum Operator;
            case Add: addr->addOffset(std::move(expr)); break;
            case Subtract: addr->subOffset(std::move(expr)); break;

            default: return nullptr;
        }
    } else {
        addr = std::move(rhs);
        if (!isValidOffsetOrLength(*left_))
            return nullptr;
        std::unique_ptr<SymbolicExpr> expr = left_->clone();
        switch (op_) {
            using enum Operator;
            case Add: addr->addOffset(std::move(expr)); break;
            case Subtract: return nullptr;
            default: return nullptr;
        }
    }
    return addr;
}

std::unique_ptr<Address> Address::tryEvalAsOffsetedAddr() const {
    auto result = make_unique<Address>(*this);
    if (!isOffseted())
        result->setOffset(make_unique<LiteralExpr>(Address::ZERO_OFFSET));
    return result;
}

unordered_map<unsigned int, std::variant<const Symbolic::Variable *, const Symbolic::Address *>> Symbolic::
    Variable::collectUsedVarsAndAddrs() const {
    return {{id_, this}};
}

unordered_map<unsigned int, std::variant<const Symbolic::Variable *, const Symbolic::Address *>> BinaryOpExpr::
    collectUsedVarsAndAddrs() const {
    auto lmap = left_->collectUsedVarsAndAddrs();
    auto rmap = right_->collectUsedVarsAndAddrs();
    lmap.insert(make_move_iterator(rmap.begin()), make_move_iterator(rmap.end()));
    return lmap;
}

unordered_map<unsigned int, std::variant<const Symbolic::Variable *, const Symbolic::Address *>> UnaryOpExpr::
    collectUsedVarsAndAddrs() const {
    return expr_->collectUsedVarsAndAddrs();
}

unordered_map<unsigned int, std::variant<const Symbolic::Variable *, const Symbolic::Address *>> Symbolic::
    Address::collectUsedVarsAndAddrs() const {
    if (isRange())
        ERROR("Address range is solely for address representation and should not be "
              "used as an expression.");
    return {{id_, this}};
}

Address::Address(const Address &other)
    : SymbolicExpr(other), id_(other.id_), offset_(nullopt), range_(other.range_) {
    if (other.offset_)
        offset_.emplace(other.offset_.value()->clone());
    std::visit(
        [this](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                from_ = std::monostate{};
            } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                from_ = arg;
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                from_.emplace<2>(
                    std::unique_ptr<Address>{static_cast<Address *>(arg->clone().release())});
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                from_.emplace<3>(arg);
            }
        },
        other.from_);
}

Address &Address::operator=(const Address &other) {
    if (this != &other) {
        SymbolicExpr::operator=(other);
        id_     = other.id_;
        offset_ = nullopt;
        if (other.offset_)
            offset_.emplace(other.offset_.value()->clone());
        range_ = other.range_;
        std::visit(
            [this](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    from_ = std::monostate{};
                } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                    from_ = arg;
                } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                    from_.emplace<2>(
                        std::unique_ptr<Address>{static_cast<Address *>(arg->clone().release())});
                } else if constexpr (std::is_same_v<
                                         T,
                                         std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                   const size_t>>) {
                    from_.emplace<3>(arg);
                }
            },
            other.from_);
    }
    return *this;
}

Address &Address::operator=(Address &&other) {
    if (this == &other)
        return *this;
    SymbolicExpr::operator=(other);
    id_     = std::move(other.id_);
    offset_ = std::move(other.offset_);
    range_  = std::move(other.range_);
    std::visit(
        [this](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                from_ = std::monostate{};
            } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                from_ = arg;
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                from_.emplace<2>(std::move(arg));
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                from_.emplace<3>(std::move(arg));
            }
        },
        other.from_);
    return *this;
}

Address::Address(unsigned int id,
                 std::variant<std::monostate,
                              const clang::VarDecl *,
                              std::unique_ptr<const Address>,
                              std::pair<std::shared_ptr<const Structure::Info>, const size_t>> from,
                 std::unique_ptr<const SymbolicExpr> offset,
                 std::unique_ptr<const SymbolicExpr> length)
    : SymbolicExpr(ExprType::SymbolAddress, {ScalarKind::UInt, 64}), id_(id), offset_(nullopt) {
    if (offset != nullptr)
        offset_.emplace(std::move(offset));
    if (length == nullptr) {
        // Normal address.
    } else if (offset_ != nullopt) {
        // Address Range.
        range_ = Range{id, std::move(length)};
    } else {
        ERROR("Address range should have both non-null offset and length");
    }
    std::visit(
        [this](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                from_ = std::monostate{};
            } else if constexpr (std::is_same_v<T, const clang::VarDecl *>) {
                from_ = arg;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<const Address>>) {
                from_.emplace<2>(std::move(arg));
            } else if constexpr (std::is_same_v<T, std::pair<std::shared_ptr<const Structure::Info>,
                                                             const size_t>>) {
                from_.emplace<3>(std::move(arg));
            }
        },
        from);
}

void Address::setOffset(not_null<std::unique_ptr<SymbolicExpr>> offset) {
    if (!isValidOffsetOrLength(*offset))
        ERROR("Invalid offset.");
    offset_ = std::move(offset).into_underlying();
}

void Address::addOffset(not_null<std::unique_ptr<SymbolicExpr>> extra) {
    if (!isValidOffsetOrLength(*extra))
        ERROR("Invalid offset.");
    if (isOffseted())
        offset_.emplace(std::make_unique<BinaryOpExpr>(
                            offset_.value()->clone(), BinaryOpExpr::Operator::Add, std::move(extra))
                            ->simplifiedExpr());
    else
        offset_.emplace(extra->simplifiedExpr());
}

void Address::subOffset(not_null<std::unique_ptr<SymbolicExpr>> extra) {
    if (!isValidOffsetOrLength(*extra))
        ERROR("Invalid offset.");
    if (isOffseted())
        offset_.emplace(std::make_unique<BinaryOpExpr>(offset_.value()->clone(),
                                                       BinaryOpExpr::Operator::Subtract,
                                                       std::move(extra))
                            ->simplifiedExpr());
    else
        offset_.emplace(extra->simplifiedExpr());
}

void Address::setLength(std::unique_ptr<SymbolicExpr> len) {
    if (len == nullptr || !isValidOffsetOrLength(*len))
        ERROR("Invalid Length.");
    if (range_ == nullopt) {
        range_.emplace(id_, std::move(len));
        return;
    }
    range_.value().len_ = std::move(len);
}

int Address::getDimension() const {
    return std::visit(
        [](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return -1;
            } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                return 0;
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                if (auto dim = arg->getDimension(); dim >= 0)
                    return dim + 1;
                return -1;
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                TODO();
                return -1;
            }
        },
        from_);
}

const clang::VarDecl *Address::retrieveVarDecl() const {
    return std::visit(
        [](auto &&arg) -> const clang::VarDecl * {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return nullptr;
            } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                return arg;
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                return arg->retrieveVarDecl();
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                TODO();
                return nullptr;
            }
        },
        from_);
}

std::string Address::getBaseName() const {
    return std::visit(
        [this](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                // This path is unreachable, yet the compiler infers std::visit to return void in
                // the absence of a return statement—a strange error.
                return "[If you see this message, check Address::getBaseName.]"s;
            } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                if (offset_) {
                    ERROR("Address from varDecl should not be offseted.");
                    // This path is unreachable, yet the compiler infers std::visit to return void
                    // in the absence of a return statement—a strange error.
                    return "[If you see this message, check Address::getBaseName.]"s;
                }
                return "&" + arg->getNameAsString();
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                auto nameStr   = arg->getBaseName();
                auto offsetStr = (offset_ ? offset_.value()->dump() : "");
                if (nameStr.length() == 0) {
                    ERROR("Empty name.");
                    // This path is unreachable, yet the compiler infers std::visit to return void
                    // in the absence of a return statement—a strange error.
                    return "[If you see this message, check Address::getBaseName.]"s;
                }

                if (nameStr[0] == '&')
                    nameStr = nameStr.substr(1);
                else
                    nameStr = "*" + nameStr;

                if (offsetStr.length())
                    return "(" + nameStr.substr(1) + "+" + offsetStr + ")";
                else
                    return nameStr;
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                auto &[st, index] = arg;
                auto nameStr      = st->regularFormOfField(index);
                auto offsetStr    = (offset_ ? offset_.value()->dump() : "");
                if (nameStr.length() == 0) {
                    ERROR("Empty name.");
                    // This path is unreachable, yet the compiler infers std::visit to return void
                    // in the absence of a return statement—a strange error.
                    return "[If you see this message, check Address::getBaseName.]"s;
                }

                if (nameStr[0] == '&')
                    nameStr = nameStr.substr(1);
                else
                    nameStr = "*" + nameStr;

                if (offsetStr.length())
                    return "(" + nameStr.substr(1) + "+" + offsetStr + ")";
                else
                    return nameStr;
            }
        },
        from_);
}

Structure::Info::Info(const Info &other) : definition_(other.definition_), layout_(other.layout_) {
    std::visit(
        [this](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                from_ = std::monostate{};
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                from_.emplace<1>(
                    std::unique_ptr<Address>(static_cast<Address *>((arg)->clone().release())));
            } else if constexpr (std::is_same_v<
                                     T, std::pair<not_null<std::shared_ptr<const Structure::Info>>,
                                                  const size_t>>) {
                from_.emplace<2>(arg);
            }
        },
        other.from_);
}

bool Structure::isComplete() const {
    for (auto &field : fields_)
        if (field == nullopt)
            return false;
    return true;
}

void Structure::setFieldValue(size_t index, const SymbolicExpr &expr) {
    if (index >= fields_.size())
        ERROR("Out-of-bounds access");
    fields_[index] = expr.clone();
}

std::unique_ptr<SymbolicExpr> Structure::getFieldValue(size_t index) const {
    if (index >= fields_.size())
        ERROR("Out-of-bounds access");
    return fields_[index] ? fields_[index].value()->clone() : nullptr;
}

std::string Symbolic::Structure::Info::regularFormOfField(size_t index,
                                                          std::optional<std::string_view> prefix,
                                                          std::optional<std::string_view> suffix,
                                                          int,
                                                          bool) const {
    string s = regularForm(prefix, suffix);
    if (s.empty())
        ERROR("Empty regular form.");

    auto fields = definition_->fields();
    auto it     = std::ranges::next(fields.begin(), index, fields.end());
    if (it == fields.end())
        ERROR("Out-of-bounds access");

    auto fieldName = it->getNameAsString();
    return s + "." + fieldName;
}

std::string Symbolic::Structure::regularFormOfField(size_t index,
                                                    std::optional<std::string_view> prefix,
                                                    std::optional<std::string_view> suffix,
                                                    int,
                                                    bool) const {
    return info_->regularFormOfField(index, prefix, suffix);
}

Symbolic::Variable::Variable(const Variable &other)
    : SymbolicExpr(other), name_(other.name_), varType_(other.varType_), id_(other.id_) {
    std::visit(
        [this](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                from_ = std::monostate{};
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                from_.emplace<1>(std::make_unique<Address>(*arg));
            } else if constexpr (std::is_same_v<T, not_null<const Symbolic::Structure::Info *>>) {
                from_.emplace<2>(arg);
            }
        },
        other.from_);
}

namespace std {
    template <> struct hash<Address> {
        std::size_t operator()(const Address &addr) const {
            return std::hash<unsigned int>()(addr.getId());
        }
    };
} // namespace std

std::ostream &operator<<(std::ostream &os, SymbolicExpr::ExprType t) {
    switch (t) {
        using enum SymbolicExpr::ExprType;
        case Literal: os << "Literal"; break;
        case Variable: os << "Variable"; break;
        case SymbolAddress: os << "SymbolAddress"; break;
        case BinaryOp: os << "BinaryOp"; break;
        case UnaryOp: os << "UnaryOp"; break;
        case Structure: os << "Structure"; break;
        case Unknown: os << "Unknown"; break;
    }
    return os;
}

namespace Symbolic {
    unique_ptr<SymbolicExpr> createLNotExpr(not_null<unique_ptr<SymbolicExpr>> expr) {
        return make_unique<UnaryOpExpr>(UnaryOpExpr::Operator::LogicalNot, std::move(expr));
    }

    BinaryOpExpr::Operator getCompoundAssignOp(BinaryOperatorKind compoundAssignOp) {
        switch (compoundAssignOp) {
            case BO_MulAssign: return BinaryOpExpr::Operator::Multiply;
            case BO_DivAssign: return BinaryOpExpr::Operator::Divide;
            case BO_RemAssign: return BinaryOpExpr::Operator::Remainder;
            case BO_AddAssign: return BinaryOpExpr::Operator::Add;
            case BO_SubAssign: return BinaryOpExpr::Operator::Subtract;
            case BO_ShlAssign: return BinaryOpExpr::Operator::ShiftLeft;
            case BO_ShrAssign: return BinaryOpExpr::Operator::ShiftRight;
            case BO_AndAssign: return BinaryOpExpr::Operator::BitAnd;
            case BO_XorAssign: return BinaryOpExpr::Operator::BitXor;
            case BO_OrAssign: return BinaryOpExpr::Operator::BitOr;
            default: UNREACHABLE();
        }
    }

    // No AssignOp Here.
    BinaryOpExpr::Operator getBinaryOp(BinaryOperatorKind op) {
        switch (op) {
            case BO_Mul: return BinaryOpExpr::Operator::Multiply;
            case BO_Div: return BinaryOpExpr::Operator::Divide;
            case BO_Rem: return BinaryOpExpr::Operator::Remainder;
            case BO_Add: return BinaryOpExpr::Operator::Add;
            case BO_Sub: return BinaryOpExpr::Operator::Subtract;
            case BO_Shl: return BinaryOpExpr::Operator::ShiftLeft;
            case BO_Shr: return BinaryOpExpr::Operator::ShiftRight;
            case BO_LT: return BinaryOpExpr::Operator::LessThan;
            case BO_GT: return BinaryOpExpr::Operator::GreaterThan;
            case BO_LE: return BinaryOpExpr::Operator::LessEqual;
            case BO_GE: return BinaryOpExpr::Operator::GreaterEqual;
            case BO_EQ: return BinaryOpExpr::Operator::Equal;
            case BO_NE: return BinaryOpExpr::Operator::NotEqual;
            case BO_And: return BinaryOpExpr::Operator::BitAnd;
            case BO_Xor: return BinaryOpExpr::Operator::BitXor;
            case BO_Or: return BinaryOpExpr::Operator::BitOr;
            case BO_LAnd: return BinaryOpExpr::Operator::LogicalAnd;
            case BO_LOr: return BinaryOpExpr::Operator::LogicalOr;
            case BO_Assign:
            case BO_AddAssign:
            case BO_SubAssign:
            case BO_MulAssign:
            case BO_DivAssign:
            case BO_RemAssign:
            case BO_ShlAssign:
            case BO_ShrAssign:
            case BO_AndAssign:
            case BO_XorAssign:
            case BO_OrAssign: UNREACHABLE();
            default:
                UNIMPLEMENT("Unsupported binary operator: " << op);
                return BinaryOpExpr::Operator::Add;
        }
    }

    SymbolicExpr::Type deriveVarType(QualType type) {
        if (auto ptr = type->getAs<PointerType>())
            return deriveVarType(ptr->getPointeeType());
        return TypeSwitch<QualType, SymbolicExpr::Type>(type.getCanonicalType())
            .Case([](const BuiltinType *BT) -> SymbolicExpr::Type {
                using Kind = SymbolicExpr::ScalarKind;

                switch (BT->getKind()) {
                    case BuiltinType::Bool: return {Kind::Bool, 1};
                    case BuiltinType::Char_S:
                    case BuiltinType::SChar: return {Kind::Int, 8};
                    case BuiltinType::Char_U:
                    case BuiltinType::UChar: return {Kind::UInt, 8};

                    case BuiltinType::Short: return {Kind::Int, 16};
                    case BuiltinType::UShort: return {Kind::UInt, 16};

                    case BuiltinType::Int: return {Kind::Int, 32};
                    case BuiltinType::UInt: return {Kind::UInt, 32};

                    case BuiltinType::Long: return {Kind::Int, 64};
                    case BuiltinType::ULong: return {Kind::UInt, 64};

                    case BuiltinType::LongLong: return {Kind::Int, 64};
                    case BuiltinType::ULongLong: return {Kind::UInt, 64};
                    case BuiltinType::Void: return {Kind::Void, 0};

                    default:
                        LangOptions langOpts;
                        PrintingPolicy pp(langOpts);
                        UNIMPLEMENT("Unsupported builtin type: " << BT->getName(pp).str());
                }
            })
            .Default([&](QualType QT) -> SymbolicExpr::Type {
                if (QT->isStructureType()) {
                    return {SymbolicExpr::ScalarKind::Structure, 0};
                }
                UNIMPLEMENT("Unsupported non-builtin type: " << QT.getAsString());
            });
    }

    bool isValidOffsetOrLength(const SymbolicExpr &expr) {
        if (!expr.isLinear())
            return false;
        if (expr.tryEvalAsOffsetedAddr())
            return false;
        return true;
    }

    bool isFrom(const Symbolic::Address &addr,
                std::variant<not_null<const Symbolic::Variable *>,
                             not_null<const Symbolic::Address *>,
                             not_null<const Symbolic::Structure::Info *>> symbol) {
        return std::visit(
            [&](auto &&arg) -> bool {
                if (auto addrPt =
                        std::get_if<not_null<std::unique_ptr<const Address>>>(&arg->getFrom());
                    addrPt && **addrPt == addr) {
                    return true;
                } else {
                    return false;
                }
            },
            symbol);
    }
} // namespace Symbolic