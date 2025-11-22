#include <iterator>
#include <llvm-19/llvm/Support/Casting.h>
#include <queue>
#include <regex>
#include "ppl.hh"

#include "Symbolic/aggregateExpr.h"
#include "expr.h"
#include "Analyzer/state.h"
#include "Stingx/LinTS.h"

namespace acslg::analyzer::symbolic {
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
        // Algebraic summaries of both sides.
        const int ldeg  = left_->getMaxDegree();
        const int rdeg  = right_->getMaxDegree();
        const bool linL = left_->isLinear();
        const bool linR = right_->isLinear();

        // A “constant” is defined as degree-0 and linear in this abstraction.
        const bool isConstL = (ldeg == 0) && linL;
        const bool isConstR = (rdeg == 0) && linR;

        switch (op_) {
            case Operator::Add:
            case Operator::Subtract:
                // Affine expressions are closed under addition/subtraction.
                return linL && linR;

            case Operator::Multiply:
                // Linear iff exactly one side is a constant (scalar multiplication).
                return (isConstL && linR) || (isConstR && linL);

            case Operator::ShiftLeft:
                // x << k == x * 2^k; linear if the shift amount is a compile-time constant.
                return isConstR && linL;

            default:
                // All other operators are considered non-linear in this abstraction.
                return false;
        }
    }

    int BinaryOpExpr::getMaxDegree() const {
        const int ldeg = left_->getMaxDegree();
        const int rdeg = right_->getMaxDegree();

        switch (op_) {
            case Operator::Add:
            case Operator::Subtract:
                // Degree is the maximum of operand degrees; invalid if any side is invalid.
                if (ldeg < 0 || rdeg < 0)
                    return -1;
                return std::max(ldeg, rdeg);

            case Operator::Multiply: return (ldeg >= 0 && rdeg >= 0) ? (ldeg + rdeg) : -1;

            case Operator::ShiftLeft:
                // For x << k with constant k (enforced in isLinear), degree equals degree(x).
                // Otherwise invalid.
                if (rdeg == 0 && ldeg >= 0)
                    return ldeg;
                return -1;

            default:
                // Non-linear or unsupported operators yield an invalid degree.
                return -1;
        }
    }

    std::optional<Parma_Polyhedra_Library::Linear_Expression> LiteralExpr::toLinearExpr(
        const std::unordered_map<std::string, size_t> &) const {
        using namespace Parma_Polyhedra_Library;
        switch (type_) {
            case LiteralType::Boolean: return Linear_Expression(data_.boolValue ? 1 : 0);
            case LiteralType::Int: return Linear_Expression(data_.intValue);
            case LiteralType::UnsignedInt:
                return Linear_Expression(static_cast<int>(data_.uintValue));
            case LiteralType::Short: return Linear_Expression(static_cast<int>(data_.shortValue));
            case LiteralType::UnsignedShort:
                return Linear_Expression(static_cast<int>(data_.ushortValue));
            case LiteralType::Int64:
                return Linear_Expression(static_cast<Coefficient>(data_.int64Value));
            case LiteralType::UInt64:
                return Linear_Expression(static_cast<Coefficient>(data_.uint64Value));
            default: throw runtime_error("Unsupported LiteralExpr type_ in toLinearExpr");
        }
    }

    std::optional<Parma_Polyhedra_Library::Linear_Expression> BinaryOpExpr::toLinearExpr(
        const std::unordered_map<std::string, size_t> &varIndexMap) const {
        auto L = left_->toLinearExpr(varIndexMap);
        auto R = right_->toLinearExpr(varIndexMap);
        if (L == std::nullopt || R == std::nullopt)
            return std::nullopt;

        switch (op_) {
            case Operator::Add: return L.value() + R.value();
            case Operator::Subtract: return L.value() - R.value();
            case Operator::Multiply:
                if (right_->getMaxDegree() == 0)
                    return L.value() * R.value().inhomogeneous_term();
                if (left_->getMaxDegree() == 0)
                    return R.value() * L.value().inhomogeneous_term();
                break;
            case Operator::Divide:
                if (right_->getMaxDegree() == 0) {
                    auto denom = R.value().inhomogeneous_term();
                    if (denom != 0) {
                        Parma_Polyhedra_Library::Linear_Expression result(0);
                        int maxDim = L.value().space_dimension();
                        for (int i = 0; i < maxDim; ++i) {
                            auto coeff =
                                L.value().coefficient(Parma_Polyhedra_Library::Variable(i));
                            if (coeff != 0)
                                result += (coeff / denom) * Parma_Polyhedra_Library::Variable(i);
                        }
                        result += L.value().inhomogeneous_term() / denom;
                        return result;
                    }
                }
                break;
            default: break;
        }

        ERROR("BinaryOpExpr: non-affine or unsupported operator");
    }

    std::optional<Parma_Polyhedra_Library::Linear_Expression> UnaryOpExpr::toLinearExpr(
        const std::unordered_map<std::string, size_t> &varIndexMap) const {
        auto E = expr_->toLinearExpr(varIndexMap);
        if (E == std::nullopt)
            return std::nullopt;

        switch (op_) {
            case Operator::Plus: return E.value();
            case Operator::Minus: return -E.value();
            default: ERROR("UnaryOpExpr: non-affine or unsupported operator");
        }
    }

    std::optional<Parma_Polyhedra_Library::Linear_Expression> symbolic::SymbolValue::toLinearExpr(
        const std::unordered_map<std::string, size_t> &varIndexMap) const {
        using namespace Parma_Polyhedra_Library;
        Linear_Expression e(0);

        auto varAddr = llvm::dyn_cast<const VariableAddress>(fromAddr_.get().get());
        if (varAddr == nullptr)
            return std::nullopt;

        auto it = varIndexMap.find(varAddr->getFrom()->getNameAsString());
        if (it == varIndexMap.end()) {
            ERROR("Variable '" + varAddr->getFrom()->getNameAsString() +
                  "' not found in index std::map.");
        }
        e += Parma_Polyhedra_Library::Variable(it->second);
        return e;
    }

    Parma_Polyhedra_Library::Linear_Expression LiteralExpr::toLinearExpr(
        const std::unordered_map<size_t, size_t> &) const {
        switch (type_) {
            case LiteralType::Boolean:
                return Parma_Polyhedra_Library::Linear_Expression(data_.boolValue ? 1 : 0);
            case LiteralType::Int:
                return Parma_Polyhedra_Library::Linear_Expression(data_.intValue);
            case LiteralType::UnsignedInt:
                return Parma_Polyhedra_Library::Linear_Expression(
                    static_cast<int>(data_.uintValue));
            case LiteralType::Short:
                return Parma_Polyhedra_Library::Linear_Expression(
                    static_cast<int>(data_.shortValue));
            case LiteralType::UnsignedShort:
                return Parma_Polyhedra_Library::Linear_Expression(
                    static_cast<int>(data_.ushortValue));
            case LiteralType::Int64:
                return Parma_Polyhedra_Library::Linear_Expression(
                    static_cast<Parma_Polyhedra_Library::Coefficient>(data_.int64Value));
            case LiteralType::UInt64:
                return Parma_Polyhedra_Library::Linear_Expression(
                    static_cast<Parma_Polyhedra_Library::Coefficient>(data_.uint64Value));
            default: throw runtime_error("Unsupported LiteralExpr type_ in toLinearExpr");
        }
    }

    Parma_Polyhedra_Library::Linear_Expression BinaryOpExpr::toLinearExpr(
        const std::unordered_map<size_t, size_t> &hashIdMap) const {
        auto L = left_->toLinearExpr(hashIdMap);
        auto R = right_->toLinearExpr(hashIdMap);

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
                        auto maxDim = L.space_dimension(); // total variable dimensions
                        for (size_t i = 0; i < maxDim; ++i) {
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
            case Operator::ShiftLeft:
                if (right_->getMaxDegree() == 0) {
                    if (auto k = right_->tryEvalAsConstant(); k && *k >= 0) {
                        Parma_Polyhedra_Library::Coefficient factor(1);
                        factor <<= static_cast<unsigned>(*k);
                        return L * factor;
                    }
                }
                break;
            default: break;
        }
        ERROR("non-affine or unsupported op");
    }

    std::optional<Parma_Polyhedra_Library::Linear_Expression> SymbolAddress::toLinearExpr(
        const std::unordered_map<std::string, size_t> &varIndexMap) const {
        if (length_ != std::nullopt)
            ERROR("Address range is solely for address representation and should not be "
                  "used as an expression.");
        using namespace Parma_Polyhedra_Library;
        Linear_Expression e(0);
        if (getDimension() != 1)
            return std::nullopt;

        auto varDecl = getFromRoot();
        if (varDecl == std::nullopt)
            ERROR("Failed to retrieve the original varDecl.");
        auto it = varIndexMap.find(varDecl.value()->getNameAsString());
        if (it == varIndexMap.end()) {
            ERROR("Address '" + dump() + "' not found in index std::map.");
        }
        e += Parma_Polyhedra_Library::Variable(it->second);
        return e;
    }

    Parma_Polyhedra_Library::Linear_Expression UnaryOpExpr::toLinearExpr(
        const std::unordered_map<size_t, size_t> &hashIdMap) const {
        auto E = expr_->toLinearExpr(hashIdMap);

        switch (op_) {
            case Operator::Plus: return E;
            case Operator::Minus: return -E;
            default: break;
        }

        ERROR("non-affine or unsupported op");
    }

    Parma_Polyhedra_Library::Linear_Expression symbolic::SymbolValue::toLinearExpr(
        const std::unordered_map<size_t, size_t> &hashIdMap) const {
        Parma_Polyhedra_Library::Linear_Expression e(0);
        if (auto it = hashIdMap.find(hash()); it != hashIdMap.end()) {
            auto id  = it->second;
            auto var = Parma_Polyhedra_Library::Variable(id);
            e += var;
            return e;
        } else {
            ERROR("Hash of Variable: {" + dump() + "} can't be found.");
        }
    }

    Parma_Polyhedra_Library::Linear_Expression SymbolAddress::toLinearExpr(
        const std::unordered_map<size_t, size_t> &hashIdMap) const {
        if (length_ != std::nullopt)
            ERROR("Address range is solely for address representation and should not be "
                  "used as an expression.");
        Parma_Polyhedra_Library::Linear_Expression e(0);
        if (auto it = hashIdMap.find(hash()); it != hashIdMap.end()) {
            auto id  = it->second;
            auto var = Parma_Polyhedra_Library::Variable(id);
            e += var;
            return e;
        } else {
            ERROR("Hash of Variable: {" + dump() + "} can't be found.");
        }
    }

    Parma_Polyhedra_Library::Linear_Expression SumOverRange::toLinearExpr(
        const std::unordered_map<size_t, size_t> &hashIdMap) const {
        Parma_Polyhedra_Library::Linear_Expression e(0);
        if (auto it = hashIdMap.find(hash()); it != hashIdMap.end()) {
            auto id  = it->second;
            auto var = Parma_Polyhedra_Library::Variable(id);
            e += var;
            return e;
        } else {
            ERROR("Hash of Variable: {" + dump() + "} can't be found.");
        }
    }

} // namespace acslg::analyzer::symbolic

namespace acslg::analyzer {
    namespace details {
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
                    if (abs(coeff) != 1) {
                        oss << abs(coeff);
                        oss << "*";
                    }

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

        Parma_Polyhedra_Library::Constraint primedConstriant(
            const Parma_Polyhedra_Library::Constraint &c,
            const VarManager &vm) {
            Linear_Expression shiftedExpr(0);
            auto spaceDim = c.space_dimension();

            for (size_t i = 0; i < spaceDim; ++i) {
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

        Parma_Polyhedra_Library::C_Polyhedron primedPolyhedron(
            const Parma_Polyhedra_Library::C_Polyhedron &poly,
            const VarManager &vm) {
            using namespace Parma_Polyhedra_Library;

            auto primed = C_Polyhedron{vm.numVars * 2, UNIVERSE};

            Constraint_System cs = poly.constraints();
            for (Constraint_System::const_iterator it = cs.begin(); it != cs.end(); ++it) {
                Constraint primedC = primedConstriant(*it, vm);
                primed.add_constraint(primedC);
            }

            return primed;
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

            auto dim = vm.numVars * 2;
            C_Polyhedron poly(dim, UNIVERSE);

            for (size_t i = 0; i < vm.numVars; ++i) {
                Parma_Polyhedra_Library::Variable unprimed(i);
                Parma_Polyhedra_Library::Variable primed(i + vm.numVars);
                Linear_Expression eq = primed - unprimed;
                poly.add_constraint(Constraint(eq == 0));
            }

            return poly;
        }

        std::vector<Formulas> negateFormulas(Formulas input) {
            using Op = symbolic::BinaryOpExpr::Operator;

            std::vector<Formulas> result;
            std::queue<pair<Formulas, const size_t>> worklist;
            worklist.push({std::move(input), 0});

            while (!worklist.empty()) {
                auto [current, startIdx] = std::move(worklist.front());
                worklist.pop();

                bool expanded = false;

                for (size_t i = startIdx; i < current.size(); ++i) {
                    auto *bin = llvm::dyn_cast<symbolic::BinaryOpExpr>(current[i].get().get());
                    if (!bin) {
                        ERROR("negateFormulas: input[" + to_string(i) + "] is not a BinaryOpExpr");
                    }

                    const auto &lhs = bin->getLeft();
                    const auto &rhs = bin->getRight();

                    // @WindOctober: try to optimize clone.
                    switch (bin->getOperator()) {
                        case Op::LessEqual: {
                            auto newRHS = std::make_unique<symbolic::BinaryOpExpr>(
                                rhs->clone(), Op::Add, std::make_unique<symbolic::LiteralExpr>(1));
                            current[i] = std::make_unique<symbolic::BinaryOpExpr>(
                                lhs->clone(), Op::GreaterEqual, std::move(newRHS));
                            worklist.push({std::move(current), i + 1});
                            expanded = true;
                            break;
                        }
                        case Op::GreaterEqual: {
                            auto newRHS = std::make_unique<symbolic::BinaryOpExpr>(
                                rhs->clone(), Op::Subtract,
                                std::make_unique<symbolic::LiteralExpr>(1));
                            current[i] = std::make_unique<symbolic::BinaryOpExpr>(
                                lhs->clone(), Op::LessEqual, std::move(newRHS));
                            worklist.push({std::move(current), i + 1});
                            expanded = true;
                            break;
                        }
                        case Op::Equal: {
                            auto leExpr = std::make_unique<symbolic::BinaryOpExpr>(
                                lhs->clone(), Op::LessEqual,
                                std::make_unique<symbolic::BinaryOpExpr>(
                                    rhs->clone(), Op::Subtract,
                                    std::make_unique<symbolic::LiteralExpr>(1)));

                            auto geExpr = std::make_unique<symbolic::BinaryOpExpr>(
                                lhs->clone(), Op::GreaterEqual,
                                std::make_unique<symbolic::BinaryOpExpr>(
                                    rhs->clone(), Op::Add,
                                    std::make_unique<symbolic::LiteralExpr>(1)));

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

        Formulas preprocessConjConds(const PathConditions &conjConds) {
            Formulas copied;
            copied.reserve(conjConds.size());
            for (const auto &cond : conjConds) {
                copied.push_back(cond->clone());
            }
            return preprocessConjConds(copied);
        }

        Formulas preprocessConjConds(const Formulas &conjConds) {
            Formulas result;
            for (auto &cond : conjConds) {
                if (auto bin = llvm::dyn_cast<symbolic::BinaryOpExpr>(cond.get().get())) {
                    using enum symbolic::BinaryOpExpr::Operator;
                    const auto &lhs = bin->getLeft();
                    const auto &rhs = bin->getRight();

                    switch (bin->getOperator()) {
                        case NotEqual: {
                            // Create disjunctive conds, just skip this now.
                            continue;
                        }
                        case GreaterThan: {
                            auto newRHS = std::make_unique<symbolic::BinaryOpExpr>(
                                rhs->clone(), Add, std::make_unique<symbolic::LiteralExpr>(1));
                            result.push_back(std::make_unique<symbolic::BinaryOpExpr>(
                                lhs->clone(), GreaterEqual, std::move(newRHS)));
                            break;
                        }
                        case LessThan: {
                            auto newRHS = std::make_unique<symbolic::BinaryOpExpr>(
                                rhs->clone(), Subtract, std::make_unique<symbolic::LiteralExpr>(1));
                            result.push_back(std::make_unique<symbolic::BinaryOpExpr>(
                                lhs->clone(), LessEqual, std::move(newRHS)));
                            break;
                        }
                        case LogicalAnd: {
                            Formulas twoConds;
                            twoConds.reserve(2);
                            twoConds.push_back(lhs->clone());
                            twoConds.push_back(rhs->clone());
                            auto reTwoConds = preprocessConjConds(twoConds);
                            result.insert(result.end(), std::make_move_iterator(reTwoConds.begin()),
                                          std::make_move_iterator(reTwoConds.end()));
                            break;
                        }
                        case GreaterEqual:
                        case LessEqual:
                        case Equal: {
                            result.push_back(cond->clone());
                            break;
                        }
                        default: continue;
                    }
                } else if (auto unary = llvm::dyn_cast<symbolic::UnaryOpExpr>(cond.get().get())) {
                    if (unary->getOperator() != symbolic::UnaryOpExpr::Operator::LogicalNot)
                        continue;
                    Formulas oneExpr;
                    oneExpr.push_back(unary->getSub()->clone());
                    auto reOneExpr = preprocessConjConds(oneExpr);
                    if (reOneExpr.size() != 1)
                        continue;
                    auto uneqExpr =
                        llvm::dyn_cast<const symbolic::BinaryOpExpr>(reOneExpr.front().get().get());
                    assert(uneqExpr);
                    switch (uneqExpr->getOperator()) {
                        using enum symbolic::BinaryOpExpr::Operator;
                        case LessEqual: {
                            auto newRHS = std::make_unique<symbolic::BinaryOpExpr>(
                                uneqExpr->getRight()->clone(), Add,
                                std::make_unique<symbolic::LiteralExpr>(1));
                            result.push_back(std::make_unique<symbolic::BinaryOpExpr>(
                                uneqExpr->getLeft()->clone(), GreaterEqual, std::move(newRHS)));
                            break;
                        }
                        case GreaterEqual: {
                            auto newRHS = std::make_unique<symbolic::BinaryOpExpr>(
                                uneqExpr->getRight()->clone(), Subtract,
                                std::make_unique<symbolic::LiteralExpr>(1));
                            result.push_back(std::make_unique<symbolic::BinaryOpExpr>(
                                uneqExpr->getLeft()->clone(), LessEqual, std::move(newRHS)));
                            break;
                        }
                        default: continue;
                    }
                }
            }

            return result;
        }

        Parma_Polyhedra_Library::C_Polyhedron buildPathPoly(const Path &path,
                                                            const VarManager &vm,
                                                            bool init) {
            using namespace Parma_Polyhedra_Library;

            auto dim = init ? vm.numVars : vm.numVars * 2;

            auto result = C_Polyhedron{dim, UNIVERSE};

            const auto &varAddrMap = path.getVarAddr();
            std::unordered_set<std::string> assignedVars;

            for (const auto &[varDecl, addrPtr] : varAddrMap) {
                if (!varDecl)
                    UNREACHABLE();
                std::string varName = varDecl->getNameAsString();

                auto varIt = vm.varIndexMap.find(varName);
                if (varIt == vm.varIndexMap.end())
                    continue;

                auto expr = path.getVarState(varDecl);

                size_t idx;
                if (init) {
                    idx = varIt->second;
                } else {
                    idx = varIt->second + vm.numVars;
                }
                Linear_Expression lhs = Parma_Polyhedra_Library::Variable(idx);

                auto rhs = expr->toLinearExpr(vm.varIndexMap);
                if (rhs == std::nullopt)
                    continue; // Ignore this variable.

                Linear_Expression eq = lhs - rhs.value();
                result.add_constraint(Constraint(eq == 0));

                assignedVars.insert(varName);
            }
            if (!init) {
                // Variables that can't be deal with (mainly because their value contains symbolic
                // values from pointer or std::array). for (const auto &[name, idx] :
                // vm.varIndexMap) {
                //     if (assignedVars.contains(name))
                //         continue;

                //     auto unprimed = idx;
                //     auto primed   = idx + vm.numVars;

                //     Linear_Expression eq = Parma_Polyhedra_Library::Variable(primed) -
                //                            Parma_Polyhedra_Library::Variable(unprimed);
                //     result.add_constraint(Constraint(eq == 0));
                // }

                auto half = vm.numVars / 2;
                for (size_t idx = half; idx < vm.numVars; ++idx) {
                    auto primed          = idx + vm.numVars;
                    Linear_Expression eq = Parma_Polyhedra_Library::Variable(primed) -
                                           Parma_Polyhedra_Library::Variable(idx);
                    result.add_constraint(Constraint(eq == 0));
                }
            } else {
                auto half = vm.numVars / 2;
                for (const auto &[name, idx] : vm.varIndexMap) {
                    auto initIdx = idx + half;

                    Linear_Expression eq = Parma_Polyhedra_Library::Variable(initIdx) -
                                           Parma_Polyhedra_Library::Variable(idx);
                    result.add_constraint(Constraint(eq == 0));
                }
            }

            return result;
        }

        // todo: should be a virtual function of `SymbolicExpr`
        std::optional<Parma_Polyhedra_Library::Constraint> toConstraint(
            const symbolic::SymbolicExpr *expr,
            const VarManager &vm) {
            auto bin = llvm::dyn_cast_if_present<const symbolic::BinaryOpExpr>(expr);
            if (bin == nullptr) {
                WARN("toConstraint: expression must be a BinaryOpExpr.");
                return std::nullopt;
            }

            const auto &op = bin->getOperator();

            auto lhs = bin->getLeft()->toLinearExpr(vm.varIndexMap);
            auto rhs = bin->getRight()->toLinearExpr(vm.varIndexMap);
            if (lhs == std::nullopt || rhs == std::nullopt)
                return std::nullopt;
            auto le = lhs.value() - rhs.value();

            switch (op) {
                case symbolic::BinaryOpExpr::Operator::LessEqual:
                    return Parma_Polyhedra_Library::Constraint(le <= 0);
                case symbolic::BinaryOpExpr::Operator::GreaterEqual:
                    return Parma_Polyhedra_Library::Constraint(le >= 0);
                case symbolic::BinaryOpExpr::Operator::Equal:
                    return Parma_Polyhedra_Library::Constraint(le == 0);
                default:
                    WARN("toConstraint: unsupported binary operator in assertion (must be <=, >=, "
                         "==).");
                    return std::nullopt;
            }
        }

        Parma_Polyhedra_Library::C_Polyhedron convertFormulaToPoly(const Formulas &assertions,
                                                                   const VarManager &vm) {
            if (vm.numVars == 0) {
                ERROR("convertFormulaToPoly: empty variable std::map");
            }

            auto dimension = vm.numVars * 2;
            auto poly =
                Parma_Polyhedra_Library::C_Polyhedron{dimension, Parma_Polyhedra_Library::UNIVERSE};

            for (const auto &assertion : assertions) {
                const symbolic::SymbolicExpr *rawExpr = assertion.get().get();
                auto constraint                       = toConstraint(rawExpr, vm);
                if (constraint == std::nullopt)
                    continue;
                poly.add_constraint(constraint.value());
            }

            return poly;
        }

        [[deprecated("Arguments simplified")]] [[maybe_unused]]
        PathsAndExitInvs computeLinearInv(const std::vector<std::string> &locations,
                                          const std::vector<TransRel> &transitions,
                                          const InitRel &initial,
                                          const VarManager &vm) {
            auto linTS = std::make_unique<LinTS>();

            for (const std::string &name : vm.orderedVars) {
                linTS->addVariable(name.c_str());
            }

            for (size_t i = 0; i < locations.size(); ++i) {
                const std::string &locName = locations[i];
                if (static_cast<int>(i) == initial.first) {
                    linTS->addLocInit(locName.c_str(), initial.second);
                } else {
                    linTS->addLocInit(locName.c_str(), nullptr);
                }
            }

            for (size_t i = 0; i < transitions.size(); ++i) {
                const auto &[src, dst, transPoly] = transitions[i];
                std::string transName             = "t" + to_string(i);

                linTS->addTransRel(transName.c_str(), locations[src].c_str(),
                                   locations[dst].c_str(), transPoly);
            }

            linTS->ComputeLinTSInv();
            auto invariants = linTS->getInvMap();

            PathsAndExitInvs result;
            for (auto &pathName : locations) {
                if (pathName == "init" || pathName == "exit")
                    continue;

                std::vector<Parma_Polyhedra_Library::C_Polyhedron> pathInvs;

                for (auto pathInv : invariants[pathName])
                    pathInvs.push_back(*pathInv);
                result.pathsInvs.push_back(std::move(pathInvs));
            }

            const auto &exitInvariants = invariants["exit"];

            for (const auto *p : exitInvariants) {
                result.exitInvs.emplace_back(*p);
            }

            if (result.exitInvs.empty()) {
                // TODO: exitInvs_.size() is unstable now.
                result.pathsInvs.clear();
            }

            return result;
        }

        PathsAndExitInvs computeLinearInv(
            const std::vector<std::string> &locations,
            std::vector<std::tuple<size_t, size_t, C_Polyhedron>> &transitions,
            const std::pair<size_t, C_Polyhedron> &initial,
            const VarManager &vm) {
            auto linTS = std::make_unique<LinTS>();

            for (auto &name : vm.orderedVars) {
                linTS->addVariable(name.c_str());
            }

            for (size_t i = 0; i < locations.size(); ++i) {
                auto &locName = locations[i];
                if (i == initial.first) {
                    linTS->addLocInit(locName.c_str(), &(initial.second));
                } else {
                    linTS->addLocInit(locName.c_str(), nullptr);
                }
            }

            for (size_t i = 0; i < transitions.size(); ++i) {
                auto &[src, dst, transPoly] = transitions[i];
                std::string transName       = "t" + to_string(i);

                linTS->addTransRel(transName.c_str(), locations[src].c_str(),
                                   locations[dst].c_str(), &transPoly);
            }

            linTS->ComputeLinTSInv();
            auto invariants = linTS->getInvMap();

            PathsAndExitInvs result;
            for (auto &pathName : locations) {
                if (pathName == "init" || pathName == "exit")
                    continue;

                std::vector<Parma_Polyhedra_Library::C_Polyhedron> pathInvs;

                for (auto pathInv : invariants[pathName])
                    pathInvs.push_back(*pathInv);
                result.pathsInvs.push_back(std::move(pathInvs));
            }

            const auto &exitInvariants = invariants["exit"];

            for (const auto *p : exitInvariants) {
                result.exitInvs.emplace_back(*p);
            }

            if (result.exitInvs.empty()) {
                ERROR("?");
                // TODO: exitInvs_.size() is unstable now.
                result.pathsInvs.clear();
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
         *                           Invariant-to-Path Extraction                     *
         *  This section converts computed invariants into symbolic execution paths,  *
         *  capturing the relationships between initial values and post-loop states.  *
         *                                                                            *
         *  It enables symbolic representation of postconditions by analyzing the     *
         *  invariant polyhedra and mapping them into logical formulas over symbolic  *
         *  variables. The generated paths are suitable for downstream ACSL or        *
         *  verification-based consumption.                                           *
        \******************************************************************************/

        std::optional<std::string> buildInvs(
            const std::vector<vector<Parma_Polyhedra_Library::C_Polyhedron>> &polyss,
            const VarManager &vm) {
            auto getConjunctiveInvs = [&](const Parma_Polyhedra_Library::C_Polyhedron poly)
                -> std::optional<std::string> {
                std::string spec;
                bool firstLine        = true;
                size_t outputConsSize = 0;
                for (auto &constraint : poly.constraints()) {
                    using namespace Parma_Polyhedra_Library;
                    std::string invStr;
                    bool first = true;

                    auto n    = vm.numVars;
                    auto half = n / 2;
                    auto dim  = constraint.space_dimension();

                    for (size_t i = 0; i < dim; ++i) {
                        auto coeff = constraint.coefficient(Parma_Polyhedra_Library::Variable(i));
                        if (coeff == 0)
                            continue;

                        if (!first && coeff > 0)
                            invStr += " + ";
                        else if (coeff < 0)
                            invStr += " - ";
                        if (abs(coeff) != 1) {
                            invStr += to_string(abs(coeff.get_si()));
                            invStr += "*";
                        }

                        std::string varName;
                        if (i < half) {
                            varName = vm.orderedVars[i];
                        } else if (i < n) {
                            varName = "\\at(" + vm.orderedVars[i - half] + ", LoopEntry)";
                        } else {
                            UNREACHABLE();
                        }

                        invStr += varName;
                        first = false;
                    }

                    Coefficient inhom = constraint.inhomogeneous_term();
                    if (inhom != 0 || first) {
                        if (!first && inhom > 0)
                            invStr += " + ";
                        if (inhom < 0)
                            invStr += " - ";
                        invStr += to_string(abs(inhom.get_si()));
                    }

                    switch (constraint.type()) {
                        case Constraint::EQUALITY: invStr += " == 0"; break;
                        case Constraint::NONSTRICT_INEQUALITY: invStr += " >= 0"; break;
                        case Constraint::STRICT_INEQUALITY: invStr += " > 0"; break;
                        default: UNREACHABLE();
                    }

                    // like "x - \at(x, LoopEntry) == 0"
                    constexpr auto pattern_str =
                        R"(([a-zA-Z_]\w*)\s*-\s*\\at\s*\(\s*\1\s*,\s*LoopEntry\s*\)\s*==\s*0)";
                    std::regex rx(pattern_str);
                    if (std::regex_match(invStr, rx)) {
                        // Ignore this, it's "loop assigns"'s job.
                        continue;
                    }

                    ++outputConsSize;
                    if (firstLine) {
                        firstLine = false;
                        spec += "(   ";
                    } else {
                        spec += "\n    "; // 4 spaces
                        spec += " && ";
                    }

                    spec += std::move(invStr);
                }
                if (spec.empty())
                    return std::nullopt;
                if (outputConsSize != 1)
                    spec += "\n";
                spec += "    "; // 4 spaces
                spec += ")\n";
                return spec;
            }; // getConjunctiveInvs ends

            std::string spec;
            bool firstBlock = true;
            for (auto &polys : polyss) {
                for (auto &poly : polys) {
                    auto conjStr = getConjunctiveInvs(poly);
                    if (conjStr == std::nullopt)
                        continue;
                    if (firstBlock) {
                        firstBlock = false;
                        spec += "    "; // 4 spaces;
                    } else {
                        spec += " || ";
                    }
                    spec += std::move(conjStr.value());
                }
            }
            if (spec.empty())
                return std::nullopt;
            return "loop invariant\n" + std::move(spec) + ";";
        }

        AddrValueAndCondsPair buildPostState(const C_Polyhedron &poly,
                                             const Path &initPath,
                                             const VarManager &vm) {
            using namespace Parma_Polyhedra_Library;
            using R = std::pair<
                symbolic::AddressBoxMap<utils::not_null<unique_ptr<symbolic::SymbolicExpr>>>,
                std::vector<utils::not_null<unique_ptr<symbolic::SymbolicExpr>>>>;
            auto n    = vm.numVars;
            auto half = n / 2;

            std::unordered_map<int, utils::not_null<unique_ptr<symbolic::SymbolicExpr>>>
                resolvedExprs;
            for (size_t i = half; i < n; ++i) {
                auto trueDecl = vm.varDecls.at(i - half);
                resolvedExprs.emplace(i, initPath.getVarState(trueDecl));
            }

            auto &cs = poly.constraints();

            bool changed = true;
            while (changed) {
                changed = false;

                for (auto &constraint : cs) {
                    if (!constraint.is_equality())
                        continue;

                    std::map<size_t, Coefficient> coeffs;
                    Coefficient constant = constraint.inhomogeneous_term();
                    auto maxIdx          = constraint.space_dimension();

                    for (size_t i = 0; i < maxIdx; ++i) {
                        Coefficient c =
                            constraint.coefficient(Parma_Polyhedra_Library::Variable(i));
                        if (c != 0)
                            coeffs[i] = c;
                    }

                    std::vector<size_t> unknowns;
                    for (const auto &[idx, coeff] : coeffs) {
                        if (idx < half && !resolvedExprs.contains(idx))
                            unknowns.push_back(idx);
                    }

                    if (unknowns.size() != 1)
                        continue;
                    auto target = unknowns[0];

                    if (resolvedExprs.contains(target)) {
                        WARN("This variable has been resolved, and the new value is discarded.");
                        continue;
                    }

                    std::unique_ptr<symbolic::SymbolicExpr> rhs =
                        std::make_unique<symbolic::LiteralExpr>(-constant.get_si());

                    for (const auto &[idx, coeff] : coeffs) {
                        if (idx == target)
                            continue;

                        symbolic::SymbolicExpr *base = resolvedExprs.at(idx).get().get();

                        auto term = base->clone();
                        if (coeff != 1) {
                            term = std::make_unique<symbolic::BinaryOpExpr>(

                                std::make_unique<symbolic::LiteralExpr>(coeff.get_si()),
                                symbolic::BinaryOpExpr::Operator::Multiply, std::move(term));
                        }

                        rhs = std::make_unique<symbolic::BinaryOpExpr>(
                            std::move(rhs), symbolic::BinaryOpExpr::Operator::Subtract,
                            std::move(term));
                    }

                    if (coeffs[target] == -1) {
                        rhs = std::make_unique<symbolic::UnaryOpExpr>(
                            symbolic::UnaryOpExpr::Operator::Minus, std::move(rhs));
                    } else if (coeffs[target] != 1) {
                        rhs = std::make_unique<symbolic::BinaryOpExpr>(
                            std::move(rhs), symbolic::BinaryOpExpr::Operator::Divide,
                            std::make_unique<symbolic::LiteralExpr>(coeffs[target].get_si()));
                    }

                    auto [_, ok] = resolvedExprs.emplace(target, std::move(rhs));
                    if (!ok)
                        UNREACHABLE();

                    changed = true;
                }
            }

            symbolic::AddressBoxMap<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>>
                newVars;
            std::vector<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>> conds;

            for (size_t i = 0; i < half; ++i) {
                auto varDecl = vm.varDecls.at(i);
                auto &addr   = initPath.getVarAddr().at(varDecl);
                if (resolvedExprs.contains(i)) {
                    auto [_, ok] = newVars.emplace(*addr, resolvedExprs.at(i)->clone());
                    if (!ok)
                        UNREACHABLE();
                }
            }

            for (auto &constraint : cs) {
                std::optional<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>> lhs{};
                auto maxIdx = constraint.space_dimension();
                for (size_t i = 0; i < maxIdx; ++i) {
                    Coefficient c = constraint.coefficient(Parma_Polyhedra_Library::Variable(i));
                    if (c == 0)
                        continue;

                    symbolic::SymbolicExpr *base = nullptr;
                    if (resolvedExprs.contains(i))
                        base = resolvedExprs.at(i).get().get();
                    else {
                        // Unresolved expression is too complex (because of non-linear or pointer),
                        // just ignore it.
                        continue;
                    }

                    auto term = base->clone();
                    if (c != 1) {
                        term = std::make_unique<symbolic::BinaryOpExpr>(
                            std::make_unique<symbolic::LiteralExpr>(c.get_si()),
                            symbolic::BinaryOpExpr::Operator::Multiply, std::move(term));
                    }

                    if (!lhs) {
                        lhs = std::move(term);
                    } else {
                        lhs = std::make_unique<symbolic::BinaryOpExpr>(
                            std::move(lhs.value()), symbolic::BinaryOpExpr::Operator::Add,
                            std::move(term));
                    }
                }

                if (!lhs) {
                    continue;
                }

                Coefficient c0 = constraint.inhomogeneous_term();
                if (c0 != 0) {
                    lhs = std::make_unique<symbolic::BinaryOpExpr>(
                        std::move(lhs.value()), symbolic::BinaryOpExpr::Operator::Add,
                        std::make_unique<symbolic::LiteralExpr>(c0.get_si()));
                }

                symbolic::BinaryOpExpr::Operator op;
                if (constraint.is_equality()) {
                    op = symbolic::BinaryOpExpr::Operator::Equal;
                } else if (constraint.is_strict_inequality()) {
                    op = symbolic::BinaryOpExpr::Operator::GreaterEqual;
                } else if (constraint.is_inequality()) {
                    op = symbolic::BinaryOpExpr::Operator::GreaterThan;
                } else {
                    continue;
                }

                auto cond = std::make_unique<symbolic::BinaryOpExpr>(
                    std::move(lhs.value()), op, std::make_unique<symbolic::LiteralExpr>(0));

                conds.push_back(std::move(cond));
            }
            return R{std::move(newVars), std::move(conds)};
        }
    } // namespace details
} // namespace acslg::analyzer
