#include "Analyzer/NumericalInvariant/clauseSynthesis.h"
#include "testHelper.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <utility>

namespace {
    namespace invariant = acslg::analyzer::invariant;
    namespace symbolic  = acslg::analyzer::symbolic;

    class SequenceProvider final : public invariant::ClauseProvider {
      public:
        explicit SequenceProvider(std::vector<invariant::ClauseProviderResult> results = {})
            : results_(std::move(results)) {}

        invariant::ClauseProviderResult generate(
            const invariant::ClauseProviderRequest &request) override {
            feedback.push_back(request.feedback);
            requestedLimits.push_back(request.maxAtomicClauses);
            if (next_ >= results_.size()) {
                return {
                    invariant::ClauseProviderStatus::Success, {}, "sequence provider exhausted"};
            }
            return results_[next_++];
        }

        std::vector<std::optional<invariant::ClauseFeedback>> feedback;
        std::vector<std::size_t> requestedLimits;

      private:
        std::vector<invariant::ClauseProviderResult> results_;
        std::size_t next_ = 0;
    };

    class ClauseSynthesisTest : public acslg::test::utils::FixtureWithCode {
      protected:
        symbolic::Expr variable(unsigned id) {
            const auto declaration = getVarDecl(id);
            const auto address     = symbolic::Addr::variable(declaration);
            return symbolic::Expr::symbolValue(symbolic::deriveType(declaration->getType()),
                                               address, defaultPoint);
        }

        invariant::TransitionModel simpleModel() {
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
TEST_F(ClauseSynthesisTest, FeedsFailedObligationBackAndFindsDisjunction) {
    auto model   = simpleModel();
    const auto x = model.variables[0].current;
    const auto n = model.parameters[0];
    SequenceProvider provider{
        {{invariant::ClauseProviderStatus::Success, {x.lessEqual(n)}, {}},
         {invariant::ClauseProviderStatus::Success, {x.equalTo(model.variables[0].entry)}, {}}}};

    const auto result = invariant::synthesizeInvariantClauses(model, provider);

    ASSERT_EQ(result.status, invariant::ClauseSynthesisStatus::Found) << result.reason;
    ASSERT_TRUE(result.invariant.has_value());
    EXPECT_EQ(result.rounds, 2U);
    EXPECT_EQ(result.providerCalls, 2U);
    ASSERT_EQ(provider.feedback.size(), 2U);
    EXPECT_FALSE(provider.feedback[0].has_value());
    ASSERT_TRUE(provider.feedback[1].has_value());
    EXPECT_EQ(provider.feedback[1]->failedObligation, invariant::ObligationKind::Initiation);
    EXPECT_TRUE(result.invariant->structurallyEqual(
        x.lessEqual(n).logicalOr(x.equalTo(model.variables[0].entry))));
}

TEST_F(ClauseSynthesisTest, UsesSuccessfulLocalClausesWithoutCallingProvider) {
    auto model   = simpleModel();
    const auto x = model.variables[0].current;
    const auto n = model.parameters[0];
    SequenceProvider provider{};

    const auto result = invariant::synthesizeInvariantClauses(
        model, provider, {x.lessEqual(n), x.equalTo(model.variables[0].entry)});

    EXPECT_EQ(result.status, invariant::ClauseSynthesisStatus::Found);
    EXPECT_EQ(result.providerCalls, 0U);
    EXPECT_TRUE(provider.feedback.empty());
}

TEST_F(ClauseSynthesisTest, EnforcesTotalSolverAndProviderBudgets) {
    auto model   = simpleModel();
    const auto x = model.variables[0].current;
    const auto n = model.parameters[0];
    SequenceProvider provider{
        {{invariant::ClauseProviderStatus::Success, {x.lessEqual(n)}, {}},
         {invariant::ClauseProviderStatus::Success, {x.greaterEqual(n)}, {}}}};
    invariant::ClauseSynthesisLimits limits;
    limits.clauseLimits.maxSolverQueries = 1;

    const auto result = invariant::synthesizeInvariantClauses(model, provider, {}, limits);

    EXPECT_EQ(result.status, invariant::ClauseSynthesisStatus::BudgetExceeded);
    EXPECT_EQ(result.solverQueries, 1U);
    EXPECT_EQ(result.providerCalls, 2U);
}

TEST_F(ClauseSynthesisTest, LiveDeepSeekCandidatesAreAcceptedOnlyAfterZ3Proof) {
    if (!std::getenv("ACSLG_RUN_LIVE_LLM_TESTS"))
        GTEST_SKIP() << "set ACSLG_RUN_LIVE_LLM_TESTS to enable the paid API synthesis test";

    std::string error;
    auto config = invariant::OpenAICompatibleProviderConfig::fromEnvironment(error);
    ASSERT_TRUE(config.has_value()) << error;
    config->timeoutMs = 60'000;
    invariant::CurlProcessTransport transport;
    invariant::OpenAICompatibleClauseProvider provider{transport, std::move(*config)};
    auto model = simpleModel();

    invariant::ClauseSynthesisLimits limits;
    limits.maxRounds        = 3;
    limits.maxProviderCalls = 3;
    const auto result       = invariant::synthesizeInvariantClauses(model, provider, {}, limits);

    ASSERT_EQ(result.status, invariant::ClauseSynthesisStatus::Found) << result.reason;
    ASSERT_TRUE(result.invariant.has_value());
    const auto independentlyVerified =
        invariant::verifyInvariant(model, *result.invariant, limits.clauseLimits);
    EXPECT_EQ(independentlyVerified.status, invariant::VerificationStatus::Proved)
        << independentlyVerified.reason;
}
#endif

TEST_F(ClauseSynthesisTest, StopsWhenProviderMakesNoProgress) {
    auto model   = simpleModel();
    const auto x = model.variables[0].current;
    const auto n = model.parameters[0];
    SequenceProvider provider{{{invariant::ClauseProviderStatus::Success, {x.lessEqual(n)}, {}},
                               {invariant::ClauseProviderStatus::Success, {x.lessEqual(n)}, {}}}};

    invariant::ClauseSynthesisLimits limits;
#if !ACSLG_NUMERICAL_INVARIANT_HAS_Z3
    limits.clauseLimits.maxSolverQueries = 1;
#endif
    const auto result = invariant::synthesizeInvariantClauses(model, provider, {}, limits);

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
    EXPECT_EQ(result.status, invariant::ClauseSynthesisStatus::NoInvariant);
    EXPECT_EQ(result.providerCalls, 2U);
#else
    EXPECT_EQ(result.status, invariant::ClauseSynthesisStatus::SolverUnavailable);
#endif
}

TEST_F(ClauseSynthesisTest, PropagatesProviderFailureWithoutAcceptingClauses) {
    auto model = simpleModel();
    SequenceProvider provider{
        {{invariant::ClauseProviderStatus::InvalidResponse, {}, "malformed provider output"}}};

    const auto result = invariant::synthesizeInvariantClauses(model, provider);

    EXPECT_EQ(result.status, invariant::ClauseSynthesisStatus::ProviderError);
    EXPECT_FALSE(result.invariant.has_value());
    EXPECT_EQ(result.providerCalls, 1U);
    EXPECT_EQ(result.solverQueries, 0U);
}
