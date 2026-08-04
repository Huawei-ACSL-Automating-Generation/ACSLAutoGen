#include "Analyzer/NumericalInvariant/polynomialRecurrence.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <utility>

namespace acslg::analyzer::invariant {
    namespace {
        using Exponents = std::vector<unsigned>;

        struct ExponentsLess {
            bool operator()(const Exponents &left, const Exponents &right) const {
                const auto leftDegree  = std::accumulate(left.begin(), left.end(), 0U);
                const auto rightDegree = std::accumulate(right.begin(), right.end(), 0U);
                if (leftDegree != rightDegree)
                    return leftDegree < rightDegree;
                return left < right;
            }
        };

        class SparsePolynomial {
          public:
            SparsePolynomial(std::size_t variableCount, unsigned maxDegree)
                : variableCount_(variableCount), maxDegree_(maxDegree) {}

            static SparsePolynomial constant(std::size_t variableCount,
                                             unsigned maxDegree,
                                             mpq_class value) {
                SparsePolynomial result{variableCount, maxDegree};
                if (value != 0)
                    result.terms_.emplace(Exponents(variableCount, 0), std::move(value));
                return result;
            }

            static SparsePolynomial variable(std::size_t variableCount,
                                             unsigned maxDegree,
                                             std::size_t index) {
                SparsePolynomial result{variableCount, maxDegree};
                Exponents exponents(variableCount, 0);
                exponents.at(index) = 1;
                result.terms_.emplace(std::move(exponents), 1);
                return result;
            }

            bool isZero() const { return terms_.empty(); }

            bool dependsOnAny(std::size_t first, std::size_t count) const {
                for (const auto &[exponents, coefficient] : terms_) {
                    (void)coefficient;
                    for (std::size_t index = first; index < first + count; ++index) {
                        if (exponents[index] != 0)
                            return true;
                    }
                }
                return false;
            }

            SparsePolynomial &addScaled(const SparsePolynomial &other, const mpq_class &scale) {
                for (const auto &[exponents, coefficient] : other.terms_) {
                    auto &slot = terms_[exponents];
                    slot += scale * coefficient;
                    if (slot == 0)
                        terms_.erase(exponents);
                }
                return *this;
            }

            std::optional<SparsePolynomial> multiplied(const SparsePolynomial &other) const {
                SparsePolynomial result{variableCount_, maxDegree_};
                for (const auto &[leftExponents, leftCoefficient] : terms_) {
                    for (const auto &[rightExponents, rightCoefficient] : other.terms_) {
                        Exponents exponents(variableCount_, 0);
                        unsigned degree = 0;
                        for (std::size_t index = 0; index < variableCount_; ++index) {
                            exponents[index] = leftExponents[index] + rightExponents[index];
                            degree += exponents[index];
                        }
                        if (degree > maxDegree_)
                            return std::nullopt;
                        result.terms_[std::move(exponents)] += leftCoefficient * rightCoefficient;
                    }
                }
                for (auto iterator = result.terms_.begin(); iterator != result.terms_.end();) {
                    if (iterator->second == 0)
                        iterator = result.terms_.erase(iterator);
                    else
                        ++iterator;
                }
                return result;
            }

            const std::map<Exponents, mpq_class, ExponentsLess> &terms() const { return terms_; }

          private:
            std::size_t variableCount_;
            unsigned maxDegree_;
            std::map<Exponents, mpq_class, ExponentsLess> terms_;
        };

        enum class ParseFailure {
            None,
            Unsupported,
            Degree
        };

        struct ParseResult {
            std::optional<SparsePolynomial> polynomial;
            ParseFailure failure = ParseFailure::None;
        };

        bool expressionMatches(const symbolic::Expr &left, const symbolic::Expr &right) {
            return left.structurallyEqual(right);
        }

        ParseResult parsePolynomial(const symbolic::Expr &expression,
                                    const std::vector<symbolic::Expr> &symbols,
                                    unsigned maxDegree) {
            for (std::size_t index = 0; index < symbols.size(); ++index) {
                if (expressionMatches(expression, symbols[index])) {
                    return {SparsePolynomial::variable(symbols.size(), maxDegree, index),
                            ParseFailure::None};
                }
            }

            if (auto literal = symbolic::LiteralExpr::tryFrom(expression)) {
                return {SparsePolynomial::constant(symbols.size(), maxDegree,
                                                   mpq_class(literal->value())),
                        ParseFailure::None};
            }

            if (auto unary = symbolic::UnaryExpr::tryFrom(expression)) {
                auto operand = parsePolynomial(unary->operand(), symbols, maxDegree);
                if (!operand.polynomial)
                    return operand;
                switch (unary->operation()) {
                    case symbolic::UnaryOp::Plus: return operand;
                    case symbolic::UnaryOp::Minus: {
                        auto result = SparsePolynomial::constant(symbols.size(), maxDegree, 0);
                        result.addScaled(*operand.polynomial, -1);
                        return {std::move(result), ParseFailure::None};
                    }
                    default: return {std::nullopt, ParseFailure::Unsupported};
                }
            }

            if (auto binary = symbolic::BinaryExpr::tryFrom(expression)) {
                auto left = parsePolynomial(binary->left(), symbols, maxDegree);
                if (!left.polynomial)
                    return left;
                auto right = parsePolynomial(binary->right(), symbols, maxDegree);
                if (!right.polynomial)
                    return right;

                switch (binary->operation()) {
                    case symbolic::BinaryOp::Add:
                        left.polynomial->addScaled(*right.polynomial, 1);
                        return left;
                    case symbolic::BinaryOp::Subtract:
                        left.polynomial->addScaled(*right.polynomial, -1);
                        return left;
                    case symbolic::BinaryOp::Multiply: {
                        auto product = left.polynomial->multiplied(*right.polynomial);
                        if (!product)
                            return {std::nullopt, ParseFailure::Degree};
                        return {std::move(product), ParseFailure::None};
                    }
                    default: return {std::nullopt, ParseFailure::Unsupported};
                }
            }

            return {std::nullopt, ParseFailure::Unsupported};
        }

        void generateMonomialsForDegree(std::size_t variableCount,
                                        std::size_t index,
                                        unsigned remaining,
                                        Exponents &current,
                                        std::vector<Exponents> &result) {
            if (index == variableCount) {
                if (remaining == 0)
                    result.push_back(current);
                return;
            }
            for (unsigned exponent = 0; exponent <= remaining; ++exponent) {
                current[index] = exponent;
                generateMonomialsForDegree(variableCount, index + 1, remaining - exponent, current,
                                           result);
            }
        }

        std::vector<Exponents> generateMonomials(std::size_t variableCount, unsigned maxDegree) {
            std::vector<Exponents> result;
            Exponents current(variableCount, 0);
            for (unsigned degree = 0; degree <= maxDegree; ++degree)
                generateMonomialsForDegree(variableCount, 0, degree, current, result);
            std::sort(result.begin(), result.end(), ExponentsLess{});
            return result;
        }

        std::optional<SparsePolynomial> substitute(const SparsePolynomial &source,
                                                   const std::vector<SparsePolynomial> &replacements,
                                                   unsigned maxDegree) {
            SparsePolynomial result{replacements.size(), maxDegree};
            for (const auto &[exponents, coefficient] : source.terms()) {
                auto term = SparsePolynomial::constant(replacements.size(), maxDegree, coefficient);
                for (std::size_t index = 0; index < exponents.size(); ++index) {
                    for (unsigned power = 0; power < exponents[index]; ++power) {
                        auto product = term.multiplied(replacements[index]);
                        if (!product)
                            return std::nullopt;
                        term = std::move(*product);
                    }
                }
                result.addScaled(term, 1);
            }
            return result;
        }

        std::vector<std::vector<mpq_class>> nullspace(std::vector<std::vector<mpq_class>> matrix) {
            if (matrix.empty())
                return {};
            const std::size_t columnCount = matrix.front().size();
            std::vector<std::size_t> pivotColumns;
            std::size_t pivotRow = 0;

            for (std::size_t column = 0; column < columnCount && pivotRow < matrix.size();
                 ++column) {
                auto selected = pivotRow;
                while (selected < matrix.size() && matrix[selected][column] == 0)
                    ++selected;
                if (selected == matrix.size())
                    continue;

                std::swap(matrix[pivotRow], matrix[selected]);
                const auto pivot = matrix[pivotRow][column];
                for (auto &value : matrix[pivotRow])
                    value /= pivot;

                for (std::size_t row = 0; row < matrix.size(); ++row) {
                    if (row == pivotRow || matrix[row][column] == 0)
                        continue;
                    const auto scale = matrix[row][column];
                    for (std::size_t currentColumn = 0; currentColumn < columnCount;
                         ++currentColumn) {
                        matrix[row][currentColumn] -= scale * matrix[pivotRow][currentColumn];
                    }
                }

                pivotColumns.push_back(column);
                ++pivotRow;
            }

            std::vector<bool> isPivot(columnCount, false);
            for (const auto column : pivotColumns)
                isPivot[column] = true;

            std::vector<std::vector<mpq_class>> basis;
            for (std::size_t freeColumn = 0; freeColumn < columnCount; ++freeColumn) {
                if (isPivot[freeColumn])
                    continue;
                std::vector<mpq_class> vector(columnCount, 0);
                vector[freeColumn] = 1;
                for (std::size_t row = 0; row < pivotColumns.size(); ++row)
                    vector[pivotColumns[row]] = -matrix[row][freeColumn];
                basis.push_back(std::move(vector));
            }
            return basis;
        }

        void normalizeVector(std::vector<mpq_class> &values) {
            mpz_class denominatorLcm = 1;
            for (const auto &value : values)
                mpz_lcm(denominatorLcm.get_mpz_t(), denominatorLcm.get_mpz_t(),
                        value.get_den().get_mpz_t());

            std::vector<mpz_class> integers;
            integers.reserve(values.size());
            for (const auto &value : values)
                integers.push_back(value.get_num() * (denominatorLcm / value.get_den()));

            mpz_class divisor = 0;
            for (const auto &value : integers) {
                const mpz_class magnitude = abs(value);
                mpz_gcd(divisor.get_mpz_t(), divisor.get_mpz_t(), magnitude.get_mpz_t());
            }
            if (divisor == 0)
                return;
            for (auto &value : integers)
                value /= divisor;

            const auto first = std::find_if(integers.begin(), integers.end(),
                                            [](const auto &value) { return value != 0; });
            if (first != integers.end() && *first < 0) {
                for (auto &value : integers)
                    value = -value;
            }
            for (std::size_t index = 0; index < values.size(); ++index)
                values[index] = mpq_class(integers[index]);
        }

        Polynomial toPublicPolynomial(const SparsePolynomial &polynomial) {
            std::vector<PolynomialTerm> terms;
            terms.reserve(polynomial.terms().size());
            for (const auto &[exponents, coefficient] : polynomial.terms())
                terms.push_back({exponents, coefficient});
            const auto variableCount = terms.empty() ? 0 : terms.front().exponents.size();
            return Polynomial{variableCount, std::move(terms)};
        }

        std::optional<symbolic::Expr> toExpression(const SparsePolynomial &polynomial,
                                                   const std::vector<symbolic::Expr> &symbols) {
            std::optional<symbolic::Expr> result;
            auto &factory = symbols.front().factory();
            for (const auto &[exponents, coefficient] : polynomial.terms()) {
                if (coefficient.get_den() != 1 || !coefficient.get_num().fits_slong_p())
                    return std::nullopt;
                auto term =
                    symbolic::Expr{symbolic::LiteralExpr{factory, coefficient.get_num().get_si()}};
                for (std::size_t index = 0; index < exponents.size(); ++index) {
                    for (unsigned power = 0; power < exponents[index]; ++power)
                        term = term * symbols[index];
                }
                result = result ? *result + term : term;
            }
            if (!result)
                result = symbolic::LiteralExpr{factory, 0};
            return result->simplified();
        }

        SparsePolynomial polynomialFromVector(const std::vector<mpq_class> &values,
                                              const std::vector<Exponents> &monomials,
                                              std::size_t variableCount,
                                              unsigned maxDegree) {
            SparsePolynomial result{variableCount, maxDegree};
            for (std::size_t index = 0; index < values.size(); ++index) {
                if (values[index] == 0)
                    continue;
                SparsePolynomial term{variableCount, maxDegree};
                auto coefficient =
                    SparsePolynomial::constant(variableCount, maxDegree, values[index]);
                auto monomial = SparsePolynomial::constant(variableCount, maxDegree, 1);
                for (std::size_t variable = 0; variable < variableCount; ++variable) {
                    auto factor = SparsePolynomial::variable(variableCount, maxDegree, variable);
                    for (unsigned power = 0; power < monomials[index][variable]; ++power)
                        monomial = *monomial.multiplied(factor);
                }
                term = *coefficient.multiplied(monomial);
                result.addScaled(term, 1);
            }
            return result;
        }

        std::string failureReason(ParseFailure failure) {
            switch (failure) {
                case ParseFailure::Degree:
                    return "polynomial substitution exceeds configured degree";
                case ParseFailure::Unsupported:
                    return "transition contains an unsupported expression";
                case ParseFailure::None: break;
            }
            return "unknown polynomial conversion failure";
        }
    } // namespace

    Polynomial::Polynomial(std::size_t variableCount, std::vector<PolynomialTerm> terms)
        : variableCount_(variableCount), terms_(std::move(terms)) {}

    mpq_class Polynomial::coefficient(std::span<const unsigned> exponents) const {
        for (const auto &term : terms_) {
            if (std::equal(term.exponents.begin(), term.exponents.end(), exponents.begin(),
                           exponents.end()))
                return term.coefficient;
        }
        return 0;
    }

    unsigned Polynomial::degree() const {
        unsigned result = 0;
        for (const auto &term : terms_) {
            result =
                std::max(result, std::accumulate(term.exponents.begin(), term.exponents.end(), 0U));
        }
        return result;
    }

    std::string Polynomial::dump() const {
        std::ostringstream output;
        bool first = true;
        for (const auto &term : terms_) {
            if (!first)
                output << ";";
            first = false;
            output << term.coefficient.get_str() << ":[";
            for (std::size_t index = 0; index < term.exponents.size(); ++index) {
                if (index != 0)
                    output << ",";
                output << term.exponents[index];
            }
            output << "]";
        }
        return output.str();
    }

    RecurrenceResult discoverPolynomialRecurrences(const TransitionModel &model,
                                                   const RecurrenceLimits &limits) {
        if (auto error = validateTransitionModel(model))
            return {RecurrenceStatus::InvalidModel, *error, {}};
        if (model.variables.size() > limits.maxStateVariables ||
            model.branches.size() > limits.maxBranches)
            return {RecurrenceStatus::LimitExceeded, "transition model exceeds size limits", {}};

        std::vector<symbolic::Expr> symbols;
        symbols.reserve(model.variables.size() * 2 + model.parameters.size());
        auto addUniqueSymbol = [&](const symbolic::Expr &expression) {
            const auto exists =
                std::any_of(symbols.begin(), symbols.end(), [&](const auto &symbol) {
                    return symbol.structurallyEqual(expression);
                });
            if (!exists)
                symbols.push_back(expression);
        };
        for (const auto &variable : model.variables)
            symbols.push_back(variable.current);
        for (const auto &variable : model.variables) {
            const auto isLiteral = symbolic::LiteralExpr::tryFrom(variable.entry).has_value();
            if (!isLiteral)
                addUniqueSymbol(variable.entry);
        }
        for (const auto &parameter : model.parameters)
            addUniqueSymbol(parameter);
        if (symbols.size() > limits.maxSymbols)
            return {RecurrenceStatus::LimitExceeded, "symbol basis exceeds size limit", {}};

        const auto monomials = generateMonomials(symbols.size(), limits.maxDegree);
        if (monomials.size() > limits.maxMonomials)
            return {RecurrenceStatus::LimitExceeded, "monomial basis exceeds size limit", {}};

        std::map<Exponents, std::size_t, ExponentsLess> monomialIndexes;
        for (std::size_t index = 0; index < monomials.size(); ++index)
            monomialIndexes.emplace(monomials[index], index);

        std::vector<std::vector<SparsePolynomial>> branchReplacements;
        std::set<mpq_class> eigenvalues{mpq_class(-1), mpq_class(0), mpq_class(1)};
        for (const auto &branch : model.branches) {
            std::vector<SparsePolynomial> replacements;
            replacements.reserve(symbols.size());
            for (const auto &next : branch.nextValues) {
                auto parsed = parsePolynomial(next, symbols, limits.maxDegree);
                if (!parsed.polynomial) {
                    const auto status = parsed.failure == ParseFailure::Degree
                                            ? RecurrenceStatus::LimitExceeded
                                            : RecurrenceStatus::UnsupportedExpression;
                    return {status, failureReason(parsed.failure), {}};
                }
                for (const auto &[exponents, coefficient] : parsed.polynomial->terms()) {
                    (void)exponents;
                    if (coefficient.get_den() == 1 && coefficient.get_num().fits_slong_p()) {
                        const auto value = coefficient.get_num().get_si();
                        if (value != 0 && std::abs(value) <= limits.maxEigenvalueMagnitude)
                            eigenvalues.emplace(value);
                    }
                }
                replacements.push_back(std::move(*parsed.polynomial));
            }
            for (std::size_t index = model.variables.size(); index < symbols.size(); ++index) {
                replacements.push_back(
                    SparsePolynomial::variable(symbols.size(), limits.maxDegree, index));
            }
            branchReplacements.push_back(std::move(replacements));
        }

        std::vector<SparsePolynomial> basis;
        basis.reserve(monomials.size());
        for (const auto &exponents : monomials) {
            auto polynomial = SparsePolynomial::constant(symbols.size(), limits.maxDegree, 1);
            for (std::size_t variable = 0; variable < symbols.size(); ++variable) {
                const auto factor =
                    SparsePolynomial::variable(symbols.size(), limits.maxDegree, variable);
                for (unsigned power = 0; power < exponents[variable]; ++power)
                    polynomial = *polynomial.multiplied(factor);
            }
            basis.push_back(std::move(polynomial));
        }

        std::vector<RecurrenceCandidate> candidates;
        std::set<std::pair<std::string, std::string>> seen;
        std::vector<bool> changedStateVariables(model.variables.size(), false);
        for (const auto &branch : model.branches) {
            for (std::size_t index = 0; index < model.variables.size(); ++index) {
                if (!branch.nextValues[index].structurallyEqual(model.variables[index].current))
                    changedStateVariables[index] = true;
            }
        }
        auto dependsOnChangedState = [&](const SparsePolynomial &polynomial) {
            for (const auto &[exponents, coefficient] : polynomial.terms()) {
                (void)coefficient;
                for (std::size_t index = 0; index < changedStateVariables.size(); ++index) {
                    if (changedStateVariables[index] && exponents[index] != 0)
                        return true;
                }
            }
            return false;
        };
        for (const auto &eigenvalue : eigenvalues) {
            std::vector<std::vector<mpq_class>> equations;
            for (const auto &replacements : branchReplacements) {
                std::vector<std::vector<mpq_class>> substitutionMatrix(
                    monomials.size(), std::vector<mpq_class>(monomials.size(), 0));
                for (std::size_t column = 0; column < basis.size(); ++column) {
                    auto replaced = substitute(basis[column], replacements, limits.maxDegree);
                    if (!replaced)
                        return {RecurrenceStatus::LimitExceeded,
                                "polynomial substitution exceeds configured degree",
                                {}};
                    for (const auto &[exponents, coefficient] : replaced->terms())
                        substitutionMatrix[monomialIndexes.at(exponents)][column] += coefficient;
                }
                for (std::size_t row = 0; row < monomials.size(); ++row) {
                    substitutionMatrix[row][row] -= eigenvalue;
                    equations.push_back(std::move(substitutionMatrix[row]));
                }
            }

            auto vectors         = nullspace(std::move(equations));
            const auto basisSize = vectors.size();
            for (std::size_t left = 0; left < basisSize; ++left) {
                for (std::size_t right = left + 1; right < basisSize; ++right) {
                    if (vectors.size() >= limits.maxCandidates)
                        break;
                    auto combined = vectors[left];
                    for (std::size_t index = 0; index < combined.size(); ++index)
                        combined[index] += vectors[right][index];
                    vectors.push_back(std::move(combined));
                }
                if (vectors.size() >= limits.maxCandidates)
                    break;
            }

            for (auto vector : vectors) {
                if (candidates.size() >= limits.maxCandidates)
                    break;
                normalizeVector(vector);
                auto polynomial =
                    polynomialFromVector(vector, monomials, symbols.size(), limits.maxDegree);
                if (!polynomial.dependsOnAny(0, model.variables.size()))
                    continue;
                if (limits.requireChangedStateVariable && !dependsOnChangedState(polynomial))
                    continue;

                std::vector<SparsePolynomial> entryReplacements;
                entryReplacements.reserve(symbols.size());
                for (const auto &variable : model.variables) {
                    auto entry = parsePolynomial(variable.entry, symbols, limits.maxDegree);
                    if (!entry.polynomial)
                        return {RecurrenceStatus::UnsupportedExpression,
                                "entry value is not polynomial",
                                {}};
                    entryReplacements.push_back(std::move(*entry.polynomial));
                }
                for (std::size_t index = model.variables.size(); index < symbols.size(); ++index) {
                    entryReplacements.push_back(
                        SparsePolynomial::variable(symbols.size(), limits.maxDegree, index));
                }
                auto entryPolynomial = substitute(polynomial, entryReplacements, limits.maxDegree);
                if (!entryPolynomial)
                    return {RecurrenceStatus::LimitExceeded,
                            "entry substitution exceeds configured degree",
                            {}};

                std::optional<symbolic::Expr> invariant;
                auto currentExpression = toExpression(polynomial, symbols);
                if (currentExpression && eigenvalue == 1) {
                    if (auto entryExpression = toExpression(*entryPolynomial, symbols))
                        invariant = currentExpression->equalTo(*entryExpression);
                } else if (currentExpression && entryPolynomial->isZero()) {
                    invariant = currentExpression->equalTo(
                        symbolic::LiteralExpr{symbols.front().factory(), 0});
                }

                auto publicPolynomial = toPublicPolynomial(polynomial);
                const auto key        = std::pair{eigenvalue.get_str(), publicPolynomial.dump()};
                if (seen.insert(key).second) {
                    candidates.push_back({std::move(publicPolynomial), eigenvalue, invariant});
                }
            }
        }

        std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
            if (left.eigenvalue != right.eigenvalue)
                return left.eigenvalue < right.eigenvalue;
            return left.polynomial.dump() < right.polynomial.dump();
        });
        return {RecurrenceStatus::Success, {}, std::move(candidates)};
    }
} // namespace acslg::analyzer::invariant
