#include "expr.h"
#include "ppl.hh"
#include "Analyzer/state.h"
#include "Stingx/LinTS.h"
#include <queue>

namespace acslg::analyzer::symbolic {
    namespace {
        [[maybe_unused]]
        inline void dump(const Parma_Polyhedra_Library::C_Polyhedron &poly, const VarManager &vm) {
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

        [[maybe_unused]]
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
    } // namespace

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
            auto regFrom = regularForm();
            ERROR("Address '" + (regFrom ? regFrom.value() : dump()) +
                  "' not found in index std::map.");
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

} // namespace acslg::analyzer::symbolic

namespace acslg::analyzer {
    namespace {
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

        [[deprecated("Loop condition shouldn't be multiple")]] [[maybe_unused]]
        std::vector<Formulas> preprocessLoopCond(Formulas loopCond) {
            std::vector<Formulas> result;
            std::queue<pair<Formulas, const size_t>> worklist;
            worklist.push({std::move(loopCond), 0});

            while (!worklist.empty()) {
                auto [current, startIdx] = std::move(worklist.front());
                worklist.pop();

                bool expanded = false;

                for (size_t i = startIdx; i < current.size(); ++i) {
                    auto *bin = llvm::dyn_cast<symbolic::BinaryOpExpr>(current[i].get().get());
                    if (!bin) {
                        ERROR("preprocessLoopCond: loopCond[" + to_string(i) +
                              "] is not a BinaryOpExpr");
                    }

                    using enum symbolic::BinaryOpExpr::Operator;
                    const auto &lhs = bin->getLeft();
                    const auto &rhs = bin->getRight();

                    switch (bin->getOperator()) {
                        case NotEqual: {
                            auto rhsPlus1 = std::make_unique<symbolic::BinaryOpExpr>(
                                rhs->clone(), Add, std::make_unique<symbolic::LiteralExpr>(1));
                            auto geExpr = std::make_unique<symbolic::BinaryOpExpr>(
                                lhs->clone(), GreaterEqual, std::move(rhsPlus1));

                            auto rhsMinus1 = std::make_unique<symbolic::BinaryOpExpr>(
                                rhs->clone(), Subtract, std::make_unique<symbolic::LiteralExpr>(1));
                            auto leExpr = std::make_unique<symbolic::BinaryOpExpr>(
                                lhs->clone(), LessEqual, std::move(rhsMinus1));

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
                            auto newRHS = std::make_unique<symbolic::BinaryOpExpr>(
                                rhs->clone(), Add, std::make_unique<symbolic::LiteralExpr>(1));
                            current[i] = std::make_unique<symbolic::BinaryOpExpr>(
                                lhs->clone(), GreaterEqual, std::move(newRHS));
                            worklist.push({std::move(current), i + 1});
                            expanded = true;
                            break;
                        }
                        case LessThan: {
                            auto newRHS = std::make_unique<symbolic::BinaryOpExpr>(
                                rhs->clone(), Subtract, std::make_unique<symbolic::LiteralExpr>(1));
                            current[i] = std::make_unique<symbolic::BinaryOpExpr>(
                                lhs->clone(), LessEqual, std::move(newRHS));
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

        Parma_Polyhedra_Library::C_Polyhedron buildPathPoly(const Path &path,
                                                            const VarManager &vm,
                                                            bool init = false) {
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

        std::optional<Parma_Polyhedra_Library::Constraint> toConstraint(
            const symbolic::SymbolicExpr *expr,
            const VarManager &vm) {
            auto bin = llvm::dyn_cast_if_present<const symbolic::BinaryOpExpr>(expr);
            if (bin == nullptr) {
                ERROR("toConstraint: expression must be a BinaryOpExpr.");
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
                    ERROR("toConstraint: unsupported binary operator in assertion (must be <=, >=, "
                          "==).");
            }
        }

        std::optional<Parma_Polyhedra_Library::C_Polyhedron> convertFormulaToPoly(
            const Formulas &assertions,
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
                    return std::nullopt;
                poly.add_constraint(constraint.value());
            }

            return poly;
        }

        struct PathsAndExitInvs {
            std::vector<vector<Parma_Polyhedra_Library::C_Polyhedron>> pathsInvs_;
            std::vector<Parma_Polyhedra_Library::C_Polyhedron> exitInvs_;
        };

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
                result.pathsInvs_.push_back(std::move(pathInvs));
            }

            const auto &exitInvariants = invariants["exit"];

            for (const auto *p : exitInvariants) {
                result.exitInvs_.emplace_back(*p);
            }

            if (result.exitInvs_.empty()) {
                // TODO: exitInvs_.size() is unstable now.
                result.pathsInvs_.clear();
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
                result.pathsInvs_.push_back(std::move(pathInvs));
            }

            const auto &exitInvariants = invariants["exit"];

            for (const auto *p : exitInvariants) {
                result.exitInvs_.emplace_back(*p);
            }

            if (result.exitInvs_.empty()) {
                ERROR("?");
                // TODO: exitInvs_.size() is unstable now.
                result.pathsInvs_.clear();
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

        std::optional<std::string> buildInvs(const C_Polyhedron &poly, const VarManager &vm) {
            std::string spec;
            bool firstLine = true;
            for (auto &constraint : poly.constraints()) {
                using namespace Parma_Polyhedra_Library;
                if (!firstLine)
                    spec += "    "; // 4 spaces
                firstLine = false;
                spec += "loop invariant ";
                bool first = true;

                auto n    = vm.numVars;
                auto half = n / 2;
                auto dim  = constraint.space_dimension();

                for (size_t i = 0; i < dim; ++i) {
                    auto coeff = constraint.coefficient(Parma_Polyhedra_Library::Variable(i));
                    if (coeff == 0)
                        continue;

                    if (!first && coeff > 0)
                        spec += " + ";
                    else if (coeff < 0)
                        spec += " - ";
                    if (abs(coeff) != 1)
                        spec += to_string(abs(coeff.get_si()));

                    std::string varName;
                    if (i < half) {
                        varName = vm.orderedVars[i];
                    } else if (i < n) {
                        varName = "\\at(" + vm.orderedVars[i - half] + ", LoopEntry)";
                    } else {
                        UNREACHABLE();
                    }

                    spec += varName;
                    first = false;
                }

                Coefficient inhom = constraint.inhomogeneous_term();
                if (inhom != 0 || first) {
                    if (!first && inhom > 0)
                        spec += " + ";
                    if (inhom < 0)
                        spec += " - ";
                    spec += to_string(abs(inhom.get_si()));
                }

                switch (constraint.type()) {
                    case Constraint::EQUALITY: spec += " == 0"; break;
                    case Constraint::NONSTRICT_INEQUALITY: spec += " >= 0"; break;
                    case Constraint::STRICT_INEQUALITY: spec += " > 0"; break;
                    default: UNREACHABLE();
                }

                spec += ";\n";
            }
            if (spec.empty())
                return std::nullopt;
            // restd::move '\n'
            spec.pop_back();
            return spec;
        }

        using AddrValueAndCondsPair =
            std::pair<symbolic::AddressBoxMap<utils::not_null<unique_ptr<symbolic::SymbolicExpr>>>,
                      std::vector<utils::not_null<unique_ptr<symbolic::SymbolicExpr>>>>;
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

                if (!lhs)
                    UNREACHABLE();

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
    } // namespace

    // [[deprecated("Loop condition shouldn't be multiple")]]
    // std::vector<InvsAndPostStates> buildLoopInvariant(Formulas loopCond,
    //                                              const std::vector<unique_ptr<Path>> &paths,
    //                                              const ProgramState &initState) {
    //     // TODO: process case that paths contain return.
    //     std::vector<InvsAndPostStates> invsAndPostStates;
    //     VarManager vm = VarManager::fromPaths(paths);

    //     std::vector<Formulas> processedCond = preprocessLoopCond(std::move(loopCond));

    //     const auto &initPaths = initState.getPaths();
    //     std::vector<Parma_Polyhedra_Library::C_Polyhedron *> initPathPolys;
    //     for (size_t i = 0; i < initPaths.size(); ++i) {
    //         if (initPaths[i])
    //             initPathPolys.push_back(buildPathPoly(*initPaths[i], vm, true));
    //         else
    //             UNREACHABLE();
    //     }

    //     std::vector<Parma_Polyhedra_Library::C_Polyhedron *> transPolys;
    //     for (const auto &path : paths) {
    //         if (path)
    //             transPolys.push_back(buildPathPoly(*path, vm));
    //         else
    //             UNREACHABLE();
    //     }

    //     std::vector<std::string> locations;

    //     locations.push_back("init");
    //     for (size_t i = 0; i < paths.size(); ++i) {
    //         locations.push_back("path_" + to_string(i));
    //     }
    //     locations.push_back("exit");

    //     int initIdx = 0;
    //     int exitIdx = paths.size() + 1;

    //     // TODO: fully disjunctive.
    //     auto identityPoly = buildIdentityPoly(vm); // shared across all paths
    //     for (size_t i = 0; i < processedCond.size(); ++i) {
    //         const Formulas &assertions = processedCond[i];
    //         auto *baseConditionPoly    = convertFormulaToPoly(assertions, vm);
    //         auto *primedConditionPoly  = primedPolyhedron(*baseConditionPoly, vm);

    //         auto negatedConds = negateFormulas(cloneFormulas(assertions));

    //         // === Precompute negated polyhedra
    //         std::vector<C_Polyhedron *> negatedPolys;
    //         for (const auto &neg : negatedConds) {
    //             auto *poly = convertFormulaToPoly(neg, vm);
    //             negatedPolys.push_back(poly);
    //         }
    //         for (size_t path_i = 0; path_i < initPathPolys.size(); ++path_i) {
    //             auto *initPoly = initPathPolys[path_i];
    //             std::vector<TransRel> transitions;

    //             // === init -> path_k ===
    //             for (size_t k = 0; k < paths.size(); ++k) {
    //                 auto *poly = new C_Polyhedron(identityPoly);
    //                 transitions.push_back(make_tuple(initIdx, static_cast<int>(k + 1),
    //                 poly));
    //             }

    //             // === path_j -> path_k ===
    //             for (size_t j = 0; j < paths.size(); ++j) {
    //                 for (size_t k = 0; k < paths.size(); ++k) {
    //                     auto *joined = new C_Polyhedron(*transPolys[j]);
    //                     joined->intersection_assign(*baseConditionPoly);
    //                     joined->intersection_assign(*primedConditionPoly);

    //                     if (!joined->is_empty()) {
    //                         transitions.push_back(make_tuple(static_cast<int>(j + 1),
    //                                                               static_cast<int>(k + 1),
    //                                                               joined));
    //                     } else {
    //                         delete joined;
    //                     }
    //                 }
    //             }

    //             // === path_j -> exit using precomputed negatedPolys
    //             for (size_t j = 0; j < paths.size(); ++j) {
    //                 for (auto *negPoly : negatedPolys) {
    //                     auto *exitPoly = primedPolyhedron(*negPoly, vm);
    //                     exitPoly->intersection_assign(*transPolys[j]);
    //                     exitPoly->intersection_assign(*baseConditionPoly);
    //                     if (!exitPoly->is_empty()) {
    //                         transitions.push_back(
    //                             make_tuple(static_cast<int>(j + 1), exitIdx, exitPoly));
    //                     } else {
    //                         delete exitPoly;
    //                     }
    //                 }
    //             }

    //             // === this initPoly as InitRel ===
    //             InitRel initRel = make_pair(initIdx, new C_Polyhedron(*initPoly));
    //             auto invs       = computeLinearInv(locations, transitions, initRel, vm);

    //             if (!invs.exitInvs_.empty() &&
    //                 (invs.pathsInvs_.size() <= path_i ||
    //                  invs.pathsInvs_[path_i].size() != invs.exitInvs_.size())) {
    //                 if (invs.pathsInvs_.size() > path_i) {
    //                     for (auto &inv : invs.pathsInvs_[path_i]) {
    //                         INFO("invariant");
    //                         dump(inv, vm);
    //                         INFO("\n");
    //                     }
    //                 }
    //                 for (auto &inv : invs.exitInvs_) {
    //                     INFO("post state");
    //                     dump(inv, vm);
    //                     INFO("\n");
    //                     INFO(buildPostPath(inv, *initPaths[path_i], vm)->dump());
    //                 }
    //                 ERROR("Wrong? or check computeLinearInv.\n"
    //                       "invariants's size: " +
    //                       to_string(invs.pathsInvs_.size()) +
    //                       (invs.pathsInvs_.size() <= path_i
    //                            ? " less or equal to path_i!\n"
    //                            : "\ninvariants[path_i]'s size: " +
    //                                  to_string(invs.pathsInvs_[path_i].size()) + "\n") +
    //                       "post state's size: " + to_string(invs.exitInvs_.size()) + "\n");
    //             }

    //             for (size_t k = 0; k < invs.exitInvs_.size(); k++) {
    //                 dump(invs.pathsInvs_[path_i][k], vm);
    //                 dump(invs.exitInvs_[k], vm);

    //                 auto loopInvs   = buildInvs(invs.pathsInvs_[path_i][k], vm);
    //                 auto postStates = buildPostPath(invs.exitInvs_[k], *initPaths[path_i], vm);

    //                 invsAndPostStates.emplace_back(std::move(loopInvs), std::move(postStates));
    //             }
    //             delete initRel.second;
    //         }

    //         delete baseConditionPoly;
    //         delete primedConditionPoly;
    //         for (auto *p : negatedPolys) {
    //             delete p;
    //         }
    //     }

    //     return invsAndPostStates;
    // }

    std::vector<InvsAndPostStates> buildLoopInvariant(
        std::unique_ptr<symbolic::SymbolicExpr> loopCond,
        const ProgramState &loopEntry,
        const ProgramState &loopCurrent) {
        std::vector<InvsAndPostStates> invsAndPostStates;
        auto &paths   = loopCurrent.getPaths();
        VarManager vm = VarManager::fromPaths(loopEntry.getPaths());

        auto &initPath    = loopEntry.getPaths().at(0);
        auto initPathPoly = buildPathPoly(*initPath, vm, true);

        std::vector<Parma_Polyhedra_Library::C_Polyhedron> transPolys;
        for (const auto &path : paths) {
            transPolys.push_back(buildPathPoly(*path, vm));
        }

        std::vector<std::string> locations;

        locations.push_back("init");
        for (size_t i = 0; i < paths.size(); ++i) {
            locations.push_back("path_" + to_string(i));
        }
        locations.push_back("exit");

        size_t initIdx = 0;
        size_t exitIdx = paths.size() + 1;

        auto identityPoly = buildIdentityPoly(vm); // shared across all paths

        Formulas assertions;
        assertions.push_back(std::move(loopCond)); // Yes, there is only one.
        auto baseConditionPoly = convertFormulaToPoly(assertions, vm);
        if (baseConditionPoly == std::nullopt)
            TODO(); // Condition contains values form Pointed-to Address.
        auto primedConditionPoly = primedPolyhedron(baseConditionPoly.value(), vm);

        auto negatedConds = negateFormulas(cloneFormulas(assertions));

        if (negatedConds.size() != 1)
            ERROR("negatedConds's size should be one or check `negateFormulas`.");

        // === Precompute negated polyhedra
        auto negatedCondPoly = convertFormulaToPoly(negatedConds.at(0), vm);
        if (negatedCondPoly == std::nullopt)
            TODO(); // Condition contains values form Pointed-to Address.

        std::vector<std::tuple<size_t, size_t, Parma_Polyhedra_Library::C_Polyhedron>> transitions;

        // === init -> path_k ===
        for (size_t k = 0; k < paths.size(); ++k) {
            auto poly = identityPoly;
            transitions.push_back(make_tuple(initIdx, k + 1, poly));
        }

        // === path_j -> path_k ===
        for (size_t j = 0; j < paths.size(); ++j) {
            for (size_t k = 0; k < paths.size(); ++k) {
                auto joined = transPolys[j];
                joined.intersection_assign(baseConditionPoly.value());
                joined.intersection_assign(primedConditionPoly);

                if (!joined.is_empty())
                    transitions.push_back(make_tuple(j + 1, k + 1, joined));
            }
        }

        // === path_j -> exit using precomputed negatedPolys
        for (size_t j = 0; j < paths.size(); ++j) {
            auto exitPoly = primedPolyhedron(negatedCondPoly.value(), vm);
            exitPoly.intersection_assign(transPolys[j]);
            exitPoly.intersection_assign(baseConditionPoly.value());
            if (!exitPoly.is_empty())
                transitions.push_back(make_tuple(j + 1, exitIdx, exitPoly));
        }

        // === this initPoly as InitRel ===
        auto initRel = make_pair(initIdx, initPathPoly);
        auto invs    = computeLinearInv(locations, transitions, initRel, vm);

        // for (size_t i = 0; i < invs.pathsInvs_.size(); ++i) {
        //     INFO("Path " + to_string(i));
        //     for (auto &inv : invs.pathsInvs_[i]) {
        //         INFO("invariant");
        //         dump(inv, vm);
        //         INFO("\n");
        //     }
        // }
        // for (auto &inv : invs.exitInvs_) {
        //     INFO("post state");
        //     dump(inv, vm);
        //     INFO("\n");
        // }

        // if (invs.pathsInvs_.size() != 1)
        //     TODO();
        // if (invs.pathsInvs_.at(0).size() != 1)
        //     TODO();

        auto loopInvs = buildInvs(invs.pathsInvs_.at(0).at(0), vm);

        // if (invs.exitInvs_.size() != 1)
        //     TODO();

        auto postStates = buildPostState(invs.exitInvs_.at(0), *initPath, vm);

        invsAndPostStates.emplace_back(std::move(loopInvs), std::move(postStates));

        return invsAndPostStates;
    }
} // namespace acslg::analyzer