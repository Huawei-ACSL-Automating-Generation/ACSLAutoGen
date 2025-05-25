#include "expr.h"
#include "ppl.hh"

using namespace std;
using namespace Symbolic;
bool UnaryOpExpr::isLinear() const
{
    switch (op_)
    {
    case Operator::Plus:
    case Operator::Minus: return expr_->isLinear();
    default: return false;
    }
}

int UnaryOpExpr::getMaxDegree() const
{
    switch (op_)
    {
    case Operator::Plus:
    case Operator::Minus: return expr_->getMaxDegree();
    default: return -1; // undefined / invalid
    }
}

bool BinaryOpExpr::isLinear() const
{
    // Only allow Add, Subtract, and Multiply with constant
    if (op_ == Operator::Add || op_ == Operator::Subtract)
    {
        return left_->isLinear() && right_->isLinear();
    }
    if (op_ == Operator::Multiply)
    {
        // Check one side is constant (degree 0), and the other is linear
        int ldeg = left_->getMaxDegree();
        int rdeg = right_->getMaxDegree();
        return (ldeg == 0 && right_->isLinear()) || (rdeg == 0 && left_->isLinear());
    }
    return false; // All other ops are non-linear
}

int BinaryOpExpr::getMaxDegree() const
{
    int ldeg = left_->getMaxDegree();
    int rdeg = right_->getMaxDegree();

    switch (op_)
    {
    case Operator::Add:
    case Operator::Subtract: return std::max(ldeg, rdeg);
    case Operator::Multiply: return ldeg + rdeg;
    default: return -1; // invalid in linear context
    }
}

Linear_Expression LiteralExpr::toLinearExpr() const
{
    switch (type)
    {
    case LiteralType::Boolean: return Linear_Expression(data.boolValue ? 1 : 0);
    case LiteralType::Int: return Linear_Expression(data.intValue);
    case LiteralType::UnsignedInt: return Linear_Expression(static_cast<int>(data.uintValue));
    case LiteralType::Short: return Linear_Expression(static_cast<int>(data.shortValue));
    case LiteralType::UnsignedShort: return Linear_Expression(static_cast<int>(data.ushortValue));
    case LiteralType::Int64: return Linear_Expression(static_cast<Coefficient>(data.int64Value));
    case LiteralType::UInt64: return Linear_Expression(static_cast<Coefficient>(data.uint64Value));
    default: throw std::runtime_error("Unsupported LiteralExpr type in toLinearExpr");
    }
}