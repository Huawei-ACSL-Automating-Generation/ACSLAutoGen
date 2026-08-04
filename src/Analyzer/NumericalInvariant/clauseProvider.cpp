#include "Analyzer/NumericalInvariant/clauseProvider.h"

#include <llvm/Support/JSON.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <fcntl.h>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace acslg::analyzer::invariant {
    namespace {
        constexpr std::string_view SYSTEM_PROMPT =
            "You generate candidate atomic numerical loop-invariant clauses. "
            "Return one JSON object with a clauses array. Each clause must have exactly one "
            "comparison at its root and must not contain logical and/or/not. Never claim that a "
            "clause is proved; a separate SMT solver will verify every candidate.";

        bool containsLineBreak(std::string_view value) {
            return value.find('\r') != std::string_view::npos ||
                   value.find('\n') != std::string_view::npos;
        }

        std::string jsonText(llvm::json::Value value) {
            std::string result;
            llvm::raw_string_ostream stream(result);
            stream << value;
            return result;
        }

        bool hasExactKeys(const llvm::json::Object &object,
                          std::initializer_list<std::string_view> expected) {
            if (object.size() != expected.size())
                return false;
            return std::all_of(object.begin(), object.end(), [&](const auto &member) {
                const llvm::StringRef key = member.first;
                return std::find(expected.begin(), expected.end(),
                                 std::string_view{key.data(), key.size()}) != expected.end();
            });
        }

        class ClauseJsonParser {
          public:
            ClauseJsonParser(const TransitionModel &model, const ClauseJsonLimits &limits)
                : model_(model), limits_(limits) {}

            ClauseProviderResult parse(std::string_view input) {
                if (auto modelError = validateTransitionModel(model_))
                    return failure(ClauseProviderStatus::InvalidModel, *modelError);

                auto parsed = llvm::json::parse(input);
                if (!parsed)
                    return failure(ClauseProviderStatus::InvalidResponse,
                                   "provider response is not valid JSON");
                const auto *root = parsed->getAsObject();
                if (!root || !hasExactKeys(*root, {"clauses"}))
                    return failure(ClauseProviderStatus::InvalidResponse,
                                   "response must be an object containing only 'clauses'");
                const auto *clauses = root->getArray("clauses");
                if (!clauses)
                    return failure(ClauseProviderStatus::InvalidResponse,
                                   "'clauses' must be an array");
                if (clauses->size() > limits_.maxClauses)
                    return failure(ClauseProviderStatus::BudgetExceeded,
                                   "provider returned too many atomic clauses");

                std::vector<symbolic::Expr> result;
                result.reserve(clauses->size());
                for (const auto &clauseValue : *clauses) {
                    const auto *clause = clauseValue.getAsObject();
                    if (!clause)
                        return failure(ClauseProviderStatus::InvalidResponse,
                                       "each clause must be a JSON object");
                    auto expression = parseComparison(*clause, 1);
                    if (!expression)
                        return failure(status_, reason_);
                    if (std::none_of(result.begin(), result.end(), [&](const auto &existing) {
                            return existing.structurallyEqual(*expression);
                        }))
                        result.push_back(*expression);
                }
                return {ClauseProviderStatus::Success, std::move(result), {}};
            }

          private:
            ClauseProviderResult failure(ClauseProviderStatus status, std::string reason) {
                return {status, {}, std::move(reason)};
            }

            bool consumeNode(std::size_t depth) {
                if (depth > limits_.maxDepth) {
                    status_ = ClauseProviderStatus::BudgetExceeded;
                    reason_ = "clause AST depth limit exceeded";
                    return false;
                }
                if (++nodes_ > limits_.maxNodes) {
                    status_ = ClauseProviderStatus::BudgetExceeded;
                    reason_ = "clause AST node limit exceeded";
                    return false;
                }
                return true;
            }

            std::optional<symbolic::Expr> resolveSymbol(std::string_view name) {
                std::size_t index = 0;
                if (parseIndexedName(name, 'v', index) && index < model_.variables.size())
                    return model_.variables[index].current;
                if (parseIndexedName(name, 'p', index) && index < model_.parameters.size())
                    return model_.parameters[index];
                if (parseIndexedName(name, 'e', index) && index < model_.variables.size()) {
                    const auto &entry = model_.variables[index].entry;
                    if (entry.isSymbolValue() || entry.isRangeIndex())
                        return entry;
                }
                status_ = ClauseProviderStatus::InvalidResponse;
                reason_ = "clause references unknown symbol '" + std::string(name) + "'";
                return std::nullopt;
            }

            bool parseIndexedName(std::string_view name, char prefix, std::size_t &index) {
                if (name.size() < 2 || name.front() != prefix)
                    return false;
                index             = 0;
                const auto *begin = name.data() + 1;
                const auto *end   = name.data() + name.size();
                const auto parsed = std::from_chars(begin, end, index);
                return parsed.ec == std::errc{} && parsed.ptr == end;
            }

            bool isArithmetic(const symbolic::Expr &expression) {
                return expression.getValType().kind != symbolic::ExprScalarKind::Bool;
            }

            std::optional<symbolic::Expr> parseComparison(const llvm::json::Object &object,
                                                          std::size_t depth) {
                if (!consumeNode(depth))
                    return std::nullopt;
                if (!hasExactKeys(object, {"op", "lhs", "rhs"}))
                    return invalid("comparison must contain exactly 'op', 'lhs', and 'rhs'");
                const auto operation  = object.getString("op");
                const auto *leftJson  = object.getObject("lhs");
                const auto *rightJson = object.getObject("rhs");
                if (!operation || !leftJson || !rightJson)
                    return invalid("comparison fields have invalid types");

                auto left  = parseArithmetic(*leftJson, depth + 1);
                auto right = parseArithmetic(*rightJson, depth + 1);
                if (!left || !right)
                    return std::nullopt;

                if (*operation == "eq")
                    return left->equalTo(*right);
                if (*operation == "ne")
                    return left->notEqualTo(*right);
                if (*operation == "lt")
                    return left->lessThan(*right);
                if (*operation == "le")
                    return left->lessEqual(*right);
                if (*operation == "gt")
                    return left->greaterThan(*right);
                if (*operation == "ge")
                    return left->greaterEqual(*right);
                return invalid("atomic clause root must be one of eq, ne, lt, le, gt, ge");
            }

            std::optional<symbolic::Expr> parseArithmetic(const llvm::json::Object &object,
                                                          std::size_t depth) {
                if (!consumeNode(depth))
                    return std::nullopt;

                if (hasExactKeys(object, {"symbol"})) {
                    const auto name = object.getString("symbol");
                    if (!name)
                        return invalid("'symbol' must be a string");
                    auto expression = resolveSymbol(*name);
                    if (expression && !isArithmetic(*expression))
                        return invalid("boolean symbols are not supported in numerical clauses");
                    return expression;
                }
                if (hasExactKeys(object, {"integer"})) {
                    const auto value = object.getInteger("integer");
                    if (!value)
                        return invalid("'integer' must fit in a signed 64-bit integer");
                    return symbolic::LiteralExpr{model_.variables.front().current.factory(),
                                                 *value};
                }
                if (hasExactKeys(object, {"op", "operand"})) {
                    const auto operation    = object.getString("op");
                    const auto *operandJson = object.getObject("operand");
                    if (!operation || !operandJson || *operation != "neg")
                        return invalid("unary arithmetic node must use operation 'neg'");
                    auto operand = parseArithmetic(*operandJson, depth + 1);
                    if (!operand)
                        return std::nullopt;
                    return -*operand;
                }
                if (!hasExactKeys(object, {"op", "lhs", "rhs"}))
                    return invalid("invalid arithmetic AST node");

                const auto operation  = object.getString("op");
                const auto *leftJson  = object.getObject("lhs");
                const auto *rightJson = object.getObject("rhs");
                if (!operation || !leftJson || !rightJson)
                    return invalid("arithmetic fields have invalid types");
                auto left  = parseArithmetic(*leftJson, depth + 1);
                auto right = parseArithmetic(*rightJson, depth + 1);
                if (!left || !right)
                    return std::nullopt;
                if (*operation == "add")
                    return *left + *right;
                if (*operation == "sub")
                    return *left - *right;
                if (*operation == "mul")
                    return *left * *right;
                return invalid("arithmetic operation must be one of add, sub, mul");
            }

            std::optional<symbolic::Expr> invalid(std::string reason) {
                status_ = ClauseProviderStatus::InvalidResponse;
                reason_ = std::move(reason);
                return std::nullopt;
            }

            const TransitionModel &model_;
            const ClauseJsonLimits &limits_;
            std::size_t nodes_           = 0;
            ClauseProviderStatus status_ = ClauseProviderStatus::InvalidResponse;
            std::string reason_;
        };

        class PromptExpressionWriter {
          public:
            explicit PromptExpressionWriter(const TransitionModel &model) : model_(model) {}

            std::optional<std::string> write(const symbolic::Expr &expression) {
                for (std::size_t index = 0; index < model_.variables.size(); ++index) {
                    if (expression.structurallyEqual(model_.variables[index].current))
                        return "v" + std::to_string(index);
                }
                for (std::size_t index = 0; index < model_.parameters.size(); ++index) {
                    if (expression.structurallyEqual(model_.parameters[index]))
                        return "p" + std::to_string(index);
                }
                for (std::size_t index = 0; index < model_.variables.size(); ++index) {
                    const auto &entry = model_.variables[index].entry;
                    if ((entry.isSymbolValue() || entry.isRangeIndex()) &&
                        expression.structurallyEqual(entry))
                        return "e" + std::to_string(index);
                }
                if (auto literal = symbolic::LiteralExpr::tryFrom(expression))
                    return std::to_string(literal->value());
                if (auto unary = symbolic::UnaryExpr::tryFrom(expression)) {
                    auto operand = write(unary->operand());
                    if (!operand)
                        return std::nullopt;
                    if (unary->operation() == symbolic::UnaryOp::Minus)
                        return "(-" + *operand + ")";
                    if (unary->operation() == symbolic::UnaryOp::LogicalNot)
                        return "(!" + *operand + ")";
                    reason_ = "prompt contains an unsupported unary operation";
                    return std::nullopt;
                }
                if (auto binary = symbolic::BinaryExpr::tryFrom(expression)) {
                    auto left  = write(binary->left());
                    auto right = write(binary->right());
                    if (!left || !right)
                        return std::nullopt;
                    auto token = operationToken(binary->operation());
                    if (!token)
                        return std::nullopt;
                    return "(" + *left + " " + *token + " " + *right + ")";
                }
                reason_ = "prompt contains an unsupported expression leaf";
                return std::nullopt;
            }

            const std::string &reason() const { return reason_; }

          private:
            std::optional<std::string> operationToken(symbolic::BinaryOp operation) {
                switch (operation) {
                    case symbolic::BinaryOp::Add: return "+";
                    case symbolic::BinaryOp::Subtract: return "-";
                    case symbolic::BinaryOp::Multiply: return "*";
                    case symbolic::BinaryOp::LessThan: return "<";
                    case symbolic::BinaryOp::GreaterThan: return ">";
                    case symbolic::BinaryOp::LessEqual: return "<=";
                    case symbolic::BinaryOp::GreaterEqual: return ">=";
                    case symbolic::BinaryOp::Equal: return "==";
                    case symbolic::BinaryOp::NotEqual: return "!=";
                    case symbolic::BinaryOp::LogicalAnd: return "&&";
                    case symbolic::BinaryOp::LogicalOr: return "||";
                    default:
                        reason_ = "prompt contains an unsupported binary operation";
                        return std::nullopt;
                }
            }

            const TransitionModel &model_;
            std::string reason_;
        };

        std::string obligationName(ObligationKind kind) {
            switch (kind) {
                case ObligationKind::Initiation: return "initiation";
                case ObligationKind::Definedness: return "definedness";
                case ObligationKind::Consecution: return "consecution";
                case ObligationKind::ExitPost: return "exit-post";
            }
            return "unknown";
        }

        std::optional<std::string> buildUserPrompt(const ClauseProviderRequest &request,
                                                   std::string &reason) {
            PromptExpressionWriter writer(request.model);
            auto precondition  = writer.write(request.model.precondition);
            auto loopCondition = writer.write(request.model.loopCondition);
            if (!precondition || !loopCondition) {
                reason = writer.reason();
                return std::nullopt;
            }

            std::ostringstream prompt;
            prompt << "Transition model uses current variables v0..v"
                   << (request.model.variables.size() - 1) << " and parameters p0..p";
            if (request.model.parameters.empty())
                prompt << "(none)";
            else
                prompt << (request.model.parameters.size() - 1);
            prompt << ".\nPrecondition: " << *precondition << "\nLoop condition: " << *loopCondition
                   << "\nBranches:\n";
            for (std::size_t branchIndex = 0; branchIndex < request.model.branches.size();
                 ++branchIndex) {
                const auto &branch = request.model.branches[branchIndex];
                auto guard         = writer.write(branch.guard);
                if (!guard) {
                    reason = writer.reason();
                    return std::nullopt;
                }
                prompt << "  branch " << branchIndex << " guard " << *guard << ": ";
                for (std::size_t variableIndex = 0; variableIndex < branch.nextValues.size();
                     ++variableIndex) {
                    auto next = writer.write(branch.nextValues[variableIndex]);
                    if (!next) {
                        reason = writer.reason();
                        return std::nullopt;
                    }
                    if (variableIndex != 0)
                        prompt << ", ";
                    prompt << "v" << variableIndex << "' = " << *next;
                }
                prompt << '\n';
            }
            if (request.model.postcondition) {
                auto postcondition = writer.write(*request.model.postcondition);
                if (!postcondition) {
                    reason = writer.reason();
                    return std::nullopt;
                }
                prompt << "Postcondition: " << *postcondition << '\n';
            } else {
                prompt << "Postcondition: none\n";
            }

            if (request.feedback) {
                prompt << "Previous candidates failed the "
                       << obligationName(request.feedback->failedObligation) << " obligation";
                if (request.feedback->failedBranch)
                    prompt << " on branch " << *request.feedback->failedBranch;
                prompt << ". Generate clauses that address that failure.\n";
            }

            prompt << "Return at most " << request.maxAtomicClauses
                   << " clauses using exactly this schema:\n"
                      "{\"clauses\":[{\"op\":\"le\",\"lhs\":{\"symbol\":\"v0\"},"
                      "\"rhs\":{\"symbol\":\"p0\"}}]}\n"
                      "Arithmetic nodes are {\"integer\":N}, {\"symbol\":\"v0\"}, "
                      "{\"op\":\"neg\",\"operand\":NODE}, or "
                      "{\"op\":\"add|sub|mul\",\"lhs\":NODE,\"rhs\":NODE}. "
                      "Comparison op is eq, ne, lt, le, gt, or ge.";
            return prompt.str();
        }

        std::string endpoint(std::string baseUrl) {
            while (!baseUrl.empty() && baseUrl.back() == '/')
                baseUrl.pop_back();
            return baseUrl + "/chat/completions";
        }

        ClauseProviderResult invalidConfiguration(std::string reason) {
            return {ClauseProviderStatus::ConfigurationError, {}, std::move(reason)};
        }

        bool writeAll(int descriptor, std::string_view data) {
            while (!data.empty()) {
                const auto written = ::write(descriptor, data.data(), data.size());
                if (written < 0) {
                    if (errno == EINTR)
                        continue;
                    return false;
                }
                data.remove_prefix(static_cast<std::size_t>(written));
            }
            return true;
        }

        class TemporaryFile {
          public:
            explicit TemporaryFile(std::string_view pattern) {
                path_ = "/tmp/" + std::string(pattern);
                std::vector<char> writable(path_.begin(), path_.end());
                writable.push_back('\0');
                descriptor_ = ::mkstemp(writable.data());
                path_       = writable.data();
                if (descriptor_ >= 0)
                    ::fchmod(descriptor_, S_IRUSR | S_IWUSR);
            }

            ~TemporaryFile() {
                if (descriptor_ >= 0)
                    ::close(descriptor_);
                if (!path_.empty())
                    ::unlink(path_.c_str());
            }

            TemporaryFile(const TemporaryFile &)            = delete;
            TemporaryFile &operator=(const TemporaryFile &) = delete;

            bool valid() const { return descriptor_ >= 0; }
            int descriptor() const { return descriptor_; }
            const std::string &path() const { return path_; }

            void closeDescriptor() {
                if (descriptor_ >= 0) {
                    ::close(descriptor_);
                    descriptor_ = -1;
                }
            }

          private:
            std::string path_;
            int descriptor_ = -1;
        };

        std::optional<std::string> readFile(const std::string &path, std::size_t maximum) {
            const int descriptor = ::open(path.c_str(), O_RDONLY);
            if (descriptor < 0)
                return std::nullopt;
            std::string result;
            std::array<char, 4096> buffer{};
            while (true) {
                const auto count = ::read(descriptor, buffer.data(), buffer.size());
                if (count < 0) {
                    if (errno == EINTR)
                        continue;
                    ::close(descriptor);
                    return std::nullopt;
                }
                if (count == 0)
                    break;
                if (result.size() + static_cast<std::size_t>(count) > maximum) {
                    ::close(descriptor);
                    return std::nullopt;
                }
                result.append(buffer.data(), static_cast<std::size_t>(count));
            }
            ::close(descriptor);
            return result;
        }

        std::string quoteCurlConfig(std::string_view value) {
            std::string quoted{"\""};
            for (char character : value) {
                if (character == '\\' || character == '"')
                    quoted.push_back('\\');
                quoted.push_back(character);
            }
            quoted.push_back('"');
            return quoted;
        }
    } // namespace

    ClauseProviderResult parseAtomicClauseJson(const TransitionModel &model,
                                               std::string_view json,
                                               const ClauseJsonLimits &limits) {
        if (limits.maxClauses == 0 || limits.maxNodes == 0 || limits.maxDepth == 0)
            return {ClauseProviderStatus::BudgetExceeded, {}, "JSON parsing budget is zero"};
        return ClauseJsonParser{model, limits}.parse(json);
    }

    HttpPostResponse CurlProcessTransport::post(const HttpPostRequest &request) {
        if (request.url.empty() || containsLineBreak(request.url))
            return {false, 0, {}, "invalid HTTP URL"};
        if (request.timeoutMs == 0 || request.maxResponseBytes == 0)
            return {false, 0, {}, "invalid HTTP budget"};
        for (const auto &[name, value] : request.headers) {
            if (name.empty() || containsLineBreak(name) || containsLineBreak(value))
                return {false, 0, {}, "invalid HTTP header"};
        }

        TemporaryFile body{"acslg-llm-body-XXXXXX"};
        TemporaryFile output{"acslg-llm-output-XXXXXX"};
        TemporaryFile errors{"acslg-llm-errors-XXXXXX"};
        if (!body.valid() || !output.valid() || !errors.valid())
            return {false, 0, {}, "cannot create private transport files"};
        if (!writeAll(body.descriptor(), request.body))
            return {false, 0, {}, "cannot write HTTP request body"};
        body.closeDescriptor();
        output.closeDescriptor();

        int configPipe[2]{};
        int statusPipe[2]{};
        if (::pipe(configPipe) != 0 || ::pipe(statusPipe) != 0)
            return {false, 0, {}, "cannot create HTTP transport pipes"};

        const pid_t child = ::fork();
        if (child < 0) {
            ::close(configPipe[0]);
            ::close(configPipe[1]);
            ::close(statusPipe[0]);
            ::close(statusPipe[1]);
            return {false, 0, {}, "cannot start curl"};
        }
        if (child == 0) {
            ::dup2(configPipe[0], STDIN_FILENO);
            ::dup2(statusPipe[1], STDOUT_FILENO);
            ::dup2(errors.descriptor(), STDERR_FILENO);
            ::close(configPipe[0]);
            ::close(configPipe[1]);
            ::close(statusPipe[0]);
            ::close(statusPipe[1]);

            const auto timeoutSeconds =
                std::max(1U, static_cast<unsigned>((request.timeoutMs + 999U) / 1000U));
            const auto maximumBytes = std::to_string(request.maxResponseBytes);
            const auto timeout      = std::to_string(timeoutSeconds);
            const auto bodyArgument = "@" + body.path();
            const char *arguments[] = {"curl",
                                       "--config",
                                       "-",
                                       "--silent",
                                       "--show-error",
                                       "--request",
                                       "POST",
                                       "--data-binary",
                                       bodyArgument.c_str(),
                                       "--output",
                                       output.path().c_str(),
                                       "--write-out",
                                       "%{http_code}",
                                       "--max-time",
                                       timeout.c_str(),
                                       "--max-filesize",
                                       maximumBytes.c_str(),
                                       request.url.c_str(),
                                       nullptr};
            ::execvp("curl", const_cast<char *const *>(arguments));
            _exit(127);
        }

        ::close(configPipe[0]);
        ::close(statusPipe[1]);
        std::string config;
        for (const auto &[name, value] : request.headers)
            config += "header = " + quoteCurlConfig(name + ": " + value) + "\n";
        const bool wroteConfig = writeAll(configPipe[1], config);
        ::close(configPipe[1]);

        std::array<char, 32> statusBuffer{};
        std::string statusText;
        while (true) {
            const auto count = ::read(statusPipe[0], statusBuffer.data(), statusBuffer.size());
            if (count < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (count == 0)
                break;
            statusText.append(statusBuffer.data(), static_cast<std::size_t>(count));
        }
        ::close(statusPipe[0]);

        int waitStatus = 0;
        while (::waitpid(child, &waitStatus, 0) < 0 && errno == EINTR) {}
        errors.closeDescriptor();

        auto errorText = readFile(errors.path(), 16U << 10U).value_or("cannot read curl error");
        if (!wroteConfig || !WIFEXITED(waitStatus) || WEXITSTATUS(waitStatus) != 0)
            return {false, 0, {}, errorText.empty() ? "curl request failed" : errorText};

        unsigned statusCode = 0;
        const auto converted =
            std::from_chars(statusText.data(), statusText.data() + statusText.size(), statusCode);
        if (converted.ec != std::errc{})
            return {false, 0, {}, "curl returned an invalid HTTP status"};
        auto responseBody = readFile(output.path(), request.maxResponseBytes);
        if (!responseBody)
            return {false, statusCode, {}, "HTTP response exceeded the configured size limit"};
        return {true, statusCode, std::move(*responseBody), {}};
    }

    std::optional<OpenAICompatibleProviderConfig> OpenAICompatibleProviderConfig::fromEnvironment(
        std::string &error) {
        OpenAICompatibleProviderConfig config;
        const char *apiKey = std::getenv("ACSLG_LLM_API_KEY");
        if (!apiKey || *apiKey == '\0') {
            error = "ACSLG_LLM_API_KEY is not set";
            return std::nullopt;
        }
        config.apiKey = apiKey;
        if (const char *baseUrl = std::getenv("ACSLG_LLM_BASE_URL"))
            config.baseUrl = baseUrl;
        if (const char *model = std::getenv("ACSLG_LLM_MODEL"))
            config.model = model;
        return config;
    }

    OpenAICompatibleClauseProvider::OpenAICompatibleClauseProvider(
        HttpTransport &transport,
        OpenAICompatibleProviderConfig config)
        : transport_(transport), config_(std::move(config)) {}

    ClauseProviderResult OpenAICompatibleClauseProvider::generate(
        const ClauseProviderRequest &request) {
        if (auto modelError = validateTransitionModel(request.model))
            return {ClauseProviderStatus::InvalidModel, {}, *modelError};
        if (request.maxAtomicClauses == 0)
            return {ClauseProviderStatus::BudgetExceeded, {}, "atomic clause budget is zero"};
        if (config_.apiKey.empty())
            return invalidConfiguration("API key is empty");
        if (!config_.baseUrl.starts_with("https://"))
            return invalidConfiguration("provider base URL must use HTTPS");
        if (config_.model.empty() || config_.timeoutMs == 0 || config_.maxResponseBytes == 0 ||
            config_.maxTokens == 0)
            return invalidConfiguration("provider configuration contains a zero or empty budget");
        if (containsLineBreak(config_.apiKey) || containsLineBreak(config_.baseUrl) ||
            containsLineBreak(config_.model))
            return invalidConfiguration("provider configuration contains a line break");

        std::string promptError;
        auto userPrompt = buildUserPrompt(request, promptError);
        if (!userPrompt)
            return {ClauseProviderStatus::UnsupportedModel, {}, std::move(promptError)};

        llvm::json::Array messages;
        messages.push_back(
            llvm::json::Object{{"role", "system"}, {"content", std::string(SYSTEM_PROMPT)}});
        messages.push_back(
            llvm::json::Object{{"role", "user"}, {"content", std::move(*userPrompt)}});
        llvm::json::Object body;
        body["model"]           = config_.model;
        body["messages"]        = std::move(messages);
        body["stream"]          = false;
        body["temperature"]     = 0;
        body["max_tokens"]      = static_cast<std::int64_t>(config_.maxTokens);
        body["response_format"] = llvm::json::Object{{"type", "json_object"}};
        if (config_.thinkingEnabled) {
            body["thinking"] =
                llvm::json::Object{{"type", *config_.thinkingEnabled ? "enabled" : "disabled"}};
        }

        HttpPostRequest httpRequest;
        httpRequest.url = endpoint(config_.baseUrl);
        httpRequest.headers.emplace_back("Content-Type", "application/json");
        httpRequest.headers.emplace_back("Authorization", "Bearer " + config_.apiKey);
        httpRequest.body             = jsonText(std::move(body));
        httpRequest.timeoutMs        = config_.timeoutMs;
        httpRequest.maxResponseBytes = config_.maxResponseBytes;

        auto response = transport_.post(httpRequest);
        if (!response.transportSucceeded)
            return {ClauseProviderStatus::TransportError, {}, std::move(response.error)};
        if (response.statusCode < 200 || response.statusCode >= 300) {
            return {ClauseProviderStatus::TransportError,
                    {},
                    "provider returned HTTP " + std::to_string(response.statusCode)};
        }

        auto envelope = llvm::json::parse(response.body);
        if (!envelope)
            return {
                ClauseProviderStatus::InvalidResponse, {}, "provider envelope is not valid JSON"};
        const auto *root    = envelope->getAsObject();
        const auto *choices = root ? root->getArray("choices") : nullptr;
        if (!choices || choices->empty())
            return {ClauseProviderStatus::InvalidResponse, {}, "provider envelope has no choices"};
        const auto *choice  = choices->front().getAsObject();
        const auto *message = choice ? choice->getObject("message") : nullptr;
        const auto content  = message ? message->getString("content") : std::nullopt;
        if (!content)
            return {ClauseProviderStatus::InvalidResponse,
                    {},
                    "provider choice has no message content"};
        if (const auto finishReason = choice->getString("finish_reason");
            finishReason && *finishReason != "stop") {
            return {ClauseProviderStatus::InvalidResponse,
                    {},
                    "provider response finished with '" + finishReason->str() + "'"};
        }

        ClauseJsonLimits jsonLimits;
        jsonLimits.maxClauses = request.maxAtomicClauses;
        return parseAtomicClauseJson(request.model, *content, jsonLimits);
    }

} // namespace acslg::analyzer::invariant
