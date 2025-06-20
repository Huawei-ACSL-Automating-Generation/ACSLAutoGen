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

std::unique_ptr<SymbolicExpr> SymbolicExpr::makeNull() { return std::make_unique<NullExpr>(); }

std::unique_ptr<SymbolicExpr> LiteralExpr::clone() const {
    switch (getLiteralType()) {
        case LiteralType::Boolean: return std::make_unique<LiteralExpr>(data.boolValue);
        case LiteralType::Int: return std::make_unique<LiteralExpr>(data.intValue);
        case LiteralType::UnsignedInt: return std::make_unique<LiteralExpr>(data.uintValue);
        case LiteralType::Short: return std::make_unique<LiteralExpr>(data.shortValue);
        case LiteralType::UnsignedShort: return std::make_unique<LiteralExpr>(data.ushortValue);
        case LiteralType::Int64: return std::make_unique<LiteralExpr>(data.int64Value);
        case LiteralType::UInt64: return std::make_unique<LiteralExpr>(data.uint64Value);
    }

    UNREACHABLE();
}

unique_ptr<SymbolicExpr> BinaryOpExpr::clone() const {
    return make_unique<BinaryOpExpr>(left_->clone(), op_, right_->clone());
}

unique_ptr<SymbolicExpr> UnaryOpExpr::clone() const {
    return make_unique<UnaryOpExpr>(op_, expr_->clone());
}

unique_ptr<SymbolicExpr> NullExpr::clone() const { return make_unique<NullExpr>(); }

std::unique_ptr<SymbolicExpr> Symbolic::Variable::clone() const {
    if (from_)
        return std::make_unique<Symbolic::Variable>(
            name_, varType_, id_,
            std::unique_ptr<Address>(static_cast<Address *>((*from_)->clone().release())));
    return std::make_unique<Symbolic::Variable>(name_, varType_, id_, std::nullopt);
}

std::unique_ptr<SymbolicExpr> Address::clone() const {
    variant<std::monostate, const clang::VarDecl *, unique_ptr<Address>> from;
    if (auto decl = std::get_if<not_null<const clang::VarDecl *>>(&from_)) {
        from = *decl;
    } else if (auto addr = std::get_if<not_null<unique_ptr<Address>>>(&from_)) {
        from = std::unique_ptr<Address>(static_cast<Address *>((*addr)->clone().release()));
    } else {
        from = std::monostate{};
    }
    auto cloned = std::make_unique<Address>(id_, offset_->clone(), std::move(from));
    return cloned;
}

std::size_t LiteralExpr::hash() const {
    std::size_t seed = static_cast<std::size_t>(getType());
    seed ^= static_cast<std::size_t>(type) + 0x9e3779b9;

    switch (type) {
        case LiteralType::Boolean: return seed ^ std::hash<bool>{}(data.boolValue);
        case LiteralType::Int: return seed ^ std::hash<int>{}(data.intValue);
        case LiteralType::UnsignedInt: return seed ^ std::hash<unsigned int>{}(data.uintValue);
        case LiteralType::Short: return seed ^ std::hash<short>{}(data.shortValue);
        case LiteralType::UnsignedShort:
            return seed ^ std::hash<unsigned short>{}(data.ushortValue);
        case LiteralType::Int64: return seed ^ std::hash<int64_t>{}(data.int64Value);
        case LiteralType::UInt64: return seed ^ std::hash<uint64_t>{}(data.uint64Value);
    }
    return seed;
}

int64_t LiteralExpr::getLiteralValue() const {
    switch (getLiteralType()) {
        case LiteralType::Boolean: return data.boolValue;
        case LiteralType::Int: return data.intValue;
        case LiteralType::UnsignedInt: return data.uintValue;
        case LiteralType::Short: return data.shortValue;
        case LiteralType::UnsignedShort: return data.ushortValue;
        case LiteralType::Int64: return data.int64Value;
        case LiteralType::UInt64: return data.uint64Value;
    }

    UNREACHABLE();
    return 0;
}

std::size_t Symbolic::Variable::hash() const {
    std::size_t seed = static_cast<std::size_t>(getType());
    seed ^= std::hash<int>{}(id_);
    seed ^= static_cast<std::size_t>(varType_.kind) + varType_.bitWidth;
    return seed;
}

std::size_t UnaryOpExpr::hash() const {
    std::size_t seed = static_cast<std::size_t>(getType());
    seed ^= static_cast<std::size_t>(op_);
    seed ^= expr_->hash();
    return seed;
}

std::size_t BinaryOpExpr::hash() const {
    std::size_t seed = static_cast<std::size_t>(getType());
    seed ^= left_->hash();
    seed ^= static_cast<std::size_t>(op_) + 0x9e3779b9;
    seed ^= right_->hash();
    return seed;
}

std::size_t Address::hash() const {
    std::size_t seed = static_cast<std::size_t>(getType());
    seed ^= std::hash<unsigned int>{}(id_);
    seed ^= offset_ ? offset_->hash() : 0;
    return seed;
}

std::size_t NullExpr::hash() const { return static_cast<std::size_t>(getType()); }

std::string LiteralExpr::dump() const {
    std::ostringstream oss;
    switch (getLiteralType()) {
        case LiteralType::Boolean:
            oss << "Boolean(" << (data.boolValue ? "true" : "false") << ")";
            break;
        case LiteralType::Int: oss << "Int(" << data.intValue << ")"; break;
        case LiteralType::UnsignedInt: oss << "UnsignedInt(" << data.uintValue << ")"; break;
        case LiteralType::Short: oss << "Short(" << data.shortValue << ")"; break;
        case LiteralType::UnsignedShort: oss << "UnsignedShort(" << data.ushortValue << ")"; break;
        case LiteralType::Int64: oss << "Int64(" << data.int64Value << ")"; break;
        case LiteralType::UInt64: oss << "Uint64(" << data.uint64Value << ")"; break;
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

std::string NullExpr::dump() const { return "null"; }

std::string Symbolic::Variable::dump() const {
    std::ostringstream oss;
    const auto &t = getValType();

    oss << "Var(" << name_ << "_" << id_ << ", ";

    switch (t.kind) {
        case ScalarKind::Int: oss << "int"; break;
        case ScalarKind::UInt: oss << "uint"; break;
        case ScalarKind::Bool: oss << "bool"; break;
        case ScalarKind::Void: oss << "void"; break;
    }

    oss << t.bitWidth << ")";
    return oss.str();
}

std::string Address::dump() const {
    std::ostringstream oss;
    oss << "Address(" << id_ << ")";
    SymbolicExpr *off = getOffset();
    if (dynamic_cast<NullExpr *>(off) == nullptr) {
        oss << "[" << off->dump() << "]";
    }
    return oss.str();
}

std::string LiteralExpr::regularForm(
    std::optional<std::reference_wrapper<const std::string>>,
    std::optional<std::reference_wrapper<const std::string>>) const {
    std::ostringstream oss;
    switch (getLiteralType()) {
        case LiteralType::Boolean: oss << (data.boolValue ? "true" : "false"); break;
        case LiteralType::Int: oss << data.intValue; break;
        case LiteralType::UnsignedInt: oss << data.uintValue; break;
        case LiteralType::Short: oss << data.shortValue; break;
        case LiteralType::UnsignedShort: oss << data.ushortValue; break;
        case LiteralType::Int64: oss << data.int64Value; break;
        case LiteralType::UInt64: oss << data.uint64Value; break;
    }
    return oss.str();
}

std::string BinaryOpExpr::regularForm(
    std::optional<std::reference_wrapper<const std::string>> prefix,
    std::optional<std::reference_wrapper<const std::string>> suffix) const {
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
    oss << "(" << left_->regularForm(prefix, suffix) << " " << opStr << " "
        << right_->regularForm(prefix, suffix) << ")";
    return oss.str();
}

std::string UnaryOpExpr::regularForm(
    std::optional<std::reference_wrapper<const std::string>> prefix,
    std::optional<std::reference_wrapper<const std::string>> suffix) const {
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
    oss << opStr << "(" << expr_->regularForm(prefix, suffix) << ")";
    return oss.str();
}

std::string NullExpr::regularForm(std::optional<std::reference_wrapper<const std::string>>,
                                  std::optional<std::reference_wrapper<const std::string>>) const {
    WARN("Output NullExpr's regular form, something may go wrong.");
    return "";
}

std::string Symbolic::Variable::regularForm(
    std::optional<std::reference_wrapper<const std::string>> prefix,
    std::optional<std::reference_wrapper<const std::string>> suffix) const {
    if (from_ == nullopt) {
        ERROR("Trying to get regular form of Variable with nullopt from_.");
    }
    auto addr = (*from_)->regularForm(prefix, suffix);
    if (addr.length() == 0)
        ERROR("Empty regular from.");

    if (addr[0] == '&')
        return addr.substr(1);
    return "(*" + addr + ")";
}

std::string Address::regularForm(
    std::optional<std::reference_wrapper<const std::string>> prefix,
    std::optional<std::reference_wrapper<const std::string>> suffix) const {
    if (const auto varDeclPtr = std::get_if<not_null<const clang::VarDecl *>>(&from_)) {
        if (isOffseted())
            ERROR("Address from varDecl should not be offseted.");
        return "&" + (prefix ? (std::string)*prefix : "") + (*varDeclPtr)->getNameAsString() +
               (suffix ? (string)*suffix : "");
    } else if (auto addrPtr = std::get_if<not_null<std::unique_ptr<Address>>>(&from_)) {
        auto pre = (*addrPtr)->regularForm(prefix, suffix);
        auto suf = ((isOffseted()) ? offset_->regularForm(prefix, suffix) : "");
        if (suf == "0")
            suf = "";
        if (pre.empty())
            ERROR("Empty regular from.");

        if (pre[0] == '&')
            pre = pre.substr(1);
        else
            pre = "*" + pre;

        if (!suf.empty())
            return "(" + pre + "+" + suf + ")";
        else
            return pre;
    } else {
        ERROR("Trying to get regular form of address with bad-defined from_.");
    }
}

std::string Address::regularFormOfValue(
    std::optional<std::reference_wrapper<const std::string>> prefix,
    std::optional<std::reference_wrapper<const std::string>> suffix) const {
    string s = regularForm(prefix, suffix);
    if (s.empty())
        ERROR("Empty regular from.");
    if (s[0] == '&')
        return s.substr(1);
    else
        return "*(" + s + ")";
}

bool LiteralExpr::equal(const SymbolicExpr &expr) const {
    const auto liter = dynamic_cast<const LiteralExpr *>(&expr);
    if (!liter)
        return false;

    return type == liter->type && getLiteralValue() == liter->getLiteralValue();
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

bool NullExpr::equal(const SymbolicExpr &expr) const {
    const auto null = dynamic_cast<const NullExpr *>(&expr);
    if (!null)
        return false;

    return true;
}

bool Symbolic::Variable::equal(const SymbolicExpr &expr) const {
    const auto var = dynamic_cast<const Variable *>(&expr);
    if (!var)
        return false;

    if (from_ == nullopt || var->from_ == nullopt)
        return false;
    return **from_ == **var->from_;
}

bool Address::equal(const SymbolicExpr &expr) const {
    const auto addr = dynamic_cast<const Address *>(&expr);
    if (!addr)
        return false;

    bool flag = false;
    // compare base
    if (std::holds_alternative<not_null<std::unique_ptr<Address>>>(from_) &&
        std::holds_alternative<not_null<std::unique_ptr<Address>>>(addr->from_)) {
        auto &from     = *std::get<not_null<std::unique_ptr<Address>>>(from_);
        auto &addrFrom = *std::get<not_null<std::unique_ptr<Address>>>(addr->from_);
        if (from == addrFrom) {
            flag = true;
        }
    } else if (std::holds_alternative<not_null<const clang::VarDecl *>>(from_) &&
               std::holds_alternative<not_null<const clang::VarDecl *>>(addr->from_)) {
        auto &from     = std::get<not_null<const clang::VarDecl *>>(from_);
        auto &addrFrom = std::get<not_null<const clang::VarDecl *>>(addr->from_);
        if (from == addrFrom) {
            flag = true;
        }
    }
    if (!flag)
        return false;

    // compare offset
    if (isOffseted() && addr->isOffseted()) {
        if (*offset_ != *addr->getOffset()) {
            return false;
        }
    } else if (isOffseted() ^ addr->isOffseted()) {
        return false;
    }

    return true;
}

void Symbolic::Variable::collectUsedVars(std::vector<Symbolic::Variable *> &vars) const {
    vars.push_back(const_cast<Symbolic::Variable *>(this));
}

void BinaryOpExpr::collectUsedVars(std::vector<Symbolic::Variable *> &vars) const {
    left_->collectUsedVars(vars);
    right_->collectUsedVars(vars);
}

void UnaryOpExpr::collectUsedVars(std::vector<Symbolic::Variable *> &vars) const {
    expr_->collectUsedVars(vars);
}

// *add*Offset but const func. WithOffset may be better.
std::unique_ptr<Address> Address::addOffset(std::unique_ptr<SymbolicExpr> extra) const {
    auto result = std::make_unique<Address>(*this);
    if (result->offset_ && dynamic_cast<NullExpr *>(result->offset_.get()) == nullptr)
        result->offset_ = std::make_unique<BinaryOpExpr>(
            std::move(result->offset_), BinaryOpExpr::Operator::Add, std::move(extra));
    else
        result->offset_ = std::move(extra);
    return result;
}

std::string Address::getBaseName() const {
    if (const auto varDeclPtr = std::get_if<not_null<const clang::VarDecl *>>(&from_)) {
        if (offset_ && *offset_ != *SymbolicExpr::makeNull())
            ERROR("Address from varDecl should not be offseted.");
        return "&" + (*varDeclPtr)->getNameAsString();
    } else if (const auto addrPtr = std::get_if<not_null<std::unique_ptr<Address>>>(&from_)) {
        auto prefix = (*addrPtr)->getBaseName();
        auto suffix = (offset_ && *offset_ == *makeNull() ? offset_->dump() : "");
        if (prefix.length() == 0)
            ERROR("Empty name.");

        if (prefix[0] == '&')
            prefix = prefix.substr(1);
        else
            prefix = "*" + prefix;

        if (suffix.length())
            return "(" + prefix.substr(1) + "+" + suffix + ")";
        else
            return prefix;
    } else {
        ERROR("Trying to get base name of address with bad-defined from_.");
    }
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
        case SymbolicExpr::ExprType::Literal: os << "Literal"; break;
        case SymbolicExpr::ExprType::Variable: os << "Variable"; break;
        case SymbolicExpr::ExprType::SymbolAddress: os << "SymbolAddress"; break;
        case SymbolicExpr::ExprType::BinaryOp: os << "BinaryOp"; break;
        case SymbolicExpr::ExprType::UnaryOp: os << "UnaryOp"; break;
        case SymbolicExpr::ExprType::SNULL: os << "SNULL"; break;
    }
    return os;
}

namespace Symbolic {
    unique_ptr<SymbolicExpr> createLNotExpr(unique_ptr<SymbolicExpr> expr) {
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
                UNIMPLEMENT("Unsupported non-builtin type: " << QT.getAsString());
            });
    }
} // namespace Symbolic