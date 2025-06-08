#include "expr.h"
#include "ppl.hh"
#include "Analyzer/state.h"
#include "Stingx/LinTS.h"

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

Parma_Polyhedra_Library::Linear_Expression
LiteralExpr::toLinearExpr(const unordered_map<string, int> &) const
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

Parma_Polyhedra_Library::Linear_Expression
BinaryOpExpr::toLinearExpr(const unordered_map<string, int> &varIndexMap) const
{
    auto L = left_->toLinearExpr(varIndexMap);
    auto R = right_->toLinearExpr(varIndexMap);

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

Parma_Polyhedra_Library::Linear_Expression
UnaryOpExpr::toLinearExpr(const unordered_map<string, int> &varIndexMap) const
{
    auto E = expr_->toLinearExpr(varIndexMap);

    switch (op_)
    {
    case Operator::Plus: return E;
    case Operator::Minus: return -E;
    default: break;
    }

    ERROR("non-affine or unsupported op");
}

Parma_Polyhedra_Library::Linear_Expression
Symbolic::Variable::toLinearExpr(const unordered_map<string, int> &varIndexMap) const
{
    Linear_Expression e(0);
    auto it = varIndexMap.find(name_);
    if (it == varIndexMap.end())
    {
        ERROR("Variable '" + name_ + "' not found in varIndexMap.");
    }

    int index = it->second;
    auto var  = Parma_Polyhedra_Library::Variable(index);
    e += var;
    return e;
}
Parma_Polyhedra_Library::Constraint toConstraint(
    const Symbolic::SymbolicExpr *expr, const std::unordered_map<std::string, int> &varIndexMap)
{
    if (!expr || expr->getType() != Symbolic::SymbolicExpr::ExprType::BinaryOp)
    {
        ERROR("toConstraint: expression must be a BinaryOpExpr.");
    }

    const Symbolic::BinaryOpExpr *bin = static_cast<const Symbolic::BinaryOpExpr *>(expr);
    const auto &op                    = bin->getOperator();

    Parma_Polyhedra_Library::Linear_Expression lhs = bin->getLeft()->toLinearExpr(varIndexMap);
    Parma_Polyhedra_Library::Linear_Expression rhs = bin->getRight()->toLinearExpr(varIndexMap);
    Parma_Polyhedra_Library::Linear_Expression le  = lhs - rhs;

    switch (op)
    {
    case Symbolic::BinaryOpExpr::Operator::LessEqual:
        return Parma_Polyhedra_Library::Constraint(le <= 0);
    case Symbolic::BinaryOpExpr::Operator::GreaterEqual:
        return Parma_Polyhedra_Library::Constraint(-le <= 0);
    case Symbolic::BinaryOpExpr::Operator::Equal:
        return Parma_Polyhedra_Library::Constraint(le == 0);
    default: ERROR("toConstraint: unsupported binary operator in assertion (must be <=, >=, ==).");
    }
}

Parma_Polyhedra_Library::C_Polyhedron *
convertAssertionsToPoly(const vector<unique_ptr<Symbolic::SymbolicExpr>> &assertions,
    const unordered_map<string, int> &varIndexMap)
{
    if (varIndexMap.empty())
    {
        ERROR("convertAssertionsToPoly: empty variable map");
    }

    int dimension = static_cast<int>(varIndexMap.size());
    auto *poly =
        new Parma_Polyhedra_Library::C_Polyhedron(dimension, Parma_Polyhedra_Library::UNIVERSE);

    for (const auto &assertion : assertions)
    {
        if (!assertion)
        {
            ERROR("convertAssertionsToPoly: null assertion expression encountered");
        }

        const Symbolic::SymbolicExpr *rawExpr          = assertion.get();
        Parma_Polyhedra_Library::Constraint constraint = toConstraint(rawExpr, varIndexMap);
        poly->add_constraint(constraint);
    }

    return poly;
}

void computeLinearInv(
    const vector<string> &locations, const vector<TransRel> &transitions, const InitRel &initial)
{
    // Step 1: Collect all unique Variable names from transitions and initial
    set<string> varNames;
    unordered_map<string, int> varIndexMap;
    int varCounter = 0;

    auto collectVars = [&](const vector<unique_ptr<SymbolicExpr>> &exprs) {
        for (const auto &expr : exprs)
        {
            vector<Symbolic::Variable *> vars;
            expr->collectUsedVars(vars);
            for (auto *var : vars)
            {
                const string &name = var->getName();
                if (varNames.insert(name).second)
                {
                    varIndexMap[name] = varCounter++;
                }
            }
        }
    };

    for (const auto &[src, dst, exprs] : transitions)
    {
        collectVars(exprs);
    }

    collectVars(initial.second);

    // Step 2: Initialize LinTS and add variables
    auto linTS = std::make_unique<LinTS>();

    for (const auto &name : varNames)
    {
        linTS->addVariable(
            const_cast<char *>(name.c_str())); // Assume external handles const correctly
    }

    // Step 3: Add locations and initial state
    for (size_t i = 0; i < locations.size(); ++i)
    {
        const string &locName = locations[i];
        if (static_cast<int>(i) == initial.first && initial.first != -1)
        {
            C_Polyhedron *initPoly = convertAssertionsToPoly(initial.second, varIndexMap);
            linTS->addLocInit(const_cast<char *>(locName.c_str()), initPoly);
        }
        else
        {
            linTS->addLocInit(const_cast<char *>(locName.c_str()), nullptr);
        }
    }

    // Step 4: Add transition relations
    for (size_t i = 0; i < transitions.size(); ++i)
    {
        const auto &[src, dst, exprs] = transitions[i];
        string transName              = "t" + std::to_string(i);
        C_Polyhedron *transPoly       = convertAssertionsToPoly(exprs, varIndexMap);
        linTS->addTransRel(const_cast<char *>(transName.c_str()),
            const_cast<char *>(locations[src].c_str()), const_cast<char *>(locations[dst].c_str()),
            transPoly);
    }

    // Step 5: Run invariant computation
    linTS->ComputeLinTSInv();
}
