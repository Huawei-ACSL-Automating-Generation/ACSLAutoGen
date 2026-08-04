#include "Analyzer/NumericalInvariant/polynomialRecurrence.h"
#include "testHelper.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

namespace {
    namespace invariant = acslg::analyzer::invariant;
    namespace symbolic  = acslg::analyzer::symbolic;

    symbolic::Expr integer(symbolic::ExprFactory &factory, int value) {
        return symbolic::LiteralExpr{factory, value};
    }

    symbolic::Expr boolean(symbolic::ExprFactory &factory, bool value) {
        return symbolic::LiteralExpr{factory, value};
    }

    invariant::TransitionModel makeModel(std::vector<invariant::TransitionVariable> variables,
                                         std::vector<invariant::TransitionBranch> branches,
                                         symbolic::ExprFactory &factory) {
        return {std::move(variables), {},          boolean(factory, true), boolean(factory, true),
                std::move(branches),  std::nullopt};
    }

    bool hasStateDependentQuadratic(const invariant::RecurrenceResult &result,
                                    const mpq_class &eigenvalue,
                                    std::size_t stateVariable) {
        return std::any_of(
            result.candidates.begin(), result.candidates.end(), [&](const auto &candidate) {
                if (candidate.eigenvalue != eigenvalue || candidate.polynomial.degree() != 2)
                    return false;
                return std::any_of(
                    candidate.polynomial.terms().begin(), candidate.polynomial.terms().end(),
                    [&](const auto &term) { return term.exponents[stateVariable] != 0; });
            });
    }

    class PolynomialRecurrencePrototypeTest : public acslg::test::utils::FixtureWithCode {
      protected:
        symbolic::Expr variable(unsigned id) {
            const auto declaration = getVarDecl(id);
            const auto address     = symbolic::Addr::variable(declaration);
            return symbolic::Expr::symbolValue(symbolic::deriveType(declaration->getType()),
                                               address, defaultPoint);
        }
    };
} // namespace

TEST_F(PolynomialRecurrencePrototypeTest, FindsQuadraticInvariantForTriangularLoop) {
    const auto x  = variable(0);
    const auto y  = variable(1);
    const auto i  = variable(2);
    const auto x0 = variable(3);
    const auto y0 = variable(4);
    auto &factory = x.factory();

    auto model = makeModel(
        {{x, x0}, {y, y0}, {i, integer(factory, 0)}},
        {{boolean(factory, true), {x + y, y + integer(factory, 1), i + integer(factory, 1)}}},
        factory);

    const auto result = invariant::discoverPolynomialRecurrences(model);
    ASSERT_EQ(result.status, invariant::RecurrenceStatus::Success) << result.reason;
    EXPECT_TRUE(hasStateDependentQuadratic(result, 1, 0));
    EXPECT_TRUE(
        std::any_of(result.candidates.begin(), result.candidates.end(), [](const auto &candidate) {
            return candidate.eigenvalue == 1 && candidate.polynomial.degree() == 2 &&
                   candidate.invariant.has_value();
        }));
}

TEST_F(PolynomialRecurrencePrototypeTest, FindsCommonExpressionRecurrenceAcrossBranches) {
    const auto x     = variable(0);
    const auto y     = variable(1);
    auto &factory    = x.factory();
    const auto two   = integer(factory, 2);
    const auto three = integer(factory, 3);
    const auto four  = integer(factory, 4);

    auto model = makeModel({{x, integer(factory, 0)}, {y, integer(factory, -1)}},
                           {{boolean(factory, true), {two * x, four * y + three}},
                            {boolean(factory, true), {-two * x, four * y + three}}},
                           factory);

    const auto result = invariant::discoverPolynomialRecurrences(model);
    ASSERT_EQ(result.status, invariant::RecurrenceStatus::Success) << result.reason;

    const std::array<unsigned, 2> xSquared{2, 0};
    const std::array<unsigned, 2> yLinear{0, 1};
    const std::array<unsigned, 2> constant{0, 0};
    const auto candidate =
        std::find_if(result.candidates.begin(), result.candidates.end(), [&](const auto &current) {
            if (current.eigenvalue != 4)
                return false;
            const auto xCoefficient = current.polynomial.coefficient(xSquared);
            if (xCoefficient == 0)
                return false;
            return current.polynomial.coefficient(yLinear) == xCoefficient &&
                   current.polynomial.coefficient(constant) == xCoefficient;
        });

    std::string discovered;
    for (const auto &current : result.candidates)
        discovered += current.eigenvalue.get_str() + " => " + current.polynomial.dump() + "\n";
    ASSERT_NE(candidate, result.candidates.end()) << discovered;
    EXPECT_TRUE(candidate->invariant.has_value());
}

TEST_F(PolynomialRecurrencePrototypeTest, RejectsBranchSpecificRecurrences) {
    const auto x  = variable(0);
    auto &factory = x.factory();

    auto model = makeModel({{x, integer(factory, 1)}},
                           {{boolean(factory, true), {integer(factory, 2) * x}},
                            {boolean(factory, true), {integer(factory, 3) * x}}},
                           factory);

    const auto result = invariant::discoverPolynomialRecurrences(model);
    ASSERT_EQ(result.status, invariant::RecurrenceStatus::Success) << result.reason;
    EXPECT_TRUE(result.candidates.empty());
}

TEST_F(PolynomialRecurrencePrototypeTest, RejectsUnsupportedAndOverDegreeUpdates) {
    const auto x  = variable(0);
    auto &factory = x.factory();

    auto division = makeModel({{x, integer(factory, 0)}},
                              {{boolean(factory, true), {x / integer(factory, 2)}}}, factory);
    EXPECT_EQ(invariant::discoverPolynomialRecurrences(division).status,
              invariant::RecurrenceStatus::UnsupportedExpression);

    auto cubic =
        makeModel({{x, integer(factory, 0)}}, {{boolean(factory, true), {x * x * x}}}, factory);
    EXPECT_EQ(invariant::discoverPolynomialRecurrences(cubic).status,
              invariant::RecurrenceStatus::LimitExceeded);
}

TEST_F(PolynomialRecurrencePrototypeTest, ProducesDeterministicNormalizedResults) {
    const auto x  = variable(0);
    auto &factory = x.factory();
    auto model    = makeModel({{x, integer(factory, 0)}},
                              {{boolean(factory, true), {x + integer(factory, 1)}}}, factory);

    const auto first  = invariant::discoverPolynomialRecurrences(model);
    const auto second = invariant::discoverPolynomialRecurrences(model);
    ASSERT_EQ(first.status, invariant::RecurrenceStatus::Success);
    ASSERT_EQ(second.status, invariant::RecurrenceStatus::Success);
    ASSERT_EQ(first.candidates.size(), second.candidates.size());
    for (std::size_t index = 0; index < first.candidates.size(); ++index) {
        EXPECT_EQ(first.candidates[index].eigenvalue, second.candidates[index].eigenvalue);
        EXPECT_EQ(first.candidates[index].polynomial.dump(),
                  second.candidates[index].polynomial.dump());
    }
}

TEST_F(PolynomialRecurrencePrototypeTest, CanExcludeUnchangedOnlyCandidates) {
    const auto x  = variable(0);
    const auto y  = variable(1);
    auto &factory = x.factory();
    auto model    = makeModel({{x, integer(factory, 0)}, {y, integer(factory, 5)}},
                              {{boolean(factory, true), {x + integer(factory, 1), y}}}, factory);

    const auto unfiltered = invariant::discoverPolynomialRecurrences(model);
    ASSERT_EQ(unfiltered.status, invariant::RecurrenceStatus::Success) << unfiltered.reason;
    ASSERT_FALSE(unfiltered.candidates.empty());

    invariant::RecurrenceLimits limits;
    limits.requireChangedStateVariable = true;
    const auto result                  = invariant::discoverPolynomialRecurrences(model, limits);

    ASSERT_EQ(result.status, invariant::RecurrenceStatus::Success) << result.reason;
    EXPECT_TRUE(result.candidates.empty());
}
