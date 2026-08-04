#include "Analyzer/NumericalInvariant/invariantEmitter.h"
#include "testHelper.h"

#include <gtest/gtest.h>

namespace {
    namespace invariant = acslg::analyzer::invariant;
    namespace symbolic  = acslg::analyzer::symbolic;

    class InvariantEmitterTest : public acslg::test::utils::FixtureWithCode {
      protected:
        symbolic::Expr variable(unsigned id) {
            const auto declaration = getVarDecl(id);
            return symbolic::Expr::symbolValue(symbolic::deriveType(declaration->getType()),
                                               symbolic::Addr::variable(declaration), defaultPoint);
        }

        invariant::TransitionModel model() {
            const auto x    = variable(0);
            const auto n    = variable(1);
            auto &factory   = x.factory();
            const auto zero = symbolic::LiteralExpr{factory, 0};
            const auto one  = symbolic::LiteralExpr{factory, 1};
            return {{{x, zero}},
                    {n},
                    x.equalTo(zero),
                    x.lessThan(n),
                    {{symbolic::LiteralExpr{factory, true}, {x + one}}},
                    n.lessThan(zero).logicalOr(x.equalTo(n))};
        }
    };
} // namespace

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
TEST_F(InvariantEmitterTest, EmitsOnlyAfterCompleteProof) {
    const auto transition = model();
    const auto x          = transition.variables.front().current;
    const auto n          = transition.parameters.front();
    const auto candidate  = x.lessEqual(n).logicalOr(x.equalTo(transition.variables.front().entry));

    const auto result = invariant::verifyAndEmitLoopInvariant(transition, candidate, defaultPoint);

    ASSERT_EQ(result.status, invariant::InvariantEmissionStatus::Emitted) << result.reason;
    ASSERT_TRUE(result.acsl.has_value());
    EXPECT_EQ(result.verification.status, invariant::VerificationStatus::Proved);
    EXPECT_EQ(result.verification.solverQueries, 3U);
    EXPECT_EQ(result.acsl->find("loop invariant "), 0U);
    EXPECT_EQ(result.acsl->back(), ';');
    EXPECT_EQ(result.acsl->find("\\at("), std::string::npos);
    EXPECT_TRUE(result.usedPoints.empty());
}

TEST_F(InvariantEmitterTest, EmitsSelectableLabelAndRejectsInvalidLabel) {
    const auto transition = model();
    const auto x          = transition.variables.front().current;
    const auto n          = transition.parameters.front();
    const auto candidate  = x.lessEqual(n).logicalOr(x.equalTo(transition.variables.front().entry));

    const auto labeled = invariant::verifyAndEmitLoopInvariant(transition, candidate, defaultPoint,
                                                               {}, "acslg_test");
    ASSERT_EQ(labeled.status, invariant::InvariantEmissionStatus::Emitted) << labeled.reason;
    ASSERT_TRUE(labeled.acsl.has_value());
    EXPECT_EQ(labeled.acsl->find("loop invariant acslg_test: "), 0U);

    const auto invalid = invariant::verifyAndEmitLoopInvariant(transition, candidate, defaultPoint,
                                                               {}, "invalid-label");
    EXPECT_EQ(invalid.status, invariant::InvariantEmissionStatus::FormattingError);
    EXPECT_FALSE(invalid.acsl.has_value());
    EXPECT_EQ(invalid.verification.solverQueries, 0U);
}

TEST_F(InvariantEmitterTest, DoesNotEmitRefutedCandidate) {
    const auto transition = model();
    const auto x          = transition.variables.front().current;
    const auto n          = transition.parameters.front();

    const auto result =
        invariant::verifyAndEmitLoopInvariant(transition, x.lessEqual(n), defaultPoint);

    EXPECT_EQ(result.status, invariant::InvariantEmissionStatus::Refuted);
    EXPECT_FALSE(result.acsl.has_value());
    EXPECT_EQ(result.verification.failedObligation, invariant::ObligationKind::Initiation);
}

TEST_F(InvariantEmitterTest, DoesNotEmitOnBudgetExhaustionOrUnsupportedExpression) {
    const auto transition = model();
    const auto x          = transition.variables.front().current;
    auto &factory         = x.factory();
    const auto zero       = symbolic::LiteralExpr{factory, 0};
    const auto two        = symbolic::LiteralExpr{factory, 2};

    invariant::ClauseLimits noQueries;
    noQueries.maxSolverQueries = 0;
    const auto exhausted = invariant::verifyAndEmitLoopInvariant(transition, x.greaterEqual(zero),
                                                                 defaultPoint, noQueries);
    EXPECT_EQ(exhausted.status, invariant::InvariantEmissionStatus::BudgetExceeded);
    EXPECT_FALSE(exhausted.acsl.has_value());

    const auto unsupported = invariant::verifyAndEmitLoopInvariant(
        transition, (x / two).greaterEqual(zero), defaultPoint);
    EXPECT_EQ(unsupported.status, invariant::InvariantEmissionStatus::Unsupported);
    EXPECT_FALSE(unsupported.acsl.has_value());
}
#else
TEST_F(InvariantEmitterTest, DoesNotEmitWithoutSolver) {
    const auto transition = model();
    auto &factory         = transition.variables.front().current.factory();
    const auto result     = invariant::verifyAndEmitLoopInvariant(
        transition, symbolic::LiteralExpr{factory, true}, defaultPoint);

    EXPECT_EQ(result.status, invariant::InvariantEmissionStatus::SolverUnavailable);
    EXPECT_FALSE(result.acsl.has_value());
}
#endif
