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

Parma_Polyhedra_Library::Linear_Expression LiteralExpr::toLinearExpr() const
{
    switch (type)
    {
    case LiteralType::Boolean:
        return Parma_Polyhedra_Library::Linear_Expression(data.boolValue ? 1 : 0);
    case LiteralType::Int: return Parma_Polyhedra_Library::Linear_Expression(data.intValue);
    case LiteralType::UnsignedInt:
        return Parma_Polyhedra_Library::Linear_Expression(static_cast<int>(data.uintValue));
    case LiteralType::Short:
        return Parma_Polyhedra_Library::Linear_Expression(static_cast<int>(data.shortValue));
    case LiteralType::UnsignedShort:
        return Parma_Polyhedra_Library::Linear_Expression(static_cast<int>(data.ushortValue));
    case LiteralType::Int64:
        return Parma_Polyhedra_Library::Linear_Expression(
            static_cast<Parma_Polyhedra_Library::Coefficient>(data.int64Value));
    case LiteralType::UInt64:
        return Parma_Polyhedra_Library::Linear_Expression(
            static_cast<Parma_Polyhedra_Library::Coefficient>(data.uint64Value));
    default: throw std::runtime_error("Unsupported LiteralExpr type in toLinearExpr");
    }
}

Parma_Polyhedra_Library::Linear_Expression BinaryOpExpr::toLinearExpr() const
{
    auto L = left_->toLinearExpr();
    auto R = right_->toLinearExpr();

    switch (op_)
    {
    case Operator::Add: return L + R;
    case Operator::Subtract: return L - R;
    case Operator::Multiply:
        if (right_->getMaxDegree() == 0)
            return L * R.inhomogeneous_term();
        if (left_->getMaxDegree() == 0)
            return R * L.inhomogeneous_term();
        break;
    case Operator::Divide:
        if (right_->getMaxDegree() == 0)
        {
            auto denom = R.inhomogeneous_term();
            if (denom != 0)
            {
                Parma_Polyhedra_Library::Linear_Expression result(0);
                int maxDim = L.space_dimension(); // total variable dimensions
                for (int i = 0; i < maxDim; ++i)
                {
                    Parma_Polyhedra_Library::Variable v(i);
                    auto coeff = L.coefficient(v);
                    if (coeff != 0)
                        result += (coeff / denom) * v;
                }
                result += L.inhomogeneous_term() / denom;
                return result;
            }
        }
        break;
    default: break;
    }
    ERROR("non-affine or unsupported op");
}

Parma_Polyhedra_Library::Linear_Expression UnaryOpExpr::toLinearExpr() const
{
    auto E = expr_->toLinearExpr();

    switch (op_)
    {
    case Operator::Plus: return E;
    case Operator::Minus: return -E;
    default: break;
    }

    ERROR("non-affine or unsupported op");
}

Parma_Polyhedra_Library::Linear_Expression Symbolic::Variable::toLinearExpr() const
{
    Linear_Expression e(0);
    e.set_coefficient(Parma_Polyhedra_Library::Variable(id_), 1);
    return e;
}