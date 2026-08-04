#ifndef ACSLG_ANALYZER_NUMERICAL_INVARIANT_CLAUSE_PROVIDER_H
#define ACSLG_ANALYZER_NUMERICAL_INVARIANT_CLAUSE_PROVIDER_H

#include "Analyzer/NumericalInvariant/clauseCombiner.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace acslg::analyzer::invariant {

    enum class ClauseProviderStatus {
        Success,
        InvalidModel,
        UnsupportedModel,
        InvalidResponse,
        TransportError,
        ConfigurationError,
        BudgetExceeded
    };

    struct ClauseFeedback {
        ObligationKind failedObligation;
        std::optional<std::size_t> failedBranch;
        std::optional<Counterexample> counterexample;
    };

    struct ClauseProviderRequest {
        const TransitionModel &model;
        std::optional<ClauseFeedback> feedback;
        std::size_t maxAtomicClauses = 16;
    };

    struct ClauseProviderResult {
        ClauseProviderStatus status;
        std::vector<symbolic::Expr> clauses;
        std::string reason;
    };

    class ClauseProvider {
      public:
        virtual ~ClauseProvider() = default;

        virtual ClauseProviderResult generate(const ClauseProviderRequest &request) = 0;
    };

    struct ClauseJsonLimits {
        std::size_t maxClauses = 16;
        std::size_t maxNodes   = 128;
        std::size_t maxDepth   = 12;
    };

    ClauseProviderResult parseAtomicClauseJson(const TransitionModel &model,
                                               std::string_view json,
                                               const ClauseJsonLimits &limits = {});

    struct HttpPostRequest {
        std::string url;
        std::vector<std::pair<std::string, std::string>> headers;
        std::string body;
        unsigned timeoutMs           = 30'000;
        std::size_t maxResponseBytes = 1U << 20U;
    };

    struct HttpPostResponse {
        bool transportSucceeded = false;
        unsigned statusCode     = 0;
        std::string body;
        std::string error;
    };

    class HttpTransport {
      public:
        virtual ~HttpTransport() = default;

        virtual HttpPostResponse post(const HttpPostRequest &request) = 0;
    };

    class CurlProcessTransport final : public HttpTransport {
      public:
        HttpPostResponse post(const HttpPostRequest &request) override;
    };

    struct OpenAICompatibleProviderConfig {
        std::string baseUrl = "https://api.deepseek.com";
        std::string apiKey;
        std::string model                   = "deepseek-v4-flash";
        unsigned timeoutMs                  = 30'000;
        std::size_t maxResponseBytes        = 1U << 20U;
        std::uint32_t maxTokens             = 1'024;
        std::optional<bool> thinkingEnabled = false;

        static std::optional<OpenAICompatibleProviderConfig> fromEnvironment(std::string &error);
    };

    class OpenAICompatibleClauseProvider final : public ClauseProvider {
      public:
        OpenAICompatibleClauseProvider(HttpTransport &transport,
                                       OpenAICompatibleProviderConfig config);

        ClauseProviderResult generate(const ClauseProviderRequest &request) override;

      private:
        HttpTransport &transport_;
        OpenAICompatibleProviderConfig config_;
    };

} // namespace acslg::analyzer::invariant

#endif
