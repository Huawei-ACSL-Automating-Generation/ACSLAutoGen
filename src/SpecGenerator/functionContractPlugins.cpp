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
        unordered_map<size_t, const AddressBox> assignedAddrs;

        auto isFromPointer = [&](auto f, const Address &addr) -> bool {
            using enum Address::AddressType;
            switch (addr.getAddressType()) {
                case VariableAddr: return false;
                case SymbolAddr: {
                    auto &symbolAddr = dynamic_cast<const SymbolAddress &>(addr);

                    return std::visit(
                        [&](auto &&arg) -> bool {
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, std::monostate>) {
                                TODO();
                            } else if constexpr (std::is_same_v<
                                                     T, not_null<std::unique_ptr<const Address>>>) {
                                return true;
                            }
                        },
                        symbolAddr.getFrom());
                }
                case FieldAddr: {
                    auto &fieldAddr = dynamic_cast<const FieldAddress &>(addr);
                    return std::visit(
                        [&](auto &&arg) -> bool {
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, std::monostate>) {
                                TODO();
                            } else if constexpr (std::is_same_v<
                                                     T, std::pair<
                                                            not_null<std::unique_ptr<const Address>>,
                                                            const size_t>>) {
                                return f(f, *arg.first);
                            }
                        },
                        fieldAddr.getFrom());
                }
            }
        };

        // Function's pre-state should have exactly one path.
        if (auto &paths = pre.getPaths(); paths.size() == 1) {
            auto &prePath = paths[0];

            // For every post-state path
            for (auto &postPath : post.getPaths()) {
                // and every Address in the path's memoryState.
                for (auto &&[addr, value] : postPath->getMemoryState().flat()) {
                    if (!isFromPointer(isFromPointer, addr))
                        continue;
                    postPath->isUnchanged(addr);

                    auto [_, ok] = assignedAddrs.try_emplace(addr.hash(), std::move(addr));
                    if (ok) {
                        INFO(addr.get().dump());
                        INFO(value->dump());
                    }
                }
            }
        }

        for (auto &[_, addr] : assignedAddrs) {
            auto regForm = addr.get().regularFormOfValue();
            if (regForm == nullopt) {
                WARN("Value of {" + addr.get().dump() + "} has no regular form.");
                continue;
            }
            spec += regForm.value() + ", ";
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
                returnExpr = path->getReturnExpr().value()->clone().into_underlying();
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
            auto regForm = returnExpr->simplifiedExpr()->regularForm("\\Old(", ")");
            if (regForm == nullopt) {
                WARN("Return Expr {" + returnExpr->simplifiedExpr()->dump() +
                     "} has no regular form.");
                return nullopt;
            }
            return "ensures \\result == " + regForm.value();
        }
        return nullopt;
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(ResultPlugin, "result");

// class PostStatePlugin : public FunctionContractPlugin {
//   public:
//     PostStatePlugin(const std::string &ID) : id_(ID) {}
//     std::string_view id() const override { return id_; }
//     std::optional<std::string> generate(const ProgramState &pre,
//                                         const ProgramState &post) const override {
//         std::vector<std::string> behaviors;
//         int idx = 0;

//         const auto *FD = post.getContext()->getFunctionDecl();

//         for (auto &pathPtr : post.getPaths()) {
//             const auto &path = *pathPtr;
//             if (path.getPathState() != Path::PathState::Return)
//                 continue;

//             std::string assumes = joinConj(path.getPathConditions());
//             std::vector<std::string> ensures;

//             if (auto &ret = path.getReturnExpr()) {
//                 auto regForm = ret.value()->simplifiedExpr()->regularForm("\\old(", ")");
//                 if (regForm == nullopt) {
//                     WARN("Return Expr {" + ret.value()->simplifiedExpr()->dump() +
//                          "} has no regular form.");
//                     return nullopt;
//                 }
//                 ensures.push_back("\\result == " + regForm.value());
//             }

//             auto find_pre_value = [&](const Address *faddr) -> std::unique_ptr<SymbolicExpr> {
//                 if (pre.getPaths().size() != 1)
//                     return nullptr;
//                 const auto &prePath = *pre.getPaths().front();
//                 const auto &preMem  = prePath.getMemoryState();

//                 if (auto value = preMem.read(*faddr))
//                     return value.value()->clone();

//                 using pair_t =
//                     std::pair<not_null<std::shared_ptr<const Structure::Info>>, const size_t>;
//                 if (auto postPair = std::get_if<pair_t>(&faddr->getFrom())) {
//                     for (const auto &[addr, value] : preMem.flat()) {
//                         if (auto prePair = std::get_if<pair_t>(&addr.getFrom())) {
//                             if (postPair->second == prePair->second &&
//                                 postPair->first->equal(*prePair->first)) {
//                                 return value->clone();
//                             }
//                         }
//                     }
//                 }
//                 return nullptr;
//             };

//             auto emitStruct = [&](auto &&self, const std::string &base, const Structure &st,
//                                   bool topPtr) -> void {
//                 auto RD    = st.getInfo()->definition_;
//                 auto slots = st.fieldsAddrs();
//                 for (const auto *fieldDecl : RD->fields()) {
//                     size_t fidx          = fieldDecl->getFieldIndex();
//                     const Address *faddr = slots[fidx] ? slots[fidx].value().get().get() :
//                     nullptr; if (!faddr)
//                         continue;

//                     auto postVal = path.getMemoryState().read(*faddr);
//                     if (!postVal)
//                         continue;

//                     std::string lhs = base + (topPtr ? "->" : ".") + fieldDecl->getNameAsString();

//                     if (postVal.value()->getType() == SymbolicExpr::ExprType::Structure) {
//                         self(self, lhs, *static_cast<const Structure *>(postVal.value().get()),
//                              false);
//                     } else {
//                         std::unique_ptr<SymbolicExpr> preVal = find_pre_value(faddr);
//                         bool unchanged                       = false;
//                         if (preVal) {
//                             unchanged =
//                                 postVal.value()->equal(*preVal) ||
//                                 (*postVal.value()->simplifiedExpr() == *preVal->simplifiedExpr());
//                         }
//                         if (unchanged) {
//                             ensures.push_back(lhs + " == \\old(" + lhs + ")");
//                         } else {
//                             std::string rhs =
//                                 postVal.value()->simplifiedExpr()->regularForm("\\old(", ")");
//                             ensures.push_back(lhs + " == " + rhs);
//                         }
//                     }
//                 }
//             };

//             for (const clang::ParmVarDecl *param : FD->parameters()) {
//                 auto itAddr = path.getVarAddr().find(param);
//                 if (itAddr == path.getVarAddr().end())
//                     continue;

//                 auto value = path.getMemoryState().read(*itAddr->second);
//                 if (value == nullopt)
//                     continue;

//                 auto &finalVal = *value.value();
//                 if (finalVal.isUnknown())
//                     continue;

//                 std::string varName = param->getNameAsString();
//                 auto finalValReg    = finalVal.simplifiedExpr()->regularForm("\\old(", ")");
//                 if (finalValReg) {
//                     ensures.push_back(varName + " == " + finalValReg.value());
//                 } else {
//                     WARN("Expr {" + finalVal.simplifiedExpr()->dump() + "} has no regular form.");
//                 }

//                 if (param->getType()->isPointerType()) {
//                     auto baseTy = param->getType()->getPointeeType();
//                     if (baseTy->isStructureType() &&
//                         finalVal.getType() == SymbolicExpr::ExprType::Address) {
//                         auto addr    = static_cast<const Address &>(finalVal);
//                         auto pointee = path.getMemoryState().read(addr);
//                         if (pointee &&
//                             pointee.value()->getType() == SymbolicExpr::ExprType::Structure) {
//                             emitStruct(emitStruct, varName,
//                                        *static_cast<const Structure *>(pointee.value().get()),
//                                        true);
//                         }
//                     }
//                 } else if (param->getType()->isStructureType()) {
//                     if (finalVal.getType() == SymbolicExpr::ExprType::Structure) {
//                         emitStruct(emitStruct, varName, static_cast<const Structure &>(finalVal),
//                                    false);
//                     }
//                 }
//             }

//             if (assumes.empty() && ensures.empty())
//                 continue;

//             std::string bname = "b" + std::to_string(idx++);
//             std::string block;
//             block += IND1 + "behavior " + bname + ":\n";
//             if (!assumes.empty())
//                 block += IND2 + "assumes " + assumes + ";\n";
//             for (auto &e : ensures)
//                 block += IND2 + "ensures " + e + ";\n";

//             behaviors.push_back(std::move(block));
//         }

//         if (behaviors.empty())
//             return std::nullopt;

//         std::string out;
//         for (auto &b : behaviors)
//             out += b;

//         std::vector<std::string> names;
//         for (int i = 0; i < (int)behaviors.size(); ++i)
//             names.push_back("b" + std::to_string(i));
//         out += IND1 + "complete behaviors " + joinCSV(names) + ";\n";
//         return out;
//     }

//   private:
//     std::string id_;

//     static std::string joinConj(const Formulas &conds) {
//         std::string s;
//         for (size_t i = 0; i < conds.size(); ++i) {
//             const auto &c = conds[i];
//             if (c->isUnknown())
//                 continue;
//             auto regForm = c->simplifiedExpr()->regularForm();
//             if (regForm == nullopt) {
//                 WARN("Expr {" + c->simplifiedExpr()->dump() + "} has no regular form.");
//                 continue;
//             }
//             if (regForm.value().empty())
//                 continue;
//             if (!s.empty())
//                 s += " && ";
//             s += "(" + regForm.value() + ")";
//         }
//         return s;
//     }

//     static std::string joinCSV(const std::vector<std::string> &v) {
//         std::string s;
//         for (size_t i = 0; i < v.size(); ++i) {
//             if (i)
//                 s += ", ";
//             s += v[i];
//         }
//         return s;
//     }
// };

// REGISTER_ACSL_PLUGIN(PostStatePlugin, "poststate");
