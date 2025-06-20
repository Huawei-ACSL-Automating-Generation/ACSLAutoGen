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
            if (holds_alternative<not_null<unique_ptr<Address>>>(addr.getFrom()))
                return true;
            else if (holds_alternative<monostate>(addr.getFrom()))
                TODO();
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

                        if (auto &from = symbol->getFrom()) {
                            if (**from == addr)
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
        // TODO
        return nullopt;
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(ResultPlugin, "result");
