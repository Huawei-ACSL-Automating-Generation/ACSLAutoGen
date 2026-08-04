#include "Analyzer/NumericalInvariant/clauseProvider.h"
#include "testHelper.h"

#include <gtest/gtest.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <cstdlib>

namespace {
    namespace invariant = acslg::analyzer::invariant;
    namespace symbolic  = acslg::analyzer::symbolic;

    class FakeTransport final : public invariant::HttpTransport {
      public:
        invariant::HttpPostResponse response{true, 200, {}, {}};
        std::optional<invariant::HttpPostRequest> request;

        invariant::HttpPostResponse post(const invariant::HttpPostRequest &newRequest) override {
            request = newRequest;
            return response;
        }
    };

    class ClauseProviderTest : public acslg::test::utils::FixtureWithCode {
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

        invariant::OpenAICompatibleProviderConfig config() {
            invariant::OpenAICompatibleProviderConfig result;
            result.apiKey = "test-key";
            result.model  = "test-model";
            return result;
        }
    };

    std::string envelope(std::string content, std::string finishReason = "stop") {
        llvm::json::Object message{{"content", std::move(content)}};
        llvm::json::Array choices;
        choices.push_back(llvm::json::Object{{"finish_reason", std::move(finishReason)},
                                             {"message", std::move(message)}});
        std::string result;
        llvm::raw_string_ostream stream(result);
        stream << llvm::json::Value(llvm::json::Object{{"choices", std::move(choices)}});
        return result;
    }
} // namespace

TEST_F(ClauseProviderTest, ParsesStrictAtomicClauseAstUsingPublicExpressions) {
    auto model        = simpleModel();
    const auto result = invariant::parseAtomicClauseJson(model,
                                                         R"({"clauses":[
             {"op":"le","lhs":{"symbol":"v0"},"rhs":{"symbol":"p0"}},
             {"op":"eq","lhs":{"symbol":"v0"},"rhs":{"integer":0}}
           ]})");

    ASSERT_EQ(result.status, invariant::ClauseProviderStatus::Success) << result.reason;
    ASSERT_EQ(result.clauses.size(), 2U);
    EXPECT_TRUE(result.clauses[0].structurallyEqual(
        model.variables[0].current.lessEqual(model.parameters[0])));
    EXPECT_TRUE(result.clauses[1].structurallyEqual(model.variables[0].current.equalTo(
        symbolic::LiteralExpr{model.variables[0].current.factory(), std::int64_t{0}})));
}

TEST_F(ClauseProviderTest, RejectsLogicalUnknownAndOverBudgetResponses) {
    auto model = simpleModel();

    const auto logical = invariant::parseAtomicClauseJson(
        model, R"({"clauses":[{"op":"and","lhs":{"symbol":"v0"},"rhs":{"symbol":"p0"}}]})");
    EXPECT_EQ(logical.status, invariant::ClauseProviderStatus::InvalidResponse);

    const auto unknown = invariant::parseAtomicClauseJson(
        model, R"({"clauses":[{"op":"eq","lhs":{"symbol":"v9"},"rhs":{"integer":0}}]})");
    EXPECT_EQ(unknown.status, invariant::ClauseProviderStatus::InvalidResponse);

    invariant::ClauseJsonLimits limits;
    limits.maxClauses     = 1;
    const auto overBudget = invariant::parseAtomicClauseJson(model,
                                                             R"({"clauses":[
             {"op":"eq","lhs":{"symbol":"v0"},"rhs":{"integer":0}},
             {"op":"le","lhs":{"symbol":"v0"},"rhs":{"symbol":"p0"}}
           ]})",
                                                             limits);
    EXPECT_EQ(overBudget.status, invariant::ClauseProviderStatus::BudgetExceeded);
}

TEST_F(ClauseProviderTest, BuildsOpenAICompatibleRequestAndParsesContent) {
    auto model = simpleModel();
    FakeTransport transport;
    transport.response.body =
        envelope(R"({"clauses":[{"op":"le","lhs":{"symbol":"v0"},"rhs":{"symbol":"p0"}}]})");
    invariant::OpenAICompatibleClauseProvider provider{transport, config()};

    const auto result = provider.generate({model, std::nullopt, 4});

    ASSERT_EQ(result.status, invariant::ClauseProviderStatus::Success) << result.reason;
    ASSERT_EQ(result.clauses.size(), 1U);
    ASSERT_TRUE(transport.request.has_value());
    EXPECT_EQ(transport.request->url, "https://api.deepseek.com/chat/completions");
    EXPECT_NE(transport.request->body.find("\"response_format\""), std::string::npos);
    EXPECT_NE(transport.request->body.find("v0"), std::string::npos);
    ASSERT_EQ(transport.request->headers.size(), 2U);
    EXPECT_EQ(transport.request->headers[1].second, "Bearer test-key");
}

TEST_F(ClauseProviderTest, IncludesObligationFeedbackWithoutCounterexampleDisclosure) {
    auto model = simpleModel();
    FakeTransport transport;
    transport.response.body = envelope(R"({"clauses":[]})");
    invariant::OpenAICompatibleClauseProvider provider{transport, config()};

    invariant::ClauseFeedback feedback{invariant::ObligationKind::Consecution, 0,
                                       invariant::Counterexample{}};
    const auto result = provider.generate({model, feedback, 4});

    ASSERT_EQ(result.status, invariant::ClauseProviderStatus::Success) << result.reason;
    ASSERT_TRUE(transport.request.has_value());
    EXPECT_NE(transport.request->body.find("consecution"), std::string::npos);
    EXPECT_NE(transport.request->body.find("branch 0"), std::string::npos);
    EXPECT_EQ(transport.request->body.find("bindings"), std::string::npos);
}

TEST_F(ClauseProviderTest, RejectsBadConfigurationAndMalformedEnvelope) {
    auto model = simpleModel();
    FakeTransport transport;
    auto badConfig    = config();
    badConfig.baseUrl = "http://insecure.example";
    invariant::OpenAICompatibleClauseProvider insecure{transport, badConfig};
    EXPECT_EQ(insecure.generate({model, std::nullopt, 16}).status,
              invariant::ClauseProviderStatus::ConfigurationError);

    transport.response.body = R"({"choices":[]})";
    invariant::OpenAICompatibleClauseProvider malformed{transport, config()};
    EXPECT_EQ(malformed.generate({model, std::nullopt, 16}).status,
              invariant::ClauseProviderStatus::InvalidResponse);
}

TEST_F(ClauseProviderTest, LiveDeepSeekReturnsParseableAtomicClauses) {
    if (!std::getenv("ACSLG_RUN_LIVE_LLM_TESTS"))
        GTEST_SKIP() << "set ACSLG_RUN_LIVE_LLM_TESTS to enable the paid API smoke test";

    std::string error;
    auto liveConfig = invariant::OpenAICompatibleProviderConfig::fromEnvironment(error);
    ASSERT_TRUE(liveConfig.has_value()) << error;
    liveConfig->timeoutMs = 60'000;
    invariant::CurlProcessTransport transport;
    invariant::OpenAICompatibleClauseProvider provider{transport, std::move(*liveConfig)};
    auto model = simpleModel();

    const auto result = provider.generate({model, std::nullopt, 6});

    ASSERT_EQ(result.status, invariant::ClauseProviderStatus::Success) << result.reason;
    EXPECT_FALSE(result.clauses.empty());
}
