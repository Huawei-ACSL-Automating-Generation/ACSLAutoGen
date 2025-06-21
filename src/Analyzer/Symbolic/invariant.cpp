#include "expr.h"
#include "ppl.hh"
#include "Analyzer/state.h"
#include "Stingx/LinTS.h"
#include <queue>

using namespace std;
using namespace Symbolic;

void dump(const Parma_Polyhedra_Library::C_Polyhedron &poly, const VarManager &vm);
void dump(const Parma_Polyhedra_Library::Linear_Expression &expr, const VarManager &vm);

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
Parma_Polyhedra_Library::Linear_Expression LiteralExpr::toLinearExpr(
    const std::unordered_map<std::string, int> &) const {
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

Parma_Polyhedra_Library::Linear_Expression BinaryOpExpr::toLinearExpr(
    const std::unordered_map<std::string, int> &varIndexMap) const {
    auto L = left_->toLinearExpr(varIndexMap);
    auto R = right_->toLinearExpr(varIndexMap);

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

Parma_Polyhedra_Library::Linear_Expression UnaryOpExpr::toLinearExpr(
    const std::unordered_map<std::string, int> &varIndexMap) const {
    auto E = expr_->toLinearExpr(varIndexMap);

    switch (op_) {
        case Operator::Plus: return E;
        case Operator::Minus: return -E;
        default: ERROR("UnaryOpExpr: non-affine or unsupported operator");
    }
}

Parma_Polyhedra_Library::Linear_Expression Symbolic::Variable::toLinearExpr(
    const std::unordered_map<std::string, int> &varIndexMap) const {
    using namespace Parma_Polyhedra_Library;
    Linear_Expression e(0);
    auto it = varIndexMap.find(getName());
    if (it == varIndexMap.end()) {
        ERROR("Variable '" + getName() + "' not found in index map.");
    }
    e += Parma_Polyhedra_Library::Variable(it->second);
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
        case Constraint::Type::NONSTRICT_INEQUALITY: return Constraint(shiftedExpr >= 0);
        case Constraint::Type::STRICT_INEQUALITY: ERROR("Strict inequality not supported");
        default: ERROR("Unsupported constraint type");
    }
}

Parma_Polyhedra_Library::C_Polyhedron *primedPolyhedron(
    const Parma_Polyhedra_Library::C_Polyhedron &poly,
    const VarManager &vm) {
    using namespace Parma_Polyhedra_Library;

    auto *primed = new C_Polyhedron(vm.numVars * 2, UNIVERSE);

    Constraint_System cs = poly.constraints();
    for (Constraint_System::const_iterator it = cs.begin(); it != cs.end(); ++it) {
        Constraint primedC = primedConstriant(*it, vm);
        primed->add_constraint(primedC);
    }

    return primed;
}

Parma_Polyhedra_Library::Constraint toConstraint(const Symbolic::SymbolicExpr *expr,
                                                 const VarManager &vm) {
    if (!expr || expr->getType() != Symbolic::SymbolicExpr::ExprType::BinaryOp) {
        ERROR("toConstraint: expression must be a BinaryOpExpr.");
    }

    const Symbolic::BinaryOpExpr *bin = static_cast<const Symbolic::BinaryOpExpr *>(expr);
    const auto &op                    = bin->getOperator();

    Parma_Polyhedra_Library::Linear_Expression lhs = bin->getLeft()->toLinearExpr(vm.varIndexMap);
    Parma_Polyhedra_Library::Linear_Expression rhs = bin->getRight()->toLinearExpr(vm.varIndexMap);
    Parma_Polyhedra_Library::Linear_Expression le  = lhs - rhs;

    switch (op) {
        case Symbolic::BinaryOpExpr::Operator::LessEqual:
            return Parma_Polyhedra_Library::Constraint(le <= 0);
        case Symbolic::BinaryOpExpr::Operator::GreaterEqual:
            return Parma_Polyhedra_Library::Constraint(le >= 0);
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

    int dimension = vm.numVars * 2;
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

std::vector<Parma_Polyhedra_Library::C_Polyhedron> computeLinearInv(
    const vector<string> &locations,
    const vector<TransRel> &transitions,
    const InitRel &initial,
    const VarManager &vm) {
    auto linTS = std::make_unique<LinTS>();

    for (const std::string &name : vm.orderedVars) {
        linTS->addVariable(const_cast<char *>(name.c_str()));
    }

    for (size_t i = 0; i < locations.size(); ++i) {
        const std::string &locName = locations[i];
        if (static_cast<int>(i) == initial.first) {
            linTS->addLocInit(const_cast<char *>(locName.c_str()), initial.second);
        } else {
            linTS->addLocInit(const_cast<char *>(locName.c_str()), nullptr);
        }
    }

    for (size_t i = 0; i < transitions.size(); ++i) {
        const auto &[src, dst, transPoly] = transitions[i];
        std::string transName             = "t" + std::to_string(i);

        linTS->addTransRel(const_cast<char *>(transName.c_str()),
                           const_cast<char *>(locations[src].c_str()),
                           const_cast<char *>(locations[dst].c_str()), transPoly);
    }

    linTS->ComputeLinTSInv();
    auto invariants = linTS->getInvMap();

    std::vector<Parma_Polyhedra_Library::C_Polyhedron> result;
    const auto &exitInvariants = invariants["exit"];

    for (const auto *p : exitInvariants) {
        result.emplace_back(*p);
    }
    return result;
}

Formulas cloneFormulas(const Formulas &input) {
    Formulas result;
    result.reserve(input.size());
    for (const auto &expr : input) {
        result.push_back(expr->clone());
    }
    return result;
}

/******************************************************************************\
 *                              Path Preprocessing                            *
 *  This section handles symbolic path condition construction and transition  *
 *  constraint setup for downstream invariant analysis.                       *
 *                                                                            *
 *  This is designed to facilitate integration with StInGx by providing the   *
 *  necessary InitRel and TransRel relations for its analysis engine.         *
\******************************************************************************/

Parma_Polyhedra_Library::C_Polyhedron buildIdentityPoly(const VarManager &vm) {
    using namespace Parma_Polyhedra_Library;

    int dim = vm.numVars * 2;
    C_Polyhedron poly(dim, UNIVERSE);

    for (int i = 0; i < vm.numVars; ++i) {
        Parma_Polyhedra_Library::Variable unprimed(i);
        Parma_Polyhedra_Library::Variable primed(i + vm.numVars);
        Linear_Expression eq = primed - unprimed;
        poly.add_constraint(Constraint(eq == 0));
    }

    return poly;
}

std::vector<Formulas> negateFormulas(Formulas input) {
    using Op = Symbolic::BinaryOpExpr::Operator;

    std::vector<Formulas> result;
    std::queue<std::pair<Formulas, size_t>> worklist;
    worklist.push({std::move(input), 0});

    while (!worklist.empty()) {
        auto [current, startIdx] = std::move(worklist.front());
        worklist.pop();

        bool expanded = false;

        for (size_t i = startIdx; i < current.size(); ++i) {
            auto *bin = dynamic_cast<Symbolic::BinaryOpExpr *>(current[i].get());
            if (!bin) {
                ERROR("negateFormulas: input[" + std::to_string(i) + "] is not a BinaryOpExpr");
            }

            const auto &lhs = bin->getLeft();
            const auto &rhs = bin->getRight();

            // @WindOctober: try to optimize clone.
            switch (bin->getOperator()) {
                case Op::LessEqual: {
                    auto newRHS = std::make_unique<Symbolic::BinaryOpExpr>(
                        rhs->clone(), Op::Add, std::make_unique<Symbolic::LiteralExpr>(1));
                    current[i] = std::make_unique<Symbolic::BinaryOpExpr>(
                        lhs->clone(), Op::GreaterEqual, std::move(newRHS));
                    worklist.push({std::move(current), i + 1});
                    expanded = true;
                    break;
                }
                case Op::GreaterEqual: {
                    auto newRHS = std::make_unique<Symbolic::BinaryOpExpr>(
                        rhs->clone(), Op::Subtract, std::make_unique<Symbolic::LiteralExpr>(1));
                    current[i] = std::make_unique<Symbolic::BinaryOpExpr>(
                        lhs->clone(), Op::LessEqual, std::move(newRHS));
                    worklist.push({std::move(current), i + 1});
                    expanded = true;
                    break;
                }
                case Op::Equal: {
                    auto leExpr = std::make_unique<Symbolic::BinaryOpExpr>(
                        lhs->clone(), Op::LessEqual,
                        std::make_unique<Symbolic::BinaryOpExpr>(
                            rhs->clone(), Op::Subtract,
                            std::make_unique<Symbolic::LiteralExpr>(1)));

                    auto geExpr = std::make_unique<Symbolic::BinaryOpExpr>(
                        lhs->clone(), Op::GreaterEqual,
                        std::make_unique<Symbolic::BinaryOpExpr>(
                            rhs->clone(), Op::Add, std::make_unique<Symbolic::LiteralExpr>(1)));

                    Formulas branch;
                    branch.reserve(current.size());
                    for (size_t j = 0; j < current.size(); ++j) {
                        if (j == i)
                            branch.push_back(std::move(leExpr));
                        else
                            branch.push_back(current[j]->clone());
                    }

                    current[i] = std::move(geExpr);

                    worklist.push({std::move(current), i + 1});
                    worklist.push({std::move(branch), i + 1});
                    expanded = true;
                    break;
                }
                default: UNREACHABLE();
            }

            if (expanded)
                break;
        }

        if (!expanded)
            result.push_back(std::move(current));
    }

    return result;
}

std::vector<Formulas> preprocessLoopCond(Formulas loopCond) {
    std::vector<Formulas> result;
    std::queue<std::pair<Formulas, size_t>> worklist;
    worklist.push({std::move(loopCond), 0});

    while (!worklist.empty()) {
        auto [current, startIdx] = std::move(worklist.front());
        worklist.pop();

        bool expanded = false;

        for (size_t i = startIdx; i < current.size(); ++i) {
            auto *bin = dynamic_cast<Symbolic::BinaryOpExpr *>(current[i].get());
            if (!bin) {
                ERROR("preprocessLoopCond: loopCond[" + std::to_string(i) +
                      "] is not a BinaryOpExpr");
            }

            using enum Symbolic::BinaryOpExpr::Operator;
            const auto &lhs = bin->getLeft();
            const auto &rhs = bin->getRight();

            switch (bin->getOperator()) {
                case NotEqual: {
                    auto rhsPlus1 = std::make_unique<Symbolic::BinaryOpExpr>(
                        rhs->clone(), Add, std::make_unique<Symbolic::LiteralExpr>(1));
                    auto geExpr = std::make_unique<Symbolic::BinaryOpExpr>(
                        lhs->clone(), GreaterEqual, std::move(rhsPlus1));

                    auto rhsMinus1 = std::make_unique<Symbolic::BinaryOpExpr>(
                        rhs->clone(), Subtract, std::make_unique<Symbolic::LiteralExpr>(1));
                    auto leExpr = std::make_unique<Symbolic::BinaryOpExpr>(lhs->clone(), LessEqual,
                                                                           std::move(rhsMinus1));

                    Formulas branch;
                    branch.reserve(current.size());
                    for (size_t j = 0; j < current.size(); ++j) {
                        if (j == i)
                            branch.push_back(std::move(leExpr));
                        else
                            branch.push_back(current[j]->clone());
                    }

                    current[i] = std::move(geExpr);

                    worklist.push({std::move(current), i + 1});
                    worklist.push({std::move(branch), i + 1});
                    expanded = true;
                    break;
                }
                case GreaterThan: {
                    auto newRHS = std::make_unique<Symbolic::BinaryOpExpr>(
                        rhs->clone(), Add, std::make_unique<Symbolic::LiteralExpr>(1));
                    current[i] = std::make_unique<Symbolic::BinaryOpExpr>(
                        lhs->clone(), GreaterEqual, std::move(newRHS));
                    worklist.push({std::move(current), i + 1});
                    expanded = true;
                    break;
                }
                case LessThan: {
                    auto newRHS = std::make_unique<Symbolic::BinaryOpExpr>(
                        rhs->clone(), Subtract, std::make_unique<Symbolic::LiteralExpr>(1));
                    current[i] = std::make_unique<Symbolic::BinaryOpExpr>(lhs->clone(), LessEqual,
                                                                          std::move(newRHS));
                    worklist.push({std::move(current), i + 1});
                    expanded = true;
                    break;
                }
                default: break;
            }

            if (expanded)
                break;
        }

        if (!expanded)
            result.push_back(std::move(current));
    }

    return result;
}

Parma_Polyhedra_Library::C_Polyhedron *buildPathPoly(const Path &path,
                                                     const VarManager &vm,
                                                     bool init = false) {
    using namespace Parma_Polyhedra_Library;

    int dim = init ? vm.numVars : vm.numVars * 2;

    auto *result = new C_Polyhedron(dim, UNIVERSE);

    const auto &varAddrMap = path.getVarAddr();
    std::unordered_set<std::string> assignedVars;

    for (const auto &[varDecl, addrPtr] : varAddrMap) {
        if (!varDecl || !addrPtr)
            continue;
        std::string varName = varDecl->getNameAsString();

        auto expr = path.getVarState(varDecl);
        if (!expr)
            continue;

        auto varIt = vm.varIndexMap.find(varName);
        if (varIt == vm.varIndexMap.end())
            continue;

        int idx;
        if (init) {
            idx = varIt->second;
        } else {
            idx = varIt->second + vm.numVars;
        }
        Linear_Expression lhs = Parma_Polyhedra_Library::Variable(idx);

        Linear_Expression rhs = expr->toLinearExpr(vm.varIndexMap);

        Linear_Expression eq = lhs - rhs;
        result->add_constraint(Constraint(eq == 0));

        assignedVars.insert(varName);
    }
    if (!init) {
        for (const auto &[name, idx] : vm.varIndexMap) {
            if (assignedVars.contains(name))
                continue;

            int unprimed = idx;
            int primed   = idx + vm.numVars;

            Linear_Expression eq = Parma_Polyhedra_Library::Variable(primed) -
                                   Parma_Polyhedra_Library::Variable(unprimed);
            result->add_constraint(Constraint(eq == 0));
        }

        int half = vm.numVars / 2;
        for (int idx = half; idx < vm.numVars; ++idx) {
            int primed = idx + vm.numVars;
            Linear_Expression eq =
                Parma_Polyhedra_Library::Variable(primed) - Parma_Polyhedra_Library::Variable(idx);
            result->add_constraint(Constraint(eq == 0));
        }
    } else {
        int half = vm.numVars / 2;
        for (const auto &[name, idx] : vm.varIndexMap) {
            int initIdx = idx + half;

            Linear_Expression eq =
                Parma_Polyhedra_Library::Variable(initIdx) - Parma_Polyhedra_Library::Variable(idx);
            result->add_constraint(Constraint(eq == 0));
        }
    }

    return result;
}

// TODO: optimize to one cond, condition won't get multi cases.
vector<Formulas> buildLoopInvariant(Formulas loopCond,
                                    const vector<unique_ptr<Path>> &paths,
                                    const ProgramState &initState) {
    vector<Formulas> invariants;
    VarManager vm = VarManager::fromPaths(paths);

    vector<Formulas> processedCond = preprocessLoopCond(std::move(loopCond));

    const auto &initPaths = initState.getPaths();
    std::vector<Parma_Polyhedra_Library::C_Polyhedron *> initPathPolys;
    for (size_t i = 0; i < initPaths.size(); ++i) {
        if (initPaths[i])
            initPathPolys.push_back(buildPathPoly(*initPaths[i], vm, true));
        else
            UNREACHABLE();
    }

    std::vector<Parma_Polyhedra_Library::C_Polyhedron *> transPolys;
    for (const auto &path : paths) {
        if (path)
            transPolys.push_back(buildPathPoly(*path, vm));
        else
            UNREACHABLE();
    }

    std::vector<std::string> locations;

    locations.push_back("init");
    for (size_t i = 0; i < paths.size(); ++i) {
        locations.push_back("path_" + std::to_string(i));
    }
    locations.push_back("exit");

    int initIdx = 0;
    int exitIdx = paths.size() + 1;

    // TODO: fully disjunctive.
    auto identityPoly = buildIdentityPoly(vm); // shared across all paths
    for (size_t i = 0; i < processedCond.size(); ++i) {
        const Formulas &assertions = processedCond[i];
        auto *baseConditionPoly    = convertFormulaToPoly(assertions, vm);
        auto *primedConditionPoly  = primedPolyhedron(*baseConditionPoly, vm);

        auto negatedConds = negateFormulas(cloneFormulas(assertions));

        // === Precompute negated polyhedra
        std::vector<C_Polyhedron *> negatedPolys;
        for (const auto &neg : negatedConds) {
            auto *poly = convertFormulaToPoly(neg, vm);
            negatedPolys.push_back(poly);
        }

        for (auto *initPoly : initPathPolys) {
            std::vector<TransRel> transitions;

            // === init -> path_k ===
            for (size_t k = 0; k < paths.size(); ++k) {
                auto *poly = new C_Polyhedron(identityPoly);
                transitions.push_back(std::make_tuple(initIdx, static_cast<int>(k + 1), poly));
            }

            // === path_j -> path_k ===
            for (size_t j = 0; j < paths.size(); ++j) {
                for (size_t k = 0; k < paths.size(); ++k) {
                    auto *joined = new C_Polyhedron(*transPolys[j]);
                    joined->intersection_assign(*baseConditionPoly);
                    joined->intersection_assign(*primedConditionPoly);

                    if (!joined->is_empty()) {
                        transitions.push_back(std::make_tuple(static_cast<int>(j + 1),
                                                              static_cast<int>(k + 1), joined));
                    } else {
                        delete joined;
                    }
                }
            }

            // === path_j -> exit using precomputed negatedPolys
            for (size_t j = 0; j < paths.size(); ++j) {
                for (auto *negPoly : negatedPolys) {
                    auto *exitPoly = primedPolyhedron(*negPoly, vm);
                    exitPoly->intersection_assign(*transPolys[j]);
                    exitPoly->intersection_assign(*baseConditionPoly);
                    if (!exitPoly->is_empty()) {
                        transitions.push_back(
                            std::make_tuple(static_cast<int>(j + 1), exitIdx, exitPoly));
                    } else {
                        delete exitPoly;
                    }
                }
            }

            // === this initPoly as InitRel ===
            InitRel initRel = std::make_pair(initIdx, new C_Polyhedron(*initPoly));
            auto exit_invs  = computeLinearInv(locations, transitions, initRel, vm);

            for (const auto &poly : exit_invs) {
                Formulas fmls = convertPolyToFormula(poly, vm);
                invariants.push_back(std::move(fmls));
            }
            delete initRel.second;
        }

        delete baseConditionPoly;
        delete primedConditionPoly;
        for (auto *p : negatedPolys) {
            delete p;
        }
    }

    return invariants;
}
void dump(const Parma_Polyhedra_Library::C_Polyhedron &poly, const VarManager &vm) {
    using namespace Parma_Polyhedra_Library;

    const Constraint_System &cs = poly.constraints();
    size_t n                    = vm.numVars;
    size_t half                 = n / 2;

    for (const auto &c : cs) {
        std::ostringstream oss;
        bool first = true;

        size_t dim = c.space_dimension();
        for (size_t i = 0; i < dim; ++i) {
            Coefficient coeff = c.coefficient(Parma_Polyhedra_Library::Variable(i));
            if (coeff == 0)
                continue;

            if (!first && coeff > 0)
                oss << " + ";
            if (coeff < 0)
                oss << " - ";
            if (abs(coeff) != 1)
                oss << abs(coeff);

            std::string varName;
            if (i < half) {
                varName = vm.orderedVars[i];
            } else if (i < n) {
                varName = vm.orderedVars[i - half] + "_init";
            } else if (i < n + half) {
                varName = vm.orderedVars[i - n] + "'";
            } else {
                varName = vm.orderedVars[i - n - half] + "_init'";
            }

            oss << varName;
            first = false;
        }

        Coefficient inhom = c.inhomogeneous_term();
        if (inhom != 0) {
            if (!first && inhom > 0)
                oss << " + ";
            if (inhom < 0)
                oss << " - ";
            oss << abs(inhom);
        }

        switch (c.type()) {
            case Constraint::EQUALITY: oss << " == 0"; break;
            case Constraint::NONSTRICT_INEQUALITY: oss << " >= 0"; break;
            case Constraint::STRICT_INEQUALITY: oss << " > 0"; break;
            default: oss << " ?? 0"; break;
        }

        INFO("Constraint: " + oss.str());
    }
}

void dump(const Parma_Polyhedra_Library::Linear_Expression &expr, const VarManager &vm) {
    using namespace Parma_Polyhedra_Library;

    std::ostringstream oss;
    bool first = true;

    size_t n    = vm.numVars;
    size_t half = n / 2;
    size_t dim  = expr.space_dimension();

    for (size_t i = 0; i < dim; ++i) {
        Coefficient coeff = expr.coefficient(Parma_Polyhedra_Library::Variable(i));
        if (coeff == 0)
            continue;

        if (!first && coeff > 0)
            oss << " + ";
        if (coeff < 0)
            oss << " - ";
        if (abs(coeff) != 1)
            oss << abs(coeff);

        std::string varName;
        if (i < half) {
            varName = vm.orderedVars[i];
        } else if (i < n) {
            varName = vm.orderedVars[i - half] + "_init";
        } else if (i < n + half) {
            varName = vm.orderedVars[i - n] + "'";
        } else {
            varName = vm.orderedVars[i - n - half] + "_init'";
        }

        oss << varName;
        first = false;
    }

    Coefficient inhom = expr.inhomogeneous_term();
    if (inhom != 0 || first) {
        if (!first && inhom > 0)
            oss << " + ";
        if (inhom < 0)
            oss << " - ";
        oss << abs(inhom);
    }

    INFO("LinearExpr: " + oss.str());
}

/******************************************************************************\
 *                           Invariant-to-Path Extraction                     *
 *  This section converts computed invariants into symbolic execution paths,  *
 *  capturing the relationships between initial values and post-loop states.  *
 *                                                                            *
 *  It enables symbolic representation of postconditions by analyzing the     *
 *  invariant polyhedra and mapping them into logical formulas over symbolic  *
 *  variables. The generated paths are suitable for downstream ACSL or        *
 *  verification-based consumption.                                           *
\******************************************************************************/
