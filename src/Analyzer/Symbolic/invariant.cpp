#include "expr.h"
#include "ppl.hh"
#include "Analyzer/state.h"
#include "Stingx/LinTS.h"

using namespace std;
using namespace Symbolic;

bool UnaryOpExpr::isLinear() const {
    switch (op_) {
        case Operator::Plus:
        case Operator::Minus: return expr_->isLinear();
        default: return false;
    }
}

int UnaryOpExpr::getMaxDegree() const {
    switch (op_) {
        case Operator::Plus:
        case Operator::Minus: return expr_->getMaxDegree();
        default: return -1; // undefined / invalid
    }
}

bool BinaryOpExpr::isLinear() const {
    // Only allow Add, Subtract, and Multiply with constant
    if (op_ == Operator::Add || op_ == Operator::Subtract) {
        return left_->isLinear() && right_->isLinear();
    }
    if (op_ == Operator::Multiply) {
        // Check one side is constant (degree 0), and the other is linear
        int ldeg = left_->getMaxDegree();
        int rdeg = right_->getMaxDegree();
        return (ldeg == 0 && right_->isLinear()) || (rdeg == 0 && left_->isLinear());
    }
    return false; // All other ops are non-linear
}

int BinaryOpExpr::getMaxDegree() const {
    int ldeg = left_->getMaxDegree();
    int rdeg = right_->getMaxDegree();

    switch (op_) {
        case Operator::Add:
        case Operator::Subtract: return max(ldeg, rdeg);
        case Operator::Multiply: return ldeg + rdeg;
        default: return -1; // invalid in linear context
    }
}
Parma_Polyhedra_Library::Linear_Expression LiteralExpr::toLinearExpr(const VarManager &) const {
    using namespace Parma_Polyhedra_Library;
    switch (type) {
        case LiteralType::Boolean: return Linear_Expression(data.boolValue ? 1 : 0);
        case LiteralType::Int: return Linear_Expression(data.intValue);
        case LiteralType::UnsignedInt: return Linear_Expression(static_cast<int>(data.uintValue));
        case LiteralType::Short: return Linear_Expression(static_cast<int>(data.shortValue));
        case LiteralType::UnsignedShort:
            return Linear_Expression(static_cast<int>(data.ushortValue));
        case LiteralType::Int64:
            return Linear_Expression(static_cast<Coefficient>(data.int64Value));
        case LiteralType::UInt64:
            return Linear_Expression(static_cast<Coefficient>(data.uint64Value));
        default: throw std::runtime_error("Unsupported LiteralExpr type in toLinearExpr");
    }
}

Parma_Polyhedra_Library::Linear_Expression BinaryOpExpr::toLinearExpr(const VarManager &vm) const {
    auto L = left_->toLinearExpr(vm);
    auto R = right_->toLinearExpr(vm);

    switch (op_) {
        case Operator::Add: return L + R;
        case Operator::Subtract: return L - R;
        case Operator::Multiply:
            if (right_->getMaxDegree() == 0)
                return L * R.inhomogeneous_term();
            if (left_->getMaxDegree() == 0)
                return R * L.inhomogeneous_term();
            break;
        case Operator::Divide:
            if (right_->getMaxDegree() == 0) {
                auto denom = R.inhomogeneous_term();
                if (denom != 0) {
                    Parma_Polyhedra_Library::Linear_Expression result(0);
                    int maxDim = L.space_dimension();
                    for (int i = 0; i < maxDim; ++i) {
                        auto coeff = L.coefficient(Parma_Polyhedra_Library::Variable(i));
                        if (coeff != 0)
                            result += (coeff / denom) * Parma_Polyhedra_Library::Variable(i);
                    }
                    result += L.inhomogeneous_term() / denom;
                    return result;
                }
            }
            break;
        default: break;
    }

    ERROR("BinaryOpExpr: non-affine or unsupported operator");
}

Parma_Polyhedra_Library::Linear_Expression UnaryOpExpr::toLinearExpr(const VarManager &vm) const {
    auto E = expr_->toLinearExpr(vm);

    switch (op_) {
        case Operator::Plus: return E;
        case Operator::Minus: return -E;
        default: ERROR("UnaryOpExpr: non-affine or unsupported operator");
    }
}

Parma_Polyhedra_Library::Linear_Expression Symbolic::Variable::toLinearExpr(
    const VarManager &vm) const {
    using namespace Parma_Polyhedra_Library;
    Linear_Expression e(0);
    int index = vm.getIndex(*this);
    Parma_Polyhedra_Library::Variable v(index);
    e += v;
    return e;
}

Parma_Polyhedra_Library::Linear_Expression LiteralExpr::toLinearExpr() const {
    switch (type) {
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

Parma_Polyhedra_Library::Linear_Expression BinaryOpExpr::toLinearExpr() const {
    auto L = left_->toLinearExpr();
    auto R = right_->toLinearExpr();

    switch (op_) {
        case Operator::Add: return L + R;
        case Operator::Subtract: return L - R;
        case Operator::Multiply:
            if (right_->getMaxDegree() == 0)
                return L * R.inhomogeneous_term();
            if (left_->getMaxDegree() == 0)
                return R * L.inhomogeneous_term();
            break;
        case Operator::Divide:
            if (right_->getMaxDegree() == 0) {
                auto denom = R.inhomogeneous_term();
                if (denom != 0) {
                    Parma_Polyhedra_Library::Linear_Expression result(0);
                    int maxDim = L.space_dimension(); // total variable dimensions
                    for (int i = 0; i < maxDim; ++i) {
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

Parma_Polyhedra_Library::Linear_Expression UnaryOpExpr::toLinearExpr() const {
    auto E = expr_->toLinearExpr();

    switch (op_) {
        case Operator::Plus: return E;
        case Operator::Minus: return -E;
        default: break;
    }

    ERROR("non-affine or unsupported op");
}

Parma_Polyhedra_Library::Linear_Expression Symbolic::Variable::toLinearExpr() const {
    Parma_Polyhedra_Library::Linear_Expression e(0);
    auto var = Parma_Polyhedra_Library::Variable(id_);
    e += var;
    return e;
}

Parma_Polyhedra_Library::Constraint primedConstriant(const Parma_Polyhedra_Library::Constraint &c,
                                                     const VarManager &vm) {
    Linear_Expression shiftedExpr(0);
    int spaceDim = c.space_dimension();

    for (int i = 0; i < spaceDim; ++i) {
        Parma_Polyhedra_Library::Variable oldVar(i);
        Coefficient coeff = c.coefficient(oldVar);
        if (coeff != 0) {
            Parma_Polyhedra_Library::Variable newVar(i + vm.numVars);
            shiftedExpr += coeff * newVar;
        }
    }

    shiftedExpr += c.inhomogeneous_term();

    switch (c.type()) {
        case Constraint::Type::EQUALITY: return Constraint(shiftedExpr == 0);
        case Constraint::Type::NONSTRICT_INEQUALITY: return Constraint(shiftedExpr <= 0);
        case Constraint::Type::STRICT_INEQUALITY: ERROR("Strict inequality not supported");
        default: ERROR("Unsupported constraint type");
    }
}

Parma_Polyhedra_Library::Constraint toConstraint(const Symbolic::SymbolicExpr *expr,
                                                 const VarManager &vm) {
    if (!expr || expr->getType() != Symbolic::SymbolicExpr::ExprType::BinaryOp) {
        ERROR("toConstraint: expression must be a BinaryOpExpr.");
    }

    const Symbolic::BinaryOpExpr *bin = static_cast<const Symbolic::BinaryOpExpr *>(expr);
    const auto &op                    = bin->getOperator();

    Parma_Polyhedra_Library::Linear_Expression lhs = bin->getLeft()->toLinearExpr(vm);
    Parma_Polyhedra_Library::Linear_Expression rhs = bin->getRight()->toLinearExpr(vm);
    Parma_Polyhedra_Library::Linear_Expression le  = lhs - rhs;

    switch (op) {
        case Symbolic::BinaryOpExpr::Operator::LessEqual:
            return Parma_Polyhedra_Library::Constraint(le <= 0);
        case Symbolic::BinaryOpExpr::Operator::GreaterEqual:
            return Parma_Polyhedra_Library::Constraint(-le <= 0);
        case Symbolic::BinaryOpExpr::Operator::Equal:
            return Parma_Polyhedra_Library::Constraint(le == 0);
        default:
            ERROR("toConstraint: unsupported binary operator in assertion (must be <=, >=, ==).");
    }
}

Parma_Polyhedra_Library::C_Polyhedron *convertFormulaToPoly(const Formulas &assertions,
                                                            const VarManager &vm) {
    if (vm.numVars == 0) {
        ERROR("convertFormulaToPoly: empty variable map");
    }

    int dimension = static_cast<int>(vm.numVars);
    auto *poly =
        new Parma_Polyhedra_Library::C_Polyhedron(dimension, Parma_Polyhedra_Library::UNIVERSE);

    for (const auto &assertion : assertions) {
        if (!assertion) {
            ERROR("convertFormulaToPoly: null assertion expression encountered");
        }

        const Symbolic::SymbolicExpr *rawExpr          = assertion.get();
        Parma_Polyhedra_Library::Constraint constraint = toConstraint(rawExpr, vm);
        poly->add_constraint(constraint);
    }

    return poly;
}

// @WindOctober: assume all int operation here.
Formulas convertPolyToFormula(const Parma_Polyhedra_Library::C_Polyhedron &poly,
                              const VarManager &vm) {
    Formulas result;

    for (const Parma_Polyhedra_Library::Constraint &c : poly.constraints()) {
        std::unique_ptr<Symbolic::SymbolicExpr> lhs = std::make_unique<Symbolic::LiteralExpr>(0);

        for (int i = 0; i < vm.numVars; ++i) {
            Parma_Polyhedra_Library::Variable pplVar(i);
            Parma_Polyhedra_Library::Coefficient coeff = c.coefficient(pplVar);

            if (coeff != 0) {
                const std::string &name = vm.orderedVars[i];

                auto varExpr = std::make_unique<Symbolic::Variable>(
                    name, Symbolic::SymbolicExpr::Type{Symbolic::SymbolicExpr::ScalarKind::Int, 32},
                    i, nullptr);

                std::unique_ptr<Symbolic::SymbolicExpr> term;
                if (coeff == 1) {
                    term = std::move(varExpr);
                } else {
                    // @WindOctober: Overflow for get_si().
                    auto lit =
                        std::make_unique<Symbolic::LiteralExpr>(static_cast<int>(coeff.get_si()));
                    term = std::make_unique<Symbolic::BinaryOpExpr>(
                        std::move(lit), Symbolic::BinaryOpExpr::Operator::Multiply,
                        std::move(varExpr));
                }

                lhs = std::make_unique<Symbolic::BinaryOpExpr>(
                    std::move(lhs), Symbolic::BinaryOpExpr::Operator::Add, std::move(term));
            }
        }

        // @WindOctober: same as above.
        auto rhs = std::make_unique<Symbolic::LiteralExpr>(
            static_cast<int>(-c.inhomogeneous_term().get_si()));

        Symbolic::BinaryOpExpr::Operator op;

        switch (c.type()) {
            case Parma_Polyhedra_Library::Constraint::Type::EQUALITY:
                op = Symbolic::BinaryOpExpr::Operator::Equal;
                break;
            case Parma_Polyhedra_Library::Constraint::Type::NONSTRICT_INEQUALITY:
                op = Symbolic::BinaryOpExpr::Operator::LessEqual;
                break;
            case Parma_Polyhedra_Library::Constraint::Type::STRICT_INEQUALITY:
                ERROR("Strict inequalities not supported in symbolic conversion");
            default: ERROR("Unsupported constraint type in convertPolyToFormula");
        }

        result.push_back(
            std::make_unique<Symbolic::BinaryOpExpr>(std::move(lhs), op, std::move(rhs)));
    }

    return result;
}

void computeLinearInv(const vector<string> &locations,
                      const vector<TransRel> &transitions,
                      const InitRel &initial,
                      const VarManager &vm) {
    auto linTS = make_unique<LinTS>();
    for (const std::string &name : vm.orderedVars) {
        linTS->addVariable(const_cast<char *>(name.c_str()));
    }

    for (size_t i = 0; i < locations.size(); ++i) {
        const string &locName = locations[i];
        if (static_cast<int>(i) == initial.first && initial.first != -1) {
            C_Polyhedron *initPoly = convertFormulaToPoly(initial.second, vm);
            linTS->addLocInit(const_cast<char *>(locName.c_str()), initPoly);
        } else {
            linTS->addLocInit(const_cast<char *>(locName.c_str()), nullptr);
        }
    }

    for (size_t i = 0; i < transitions.size(); ++i) {
        const auto &[src, dst, exprs] = transitions[i];
        string transName              = "t" + to_string(i);
        C_Polyhedron *transPoly       = convertFormulaToPoly(exprs, vm);
        linTS->addTransRel(const_cast<char *>(transName.c_str()),
                           const_cast<char *>(locations[src].c_str()),
                           const_cast<char *>(locations[dst].c_str()), transPoly);
    }

    linTS->ComputeLinTSInv();
}

/******************************************************************************\
 *                              Path Preprocessing                            *
 *  This section handles symbolic path condition construction and transition  *
 *  constraint setup for downstream invariant analysis.                       *
\******************************************************************************/

// TODO: optimize to one cond, condition won't get multi cases.
Formulas buildLoopInvariant(const Formulas &loopCond, const vector<unique_ptr<Path>> &paths) {
    Formulas invariants;
    VarManager vm = VarManager::fromPaths(paths);

    TODO();
    return invariants;
}