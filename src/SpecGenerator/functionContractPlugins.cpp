// src/SpecGenerator/functionContractPlugins.cpp

#include "specGenerator.h"
#include "macros.h"
#include "state.h"
#include "utils.h"
#include "expr.h"

namespace acslg::spec_generator {
    namespace symb = acslg::analyzer::symbolic;

    namespace {
        const std::string IND1 = "  ";
        const std::string IND2 = "    ";
    } // namespace

    class TopAssignsPlugin : public FunctionContractPlugin {
      public:
        TopAssignsPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        std::optional<std::string> generate(const analyzer::ProgramState &pre,
                                            const analyzer::ProgramState &post) const override {
            std::string spec;
            std::unordered_map<size_t, const symb::AddressBox> assignedAddrs;

            // Function's pre-state should have exactly one path.
            if (auto &paths = pre.getPaths(); paths.size() == 1) {
                auto &prePath = paths[0];

                for (auto &postPath : post.getPaths()) {
                    // INFO("path");
                    for (auto &&[addr, value] : postPath->getMemoryState().flat()) {
                        // INFO(addr.get().dump());
                        // INFO(value->dump());

                        // If the current address corresponds to a pointer targeting a structure,
                        // the associated handling is deliberately omitted. This omission is
                        // justified by the design of the flat() traversal: the fields of the
                        // structure are enumerated and processed individually. Thus, treating the
                        // pointer itself would introduce redundancy.
                        if (is_symbol_addr(addr) && postPath->is_point_to_structure(addr))
                            continue;

                        if (!postPath->isUnchanged(addr))
                            auto [_, ok] = assignedAddrs.try_emplace(addr.hash(), std::move(addr));
                    }
                }
            }

            for (auto &[_, addr] : assignedAddrs) {
                auto regForm = addr.get().regularFormOfValue();
                if (regForm == std::nullopt) {
                    WARN("Value of {" + addr.get().dump() + "} has no regular form.");
                    continue;
                }
                spec += regForm.value() + ", ";
            }

            if (spec.empty())
                return IND1 + std::string("assigns \\nothing;\n");
            else
                return IND1 + std::string("assigns ") + spec.substr(0, spec.length() - 2) + ";\n";
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(TopAssignsPlugin, "assigns");

    class DetailBehaviorPlugin : public FunctionContractPlugin {
      public:
        DetailBehaviorPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }

        std::optional<std::string> generate(const analyzer::ProgramState &,
                                            const analyzer::ProgramState &post) const override {
            std::vector<std::string> behaviors;
            int idx = 0;

            for (auto &pathPtr : post.getPaths()) {
                const auto &path = *pathPtr;
                if (path.getPathState() != analyzer::Path::PathState::Return)
                    continue;

                std::unordered_map<size_t, const symb::AddressBox> assignedAddrs;
                for (auto &&[a, v] : path.getMemoryState().flat()) {
                    if (is_symbol_addr(a) && path.is_point_to_structure(a))
                        continue;
                    if (!path.isUnchanged(a))
                        (void)assignedAddrs.try_emplace(a.hash(), std::move(a));
                }

                std::string assignsSpec;
                for (auto &[_, a] : assignedAddrs) {
                    auto rf = a.get().regularFormOfValue();
                    if (!rf) {
                        WARN("Value of {" + a.get().dump() + "} has no regular form.");
                        continue;
                    }
                    assignsSpec += rf.value() + ", ";
                }
                if (assignsSpec.empty())
                    assignsSpec = "\\nothing";
                else
                    assignsSpec.erase(assignsSpec.size() - 2);

                std::vector<std::string> ensures;

                // result
                if (auto &ret = path.getReturnExpr()) {
                    if (auto rf = ret.value()->simplifiedExpr()->regularForm("\\old(", ")"))
                        ensures.push_back("\\result == " + rf.value());
                }

                // Memory equations
                for (auto &&[addr, value] : path.getMemoryState().flat()) {
                    if (is_symbol_addr(addr) &&
                        value->getType() == symb::SymbolicExpr::ExprType::Structure)
                        continue;

                    auto lhsOpt = addr.get().regularFormOfValue();
                    if (!lhsOpt)
                        continue;

                    auto rhsOpt = value->simplifiedExpr()->regularForm("\\old(", ")");
                    if (!rhsOpt)
                        continue;

                    ensures.push_back(lhsOpt.value() + " == " + rhsOpt.value());
                }

                auto req = joinConj(path.getPathConditions());
                if (ensures.empty() && req.empty() && assignsSpec == "\\nothing")
                    continue;

                std::string bname = "b" + std::to_string(idx++);
                std::string block;
                block += IND1 + "behavior " + bname + ":\n";
                if (!req.empty())
                    block += IND2 + "requires " + req + ";\n";
                block += IND2 + "assigns " + assignsSpec + ";\n";
                for (auto &e : ensures)
                    block += IND2 + "ensures " + e + ";\n";

                behaviors.push_back(std::move(block));
            }

            if (behaviors.empty())
                return std::nullopt;

            std::string out;
            for (auto &b : behaviors)
                out += b;

            std::vector<std::string> names;
            for (int i = 0; i < (int)behaviors.size(); ++i)
                names.push_back("b" + std::to_string(i));
            out += IND1 + "complete behaviors " + joinCSV(names) + ";\n";
            return out;
        }

      private:
        std::string id_;

        static std::string joinConj(const analyzer::Formulas &conds) {
            std::string s;
            for (size_t i = 0; i < conds.size(); ++i) {
                const auto &c = conds[i];
                if (c->isUnknown())
                    continue;
                auto rf = c->simplifiedExpr()->regularForm();
                if (!rf || rf->empty())
                    continue;
                if (!s.empty())
                    s += " && ";
                s += "(" + rf.value() + ")";
            }
            return s;
        }

        static std::string joinCSV(const std::vector<std::string> &v) {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i) {
                if (i)
                    s += ", ";
                s += v[i];
            }
            return s;
        }
    };

    REGISTER_ACSL_PLUGIN(DetailBehaviorPlugin, "poststate");
} // namespace acslg::spec_generator