#include "symbolic.h"
#include <memory>
#include <sstream>

using namespace std;
unique_ptr<SymbolicExpr> LiteralExpr::clone() const
{
    switch (getLiteralType())
    {
    case LiteralType::Boolean: return make_unique<LiteralExpr>(data.boolValue);
    case LiteralType::Int: return make_unique<LiteralExpr>(data.intValue);
    case LiteralType::UnsignedInt: return make_unique<LiteralExpr>(data.uintValue);
    case LiteralType::Short: return make_unique<LiteralExpr>(data.shortValue);
    case LiteralType::UnsignedShort: return make_unique<LiteralExpr>(data.ushortValue);
    }
    return nullptr;
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
    switch (type)
    {
    case LiteralType::Boolean: oss << (data.boolValue ? "true" : "false"); break;
    case LiteralType::Int: oss << data.intValue; break;
    case LiteralType::UnsignedInt: oss << data.uintValue; break;
    case LiteralType::Short: oss << data.shortValue; break;
    case LiteralType::UnsignedShort: oss << data.ushortValue; break;
    default: oss << "unknown literal"; break;
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
    oss << "Var(" << name_ << ", ";
    switch (varType_)
    {
    case VarType::Int: oss << "int"; break;
    case VarType::UInt: oss << "unsigned int"; break;
    case VarType::Bool: oss << "bool"; break;
    default: oss << "unknown"; break;
    }
    oss << ")";
    return oss.str();
}

std::string Address::dump() const
{
    std::ostringstream oss;
    oss << "Address(" << id_ << ")" << getOffset() ? " (Offset)" : "";
    return oss.str();
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
