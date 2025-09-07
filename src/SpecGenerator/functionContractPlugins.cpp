// src/SpecGenerator/functionContractPlugins.cpp

#include "specGenerator.h"
#include "macros.h"
#include "state.h"
#include "utils.h"
#include "expr.h"

using namespace std;
using namespace clang;

const std::string IND1 = "  ";
const std::string IND2 = "    ";

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
            return IND1 + string("assigns \\nothing;\n");
        else
            return IND1 + string("assigns ") + spec.substr(0, spec.length() - 2) + ";\n";
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

class PostStatePlugin : public FunctionContractPlugin {
  public:
    PostStatePlugin(const std::string &ID) : id_(ID) {}
    std::string_view id() const override { return id_; }
    std::optional<std::string> generate(const ProgramState &,
                                        const ProgramState &post) const override {
        std::vector<std::string> behaviors;
        int idx = 0;

        const auto *FD = post.getContext()->getFunctionDecl();

        for (auto &pathPtr : post.getPaths()) {
            const auto &path = *pathPtr;
            if (path.getPathState() != Path::PathState::Return)
                continue;

            std::string assumes = joinConj(path.getPathConditions());
            std::vector<std::string> ensures;

            if (auto &ret = path.getReturnExpr()) {
                ensures.push_back("\\result == " +
                                  ret.value()->simplifiedExpr()->regularForm("\\old(", ")"));
            }

            auto emitStruct = [&](auto &&self, const std::string &base, const Structure &st,
                                  bool topPtr) -> void {
                auto RD = st.getInfo()->definition_;
                for (const auto *fieldDecl : RD->fields()) {
                    size_t fidx = fieldDecl->getFieldIndex();
                    auto fv     = st.getFieldValue(fidx);
                    if (!fv)
                        continue;
                    std::string lhs = base + (topPtr ? "->" : ".") + fieldDecl->getNameAsString();
                    if (fv->getType() == SymbolicExpr::ExprType::Structure) {
                        self(self, lhs, *static_cast<const Structure *>(fv.get()), false);
                    } else {
                        std::string rhs = fv->simplifiedExpr()->regularForm("\\old(", ")");
                        ensures.push_back(lhs + " == " + rhs);
                    }
                }
            };

            for (const clang::ParmVarDecl *param : FD->parameters()) {
                auto itAddr = path.getVarAddr().find(param);
                if (itAddr == path.getVarAddr().end() || !itAddr->second)
                    continue;

                auto value = path.getMemoryState().read(*itAddr->second);
                if (value == nullopt)
                    continue;

                auto &finalVal = *value.value();
                if (finalVal.isUnknown())
                    continue;

                std::string varName = param->getNameAsString();
                std::string rhsTop  = finalVal.simplifiedExpr()->regularForm("\\old(", ")");
                ensures.push_back(varName + " == " + rhsTop);

                if (param->getType()->isPointerType()) {
                    auto baseTy = param->getType()->getPointeeType();
                    if (baseTy->isStructureType() &&
                        finalVal.getType() == SymbolicExpr::ExprType::SymbolAddress) {
                        auto addr    = static_cast<const Address &>(finalVal);
                        auto pointee = path.getMemoryState().read(addr);
                        if (pointee &&
                            pointee.value()->getType() == SymbolicExpr::ExprType::Structure) {
                            emitStruct(emitStruct, varName,
                                       *static_cast<const Structure *>(pointee.value().get()),
                                       true);
                        }
                    }
                }
            }

            if (assumes.empty() && ensures.empty())
                continue;

            std::string bname = "b" + std::to_string(idx++);
            std::string block;
            block += IND1 + "behavior " + bname + ":\n";
            if (!assumes.empty())
                block += IND2 + "assumes " + assumes + ";\n";
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

    static std::string joinConj(const Formulas &conds) {
        std::string s;
        for (size_t i = 0; i < conds.size(); ++i) {
            const auto &c = conds[i];
            if (c->isUnknown())
                continue;
            std::string ci = c->simplifiedExpr()->regularForm("", "");
            if (ci.empty())
                continue;
            if (!s.empty())
                s += " && ";
            s += "(" + ci + ")";
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

REGISTER_ACSL_PLUGIN(PostStatePlugin, "poststate");
