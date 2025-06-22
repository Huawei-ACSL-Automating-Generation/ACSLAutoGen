// src/SpecGenerator/functionContractPlugins.cpp

#include <unordered_set>
#include "specGenerator.h"
#include "stringTemplate.h"
#include "macros.h"
#include "state.h"
#include "utils.h"

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
            if (get_if<unique_ptr<Address>>(&addr.getFrom()))
                return true;
            return false;
        };

        // Function's pre-state should have exactly one path.
        if (auto &paths = pre.getPaths(); paths.size() == 1) {
            auto &prePath = paths[0];

            // For every post-state path
            for (auto &postPath : post.getPaths()) {
                // and every Address in the path's memoryState.
                for (auto &[addr, value] : postPath->getMemoryState()) {
                    if (!isFromPointer(addr))
                        continue;
                    if (value->getType() == SymbolicExpr::ExprType::Variable) {
                        auto symbol = dynamic_cast<const Symbolic::Variable *>(value.get());

                        if (!symbol)
                            ERROR("A SymolicExpr with type 'Variable' but is not a Variable!");

                        if (*symbol->getFrom() == addr) {
                            continue;
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
            if (returnExpr == nullptr) {
                returnExpr = path->getReturnExpr()->clone();
            } else {
                auto &pathReturnExpr = path->getReturnExpr();
                if (pathReturnExpr == nullptr) {
                    ERROR("Some paths reach the end of the function without a return statement.");
                }
                if (*pathReturnExpr != *returnExpr) {
                    returnExpr = nullptr;
                    break;
                }
            }
        }

        if (returnExpr != nullptr)
            return "ensures \\result == " + returnExpr->regularForm("\\old(", ")");
        return nullopt;
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(ResultPlugin, "result");
