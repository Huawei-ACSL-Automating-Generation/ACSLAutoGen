// src/SpecGenerator/functionContractPlugins.cpp

#include "specGenerator.h"
#include "macros.h"
#include "state.h"
#include "utils.h"
#include "expr.h"
#include <iterator>
#include <unordered_set>

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
        GenResultType generate(const analyzer::ProgramState &pre,
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

            auto oldPoint = pre.getStartPoint();
            std::unordered_set<symb::SourcePoint> allUsedPoints;
            for (auto &[_, addr] : assignedAddrs) {
                auto acslExpected =
                    addr.get().getACSLOfValue({.predefinedLabels = {{oldPoint, "Old"}}}, oldPoint);
                if (!acslExpected) {
                    WARN("Value of {" + addr.get().dump() + "} getACSL failed.");
                    continue;
                }
                auto &[addrStr, usedPoints] = acslExpected.value();
                spec += addrStr + ", ";
                allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                     std::make_move_iterator(usedPoints.end()));
            }

            if (spec.empty())
                return std::pair{IND1 + std::string("assigns \\nothing;\n"),
                                 std::unordered_set<symb::SourcePoint>{}};
            else
                return std::pair{IND1 + std::string("assigns ") +
                                     spec.substr(0, spec.length() - 2) + ";\n",
                                 std::move(allUsedPoints)};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(TopAssignsPlugin, "assigns");

    class DetailBehaviorPlugin : public FunctionContractPlugin {
      public:
        DetailBehaviorPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }

        GenResultType generate(const analyzer::ProgramState &,
                               const analyzer::ProgramState &post) const override {
            auto oldPoint = post.getStartPoint();
            std::unordered_set<symb::SourcePoint> allUsedPoints{};

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
                    auto acslExpected =
                        a.get().getACSLOfValue({.predefinedLabels = {{oldPoint, "Old"}}}, oldPoint);
                    if (!acslExpected) {
                        WARN("Value of {" + a.get().dump() + "} getACSL failed.");
                        continue;
                    }
                    auto &[addrStr, usedPoints] = acslExpected.value();
                    assignsSpec += addrStr + ", ";
                    allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                         std::make_move_iterator(usedPoints.end()));
                }
                if (assignsSpec.empty())
                    assignsSpec = "\\nothing";
                else
                    assignsSpec.erase(assignsSpec.size() - 2);

                std::vector<std::string> ensures;

                // result
                if (auto &ret = path.getReturnExpr()) {
                    if (auto expected = ret.value()->simplifiedExpr()->getACSL(
                            {.predefinedLabels = {{oldPoint, "Old"}}})) {
                        auto &[spec, usedPoints] = expected.value();
                        ensures.push_back("\\result == " + spec);
                        allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                             std::make_move_iterator(usedPoints.end()));
                    }
                }

                // Memory equations
                for (auto &&[addr, value] : path.getMemoryState().flat()) {
                    if (is_symbol_addr(addr) && llvm::isa<symb::Structure>(value.get()))
                        continue;

                    auto lhsOpt =
                        addr.get().getACSLOfValue({.predefinedLabels = {{oldPoint, "Old"}}});
                    if (!lhsOpt)
                        continue;

                    auto rhsOpt =
                        value->simplifiedExpr()->getACSL({.predefinedLabels = {{oldPoint, "Old"}}});
                    if (!rhsOpt)
                        continue;

                    ensures.push_back(lhsOpt.value().first + " == " + rhsOpt.value().first);
                    allUsedPoints.insert(std::make_move_iterator(lhsOpt.value().second.begin()),
                                         std::make_move_iterator(lhsOpt.value().second.end()));
                    allUsedPoints.insert(std::make_move_iterator(rhsOpt.value().second.begin()),
                                         std::make_move_iterator(rhsOpt.value().second.end()));
                }

                auto req = joinConj(path.getPathConditions(), oldPoint);
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
                return std::pair{std::nullopt, std::unordered_set<symb::SourcePoint>{}};

            std::string out;
            for (auto &b : behaviors)
                out += b;

            std::vector<std::string> names;
            for (int i = 0; i < (int)behaviors.size(); ++i)
                names.push_back("b" + std::to_string(i));
            out += IND1 + "complete behaviors " + joinCSV(names) + ";\n";
            return std::pair{out, std::move(allUsedPoints)};
        }

      private:
        std::string id_;

        static std::string joinConj(const analyzer::Formulas &conds, symb::SourcePoint oldPoint) {
            std::string s;
            for (size_t i = 0; i < conds.size(); ++i) {
                const auto &c = conds[i];
                if (c->isUnknown())
                    continue;
                auto rf = c->simplifiedExpr()->getACSL({.predefinedLabels = {{oldPoint, "Old"}}});
                if (!rf || rf.value().first.empty())
                    continue;
                if (!s.empty())
                    s += " && ";
                s += "(" + rf.value().first + ")";
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