// src/SpecGenerator/functionContractPlugins.cpp

#include <unordered_set>
#include "specGenerator.h"
#include "stringTemplate.h"
#include "macros.h"
#include "state.h"
#include "utils.h"
#include "expr.h"

using namespace std;
using namespace clang;

class AssignsPlugin : public FunctionContractPlugin {
  public:
    AssignsPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    optional<string> generate(const ProgramState &pre, const ProgramState &post) const override {
        string spec;
        vector<const Address *> assignedAddrs;

        auto isExisted = [&](const Address &addr) {
            // The time complexity can be reduced from O(n) to O(1), but it requires a complex
            // memoized recursive hash implementation.
            for (auto &it : assignedAddrs) {
                if (*it == addr)
                    return true;
            }
            return false;
        };

        auto isFromPointer = [&](const Address &addr) {
            return std::visit(
                [this](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        TODO();
                        return false;
                    } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                        return false;
                    } else if constexpr (std::is_same_v<T,
                                                        not_null<std::unique_ptr<const Address>>>) {
                        return true;
                    } else if constexpr (std::is_same_v<T, std::pair<not_null<std::shared_ptr<
                                                                         const Structure::Info>>,
                                                                     const size_t>>) {
                        TODO();
                        return false;
                    }
                },
                addr.getFrom());
        };

        // Function's pre-state should have exactly one path.
        if (auto &paths = pre.getPaths(); paths.size() == 1) {
            auto &prePath = paths[0];

            // For every post-state path
            for (auto &postPath : post.getPaths()) {
                // and every Address in the path's memoryState.
                for (auto &&[addr, value] : postPath->getMemoryState().flat()) {
                    if (!isFromPointer(addr))
                        continue;
                    if (value->getType() == SymbolicExpr::ExprType::Variable) {
                        auto symbol = dynamic_cast<const Symbolic::Variable *>(value.get().get());

                        if (!symbol)
                            ERROR("A SymolicExpr with type 'Variable' but is not a Variable!");

                        if (auto fromAddr =
                                get_if<not_null<unique_ptr<const Address>>>(&symbol->getFrom())) {
                            if ((**fromAddr) == addr)
                                continue;
                        } else {
                            TODO();
                        }
                    }
                    if (isExisted(addr))
                        continue;

                    assignedAddrs.push_back(&addr);
                }
            }
        }

        for (auto &addr : assignedAddrs) {
            spec += "*" + addr->regularForm() + ", ";
        }

        if (spec.empty())
            return R"(assigns \nothing;)";
        else
            return "assigns " + spec.substr(0, spec.length() - 2) + ";";
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(AssignsPlugin, "assigns");

class ResultPlugin : public FunctionContractPlugin {
  public:
    ResultPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    optional<string> generate(const ProgramState &, const ProgramState &post) const override {
        unique_ptr<SymbolicExpr> returnExpr{nullptr};
        for (auto &path : post.getPaths()) {
            if (path->getReturnExpr() == nullopt)
                continue;
            if (returnExpr == nullptr) {
                returnExpr = path->getReturnExpr().value()->clone();
            } else {
                auto &pathReturnExpr = path->getReturnExpr();
                if (pathReturnExpr == nullopt) {
                    ERROR("Some paths reach the end of the function without a return statement.");
                }
                if (*pathReturnExpr.value() != *returnExpr) {
                    returnExpr = nullptr;
                    break;
                }
            }
        }

        if (returnExpr != nullptr) {
            return "ensures \\result == " +
                   returnExpr->simplifiedExpr()->regularForm("\\Old(", ")");
        }
        return nullopt;
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(ResultPlugin, "result");
