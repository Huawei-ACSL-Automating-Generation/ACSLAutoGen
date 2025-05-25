#include "expr.h"
using namespace std;

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