#include "symbolic.h"
#include "macros.h"
#include <memory>
#include <sstream>
#include <cstring>

using namespace std;
std::unique_ptr<SymbolicExpr> LiteralExpr::clone() const
{
    switch (getLiteralType())
    {
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

unique_ptr<SymbolicExpr> BinaryOpExpr::clone() const
{
    return make_unique<BinaryOpExpr>(left_->clone(), op_, right_->clone());
}

unique_ptr<SymbolicExpr> UnaryOpExpr::clone() const
{
    return make_unique<UnaryOpExpr>(op_, expr_->clone());
}

unique_ptr<SymbolicExpr> ArrayExpr::clone() const
{
    return make_unique<ArrayExpr>(array_->clone(), index_->clone());
}

unique_ptr<SymbolicExpr> NullExpr::clone() const { return make_unique<NullExpr>(); }

unique_ptr<SymbolicExpr> Variable::clone() const { return make_unique<Variable>(name_, varType_); }

unique_ptr<SymbolicExpr> Address::clone() const { return make_unique<Address>(id_); }

std::string LiteralExpr::dump() const
{
    std::ostringstream oss;
    switch (getLiteralType())
    {
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

std::string BinaryOpExpr::dump() const
{
    std::ostringstream oss;
    std::string opStr;
    switch (op_)
    {
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

std::string UnaryOpExpr::dump() const
{
    std::ostringstream oss;
    std::string opStr;
    switch (op_)
    {
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

std::string ArrayExpr::dump() const
{
    std::ostringstream oss;
    oss << array_->dump() << "[" << index_->dump() << "]";
    return oss.str();
}

std::string NullExpr::dump() const { return "null"; }

std::string Variable::dump() const
{
    std::ostringstream oss;
    const auto &t = getExprType();

    oss << "Var(" << name_ << ", ";

    switch (t.kind)
    {
    case ScalarKind::Int: oss << "int"; break;
    case ScalarKind::UInt: oss << "uint"; break;
    case ScalarKind::Bool: oss << "bool"; break;
    }

    oss << t.bitWidth << ")";
    return oss.str();
}

std::string Address::dump() const
{
    std::ostringstream oss;
    oss << "Address(" << id_ << ")" << (getOffset() ? " (Offset)" : "");
    return oss.str();
}

bool LiteralExpr::equal(const SymbolicExpr &expr) const
{
    const auto liter = dynamic_cast<const LiteralExpr *>(&expr);
    if (!liter)
        return false;

    return type == liter->type && (std::memcmp(&data, &(liter->data), sizeof(Data)) == 0);
}

bool BinaryOpExpr::equal(const SymbolicExpr &expr) const
{
    const auto binary = dynamic_cast<const BinaryOpExpr *>(&expr);
    if (!binary)
        return false;

    return *left_ == *(binary->left_) && op_ == binary->op_ && *right_ == *(binary->right_);
}

bool UnaryOpExpr::equal(const SymbolicExpr &expr) const
{
    const auto unary = dynamic_cast<const UnaryOpExpr *>(&expr);
    if (!unary)
        return false;

    return op_ == unary->op_ && *expr_ == *(unary->expr_);
}

bool ArrayExpr::equal(const SymbolicExpr &expr) const
{
    const auto arr = dynamic_cast<const ArrayExpr *>(&expr);
    if (!arr)
        return false;

    return *array_ == *(arr->array_) && *index_ == *(arr->index_);
}

bool NullExpr::equal(const SymbolicExpr &expr) const
{
    const auto null = dynamic_cast<const NullExpr *>(&expr);
    if (!null)
        return false;

    return true;
}

bool Variable::equal(const SymbolicExpr &expr) const
{
    const auto var = dynamic_cast<const Variable *>(&expr);
    if (!var)
        return false;

    return name_ == var->name_ && varType_ == var->varType_;
}

bool Address::equal(const SymbolicExpr &expr) const
{
    const auto addr = dynamic_cast<const Address *>(&expr);
    if (!addr)
        return false;

    return id_ == addr->id_ && offset_ == addr->offset_;
}

namespace std
{
    template <> struct hash<Address>
    {
        std::size_t operator()(const Address &addr) const
        {
            return std::hash<unsigned int>()(addr.getId());
        }
    };
} // namespace std
