#include <queue>
#include <unordered_map>
#include <memory>
#include <set>
#include <variant>
#include <llvm/ADT/TypeSwitch.h>
#include <llvm/ADT/APSInt.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/Decl.h>
#include <clang/AST/StmtCXX.h>
#include "state.h"
#include "macros.h"
#include "Utils/utils.h"
#include "Context/globalSM.h"
#include "SpecGenerator/specGenerator.h"

using namespace std;
using namespace clang;
using namespace llvm;
using namespace Symbolic;

using LValueTarget = variant<const VarDecl *, unique_ptr<Address>>;

Path::Path(const Path &other, bool shallowCopy) {
    if (shallowCopy) {
        for (const auto &cond : other.pathConditions_) {
            pathConditions_.push_back(cond->clone());
        }
        currentState_ = other.currentState_;
        if (other.returnExpr_)
            returnExpr_.emplace(other.returnExpr_.value()->clone());
        else
            returnExpr_ = std::nullopt;
        symbolVarAndAddrCounter_ = other.symbolVarAndAddrCounter_;
    } else {
        TODO();
    }
}
void Path::resymbolize() {
    memoryState_.clear();
    pathConditions_.clear();

    for (auto &entry : varAddr_) {
        const VarDecl *varDecl = entry.first;
        QualType varType       = varDecl->getType();
        Address *addr          = entry.second.get();

        if (!varType->isPointerType() && !varType->isArrayType()) {
            SymbolicExpr::Type derived       = deriveVarType(varType);
            unique_ptr<SymbolicExpr> newExpr = make_unique<Symbolic::Variable>(
                varDecl->getNameAsString(), derived, symbolVarAndAddrCounter_++,
                std::unique_ptr<Address>(static_cast<Address *>(addr->clone().release())));

            memoryState_.write(*addr, std::move(newExpr));
        } else if (varType->isPointerType()) {
            auto pointeeAddr = allocMemory(*addr);
            memoryState_.write(*addr, std::move(pointeeAddr));
        } else {
            TODO();
        }
    }
}

LValueTarget Path::extractLValue(const Expr *lhs) {
    const Expr *lexpr = lhs->IgnoreParenImpCasts();
    if (auto *declRef = dyn_cast<DeclRefExpr>(lexpr))
        return dyn_cast<VarDecl>(declRef->getDecl());
    if (auto *arr = dyn_cast<ArraySubscriptExpr>(lexpr)) {
        auto baseLVal = extractLValue(arr->getBase());
        unique_ptr<Address> addr;
        if (auto declPtr = get_if<const VarDecl *>(&baseLVal)) {
            Address *variableAddr;
            if (auto it = varAddr_.find(*declPtr); it != varAddr_.end()) {
                variableAddr = it->second.get();
            } else {
                ERROR("varState has no ArraySubscriptExpr's base, undefined variable?");
            }
            if (auto symbol = memoryState_.read(*variableAddr)) {
                auto ptr = dynamic_cast<Address *>(symbol.get());
                if (ptr == nullptr)
                    ERROR("Value of ArraySubscriptExpr's base is not 'Address', base is neither "
                          "pointer nor "
                          "array?");
                addr = unique_ptr<Address>{static_cast<Address *>(symbol.release())};
            } else {
                ERROR("memoryState_ has no ArraySubscriptExpr's base, base is neither pointer nor "
                      "array?");
            }
        } else {
            UNIMPLEMENT("Multidimensional pointer is not supported now.");
            // There is a logical error here regarding multidimensional pointers.
            // addr = get<unique_ptr<Address>>(baseLVal).get();
        }
        auto idxEval = evalExpr(arr->getIdx());
        if (idxEval.second.size() != 1)
            ERROR("This location does not support control flow branches.");
        auto idxExpr = std::move(idxEval.second[0]);
        addr->setOffset(std::move(idxExpr));
        return addr;
    }

    if (auto *uop = dyn_cast<UnaryOperator>(lexpr)) {
        if (uop->getOpcode() == UO_Deref) {
            auto addrEval = evalExpr(uop->getSubExpr());
            if (addrEval.second.size() != 1)
                TODO();

            auto addrExpr = std::move(addrEval.second[0]);
            if (addrExpr == nullptr) {
                ERROR("Met nullptr in EvalResult.");
            }
            if (auto addr = addrExpr->tryEvalAsOffsetedAddr()) {
                if (memoryState_.read(*addr) == nullptr) {
                    SymbolicExpr::Type varType = deriveVarType(uop->getType());
                    string varName = addr->getBaseName() + "[" + addr->getOffset()->dump() +
                                     "]"; // TODO: impl function to get pointer/array base name.
                    auto varExpr = std::make_unique<Symbolic::Variable>(
                        varName, varType, getNextSymVarId(), make_unique<Address>(*addr));
                    memoryState_.write(*addr, std::move(varExpr));
                }
                return addr;
            } else {
                ERROR("Expected Address in deref, got: " << addrExpr->dump());
            }
        }
    }

    UNIMPLEMENT("Unsupported LHS expression: " << lexpr->getStmtClassName());
}

unique_ptr<Address> Path::extractAddress(const Expr *lhs) {
    auto lv = extractLValue(lhs);
    if (auto varPtr = get_if<const VarDecl *>(&lv)) {
        assert(*varPtr != nullptr);
        return unique_ptr<Address>(static_cast<Address *>(varAddr_[*varPtr]->clone().release()));
    }
    if (auto addrPtr = get_if<unique_ptr<Address>>(&lv)) {
        assert(*addrPtr != nullptr);
        return std::move(*addrPtr);
    }
    UNIMPLEMENT("extractAddress: unsupported lvalue for address");
}

unique_ptr<SymbolicExpr> Path::getVarState(const VarDecl *var) const {
    auto canonicalVar = var->getCanonicalDecl();
    auto varIt        = varAddr_.find(canonicalVar);
    if (varIt == varAddr_.end())
        ERROR("Variable '" + canonicalVar->getNameAsString() + "' has no allocated address");
    auto addr  = varIt->second.get();
    auto value = memoryState_.read(*addr);
    if (value == nullptr)
        ERROR("Variable '" + canonicalVar->getNameAsString() +
              "' has no memory state entry for allocated address");
    return value;
}

const Formulas &Path::getPathConditions() const { return pathConditions_; }

Address *Path::allocMemory(const VarDecl *var) {
    auto canonicalVar = var->getCanonicalDecl();
    if (varAddr_.find(canonicalVar) != varAddr_.end())
        ERROR("Variable already has allocated memory");
    auto newAddr    = make_unique<Address>(symbolVarAndAddrCounter_++, var);
    Address *rawPtr = newAddr.get();
    varAddr_.emplace(canonicalVar, std::move(newAddr));

    // Prevent uninitialized variables.
    memoryState_.write(*rawPtr, UnknownExpr::makeUnknown());
    return rawPtr;
}

unique_ptr<Address> Path::allocMemory(const Address &from) {
    return make_unique<Address>(symbolVarAndAddrCounter_++,
                                std::unique_ptr<Address>(make_unique<Address>(from)));
}

void Path::updateMemory(Address *addr, unique_ptr<SymbolicExpr> expr) {
    if (expr == nullptr)
        ERROR("Expr is nullptr!");
    memoryState_.write(*addr, std::move(expr));
}

void Path::updateVarState(const VarDecl *var, unique_ptr<SymbolicExpr> expr) {
    if (expr == nullptr)
        ERROR("Expr is nullptr!");

    auto canonicalVar = var->getCanonicalDecl();
    auto addrIt       = varAddr_.find(canonicalVar);
    if (addrIt == varAddr_.end())
        ERROR("Variable has no allocated address");

    auto &addr = addrIt->second;
    memoryState_.write(*addr, std::move(expr));
}

void Path::insertPathCondition(unique_ptr<SymbolicExpr> cond) {
    if (!cond)
        return;
    pathConditions_.push_back(std::move(cond));
}

unique_ptr<Path> Path::clone() const {
    auto cloned           = make_unique<Path>();
    cloned->currentState_ = currentState_;
    for (const auto &entry : varAddr_)
        cloned->varAddr_.emplace(entry.first, unique_ptr<Address>(static_cast<Address *>(
                                                  entry.second->clone().release())));
    cloned->memoryState_ = memoryState_;
    for (const auto &cond : pathConditions_)
        cloned->pathConditions_.push_back(cond->clone());
    if (returnExpr_)
        cloned->returnExpr_.emplace(returnExpr_.value()->clone());
    else
        cloned->returnExpr_ = nullopt;
    cloned->symbolVarAndAddrCounter_ = symbolVarAndAddrCounter_;
    cloned->StmtCtx                  = StmtCtx;
    return cloned;
}

Path::EvalResult Path::evalExpr(const Expr *expr) {
    if (!expr)
        ERROR("Fail to convert an empty Expr");

    EvalResult eval_result =
        TypeSwitch<const Expr *, EvalResult>(expr)
            .Case<IntegerLiteral>([](const IntegerLiteral *lit) -> EvalResult {
                APInt ap         = lit->getValue();
                QualType litType = lit->getType();
                unique_ptr<SymbolicExpr> result;

                if (litType->isBooleanType()) {
                    result = make_unique<LiteralExpr>(static_cast<bool>(ap.getZExtValue()));
                } else if (litType->isUnsignedIntegerType()) {
                    if (ap.getBitWidth() <= 8)
                        result =
                            make_unique<LiteralExpr>(static_cast<unsigned char>(ap.getZExtValue()));
                    else if (ap.getBitWidth() <= 16)
                        result = make_unique<LiteralExpr>(
                            static_cast<unsigned short>(ap.getZExtValue()));
                    else if (ap.getBitWidth() <= 32)
                        result =
                            make_unique<LiteralExpr>(static_cast<unsigned int>(ap.getZExtValue()));
                    else if (ap.getBitWidth() <= 64)
                        result = make_unique<LiteralExpr>(static_cast<uint64_t>(ap.getZExtValue()));
                    else
                        UNIMPLEMENT("Unsupported unsigned integer literal with bit width > 64: "
                                    << ap.getBitWidth());
                } else {
                    if (ap.getBitWidth() <= 8)
                        result = make_unique<LiteralExpr>(static_cast<char>(ap.getSExtValue()));
                    else if (ap.getBitWidth() <= 16)
                        result = make_unique<LiteralExpr>(static_cast<short>(ap.getSExtValue()));
                    else if (ap.getBitWidth() <= 32)
                        result = make_unique<LiteralExpr>(static_cast<int>(ap.getSExtValue()));
                    else if (ap.getBitWidth() <= 64)
                        result = make_unique<LiteralExpr>(static_cast<int64_t>(ap.getSExtValue()));
                    else
                        UNIMPLEMENT("Unsupported signed integer literal with bit width > 64: "
                                    << ap.getBitWidth());
                }

                vector<unique_ptr<Path>> paths;
                Formulas exprs;
                exprs.reserve(1);
                exprs.push_back(std::move(result));

                return {std::move(paths), std::move(exprs)};
            })
            .Case<BinaryOperator>([this](const BinaryOperator *binOp) -> EvalResult {
                // TODO: maybe pack the logic in BO, ArraySub into a function?
                EvalResult lhs            = evalExpr(binOp->getLHS());
                BinaryOpExpr::Operator op = getBinaryOp(binOp->getOpcode());

                vector<unique_ptr<Path>> outPaths;
                Formulas outExprs;

                size_t lhsCount = lhs.second.size();
                for (size_t i = 0; i < lhsCount; ++i) {
                    auto lhsExpr    = std::move(lhs.second[i]);
                    Path *path      = (i == 0) ? this : lhs.first[i - 1].get();
                    EvalResult rhs  = path->evalExpr(binOp->getRHS());
                    size_t rhsCount = rhs.second.size();

                    for (size_t j = 0; j < rhsCount; ++j) {
                        unique_ptr<SymbolicExpr> rhsExpr = std::move(rhs.second[j]);

                        outExprs.emplace_back(
                            make_unique<BinaryOpExpr>(lhsExpr->clone(), op, std::move(rhsExpr)));

                        if (i == 0 && j == 0)
                            continue;

                        outPaths.emplace_back(j == 0 ? std::move(lhs.first[i - 1])
                                                     : std::move(rhs.first[j - 1]));
                    }
                }

                return {std::move(outPaths), std::move(outExprs)};
            })
            .Case<ParenExpr>([this](const ParenExpr *paren) -> EvalResult {
                return evalExpr(paren->getSubExpr());
            })
            .Case<DeclRefExpr>([this](const DeclRefExpr *declRef) -> EvalResult {
                if (const auto *varDecl = dyn_cast<VarDecl>(declRef->getDecl())) {
                    auto varExpr = getVarState(varDecl);
                    vector<unique_ptr<Path>> paths;
                    Formulas exprs;
                    exprs.push_back(std::move(varExpr));
                    return {std::move(paths), std::move(exprs)};
                }

                if (const auto *enumDecl = dyn_cast<EnumConstantDecl>(declRef->getDecl())) {
                    APSInt value = enumDecl->getInitVal();
                    auto litExpr = make_unique<LiteralExpr>(static_cast<int>(value.getSExtValue()));
                    vector<unique_ptr<Path>> paths;
                    Formulas exprs;
                    exprs.push_back(std::move(litExpr));
                    return {std::move(paths), std::move(exprs)};
                }

                UNIMPLEMENT("Unsupported Decl type: " << declRef->getDecl()->getDeclKindName());
                return Path::EvalResult{};
            })
            .Case<ArraySubscriptExpr>([this](const ArraySubscriptExpr *arrSub) -> EvalResult {
                LValueTarget lval = extractLValue(arrSub->getBase());
                string baseName;
                if (auto varPtr = get_if<const VarDecl *>(&lval))
                    baseName = (*varPtr)->getNameAsString();
                else
                    UNIMPLEMENT("Array base is not a single Variable");

                unique_ptr<Address> variableAddr = extractAddress(arrSub->getBase());
                unique_ptr<Address> addr;
                if (auto symbol = memoryState_.read(*variableAddr)) {
                    auto ptr = dynamic_cast<const Address *>(symbol.get());
                    if (ptr == nullptr)
                        ERROR(
                            "Value of ArraySubscriptExpr's base is not 'Address', base is neither "
                            "pointer nor "
                            "array?");
                    addr = unique_ptr<Address>{static_cast<Address *>(symbol.release())};
                } else {
                    ERROR("memoryState_ has no ArraySubscriptExpr's base, base is neither pointer "
                          "nor "
                          "array?");
                }
                EvalResult idx             = evalExpr(arrSub->getIdx());
                SymbolicExpr::Type varType = deriveVarType(arrSub->getBase()->getType());

                vector<unique_ptr<Path>> outPaths;
                Formulas outExprs;

                for (size_t i = 0; i < idx.second.size(); ++i) {
                    auto idxExpr   = std::move(idx.second[i]);
                    string idxDump = idxExpr->dump();
                    auto newAddr   = make_unique<Address>(*addr);
                    newAddr->setOffset(std::move(idxExpr));
                    if (auto value = memoryState_.read(*newAddr); value == nullptr) {
                        string varName = baseName + "[" + idxDump +
                                         "]"; // TODO: impl function to get pointer/array base name.
                        auto varExpr = make_unique<Symbolic::Variable>(
                            varName, varType, symbolVarAndAddrCounter_++,
                            unique_ptr<Address>(
                                static_cast<Address *>(newAddr->clone().release())));
                        memoryState_.write(*newAddr, varExpr->clone());
                        outExprs.emplace_back(std::move(varExpr));
                    } else {
                        outExprs.emplace_back(std::move(value));
                    }
                    if (i > 0)
                        outPaths.emplace_back(std::move(idx.first[i - 1]));
                }

                return {std::move(outPaths), std::move(outExprs)};
            })
            .Case<CallExpr>([](const CallExpr *call) -> EvalResult {
                const FunctionDecl *callee = call->getDirectCallee();
                if (!callee)
                    return Path::EvalResult{};
                static const set<string> ignoreNames = {"llvm.dbg.declare", "llvm.lifetime.start",
                                                        "llvm.lifetime.end", "printf",
                                                        "__assert_fail"};
                string name                          = callee->getNameAsString();
                if (ignoreNames.contains(name))
                    return Path::EvalResult{};

                // TODO: complete the logic to call function.
                UNIMPLEMENT("Unhandled CallExpr to function: " << name);
                return Path::EvalResult{};
            })
            .Case<ConditionalOperator>([this](const ConditionalOperator *condOp) -> EvalResult {
                EvalResult cond = evalExpr(condOp->getCond());

                vector<unique_ptr<Path>> outPaths;
                Formulas outExprs;

                for (size_t i = 0; i < cond.second.size(); ++i) {
                    Path *condPath = (i == 0) ? this : cond.first[i - 1].get();
                    auto condExpr  = std::move(cond.second[i]);

                    // false branch
                    auto falsePath   = condPath->clone();
                    auto negatedCond = make_unique<UnaryOpExpr>(UnaryOpExpr::Operator::LogicalNot,
                                                                condExpr->clone());
                    falsePath->insertPathCondition(std::move(negatedCond));

                    EvalResult falseVal = falsePath->evalExpr(condOp->getFalseExpr());

                    // true branch
                    auto truePath = condPath;
                    truePath->insertPathCondition(std::move(condExpr));

                    EvalResult trueVal = truePath->evalExpr(condOp->getTrueExpr());

                    // merge results
                    for (size_t j = 0; j < trueVal.second.size(); ++j) {
                        outExprs.emplace_back(std::move(trueVal.second[j]));
                        if (j > 0)
                            outPaths.emplace_back(std::move(trueVal.first[j - 1]));
                    }
                    for (size_t j = 0; j < falseVal.second.size(); ++j) {
                        outExprs.emplace_back(std::move(falseVal.second[j]));
                        if (j == 0)
                            outPaths.emplace_back(std::move(falsePath));
                        else
                            outPaths.emplace_back(std::move(falseVal.first[j - 1]));
                    }
                }

                return {std::move(outPaths), std::move(outExprs)};
            })
            .Case<UnaryOperator>([this](const UnaryOperator *uop) -> EvalResult {
                auto operand = evalExpr(uop->getSubExpr());
                vector<unique_ptr<Path>> outPaths;
                Formulas outExprs;

                UnaryOpExpr::Operator op;
                switch (uop->getOpcode()) {
                    case UO_Plus: op = UnaryOpExpr::Operator::Plus; break;
                    case UO_Minus: op = UnaryOpExpr::Operator::Minus; break;
                    case UO_LNot: op = UnaryOpExpr::Operator::LogicalNot; break;
                    case UO_Not: op = UnaryOpExpr::Operator::BitwiseNot; break;
                    case UO_PreInc: op = UnaryOpExpr::Operator::PreInc; break;
                    case UO_PreDec: op = UnaryOpExpr::Operator::PreDec; break;
                    case UO_PostInc: op = UnaryOpExpr::Operator::PostInc; break;
                    case UO_PostDec: op = UnaryOpExpr::Operator::PostDec; break;
                    case UO_AddrOf: op = UnaryOpExpr::Operator::AddrOf; break;
                    case UO_Deref: op = UnaryOpExpr::Operator::Dereference; break;
                    default:
                        UNIMPLEMENT("Unsupported UnaryOperator: "
                                    << uop->getOpcodeStr(uop->getOpcode()).str());
                }

                for (size_t i = 0; i < operand.second.size(); ++i) {
                    Path *path  = (i == 0 ? this : operand.first[i - 1].get());
                    auto unExpr = std::move(operand.second[i]);

                    // Prevent misuse by not capturing this and operand.
                    [&path, &unExpr, &op, &outExprs, &uop]() {
                        if (op == UnaryOpExpr::Operator::PreInc ||
                            op == UnaryOpExpr::Operator::PostInc ||
                            op == UnaryOpExpr::Operator::PreDec ||
                            op == UnaryOpExpr::Operator::PostDec) {
                            // ++x / x++ / --x / x--
                            unique_ptr<Address> addr = path->extractAddress(uop->getSubExpr());
                            if (!addr)
                                ERROR("extractAddress returned null");
                            // old value
                            auto oldVal = path->memoryState_.read(*addr);
                            if (oldVal == nullptr)
                                ERROR("memoryState_ doesn't contain addr.");
                            // compute new = old +/- 1
                            auto one   = make_unique<LiteralExpr>(1);
                            auto binOp = (op == UnaryOpExpr::Operator::PreInc ||
                                          op == UnaryOpExpr::Operator::PostInc)
                                             ? BinaryOpExpr::Operator::Add
                                             : BinaryOpExpr::Operator::Subtract;
                            auto newVal =
                                make_unique<BinaryOpExpr>(oldVal->clone(), binOp, std::move(one));
                            // write back
                            path->memoryState_.write(*addr, newVal->clone());
                            // return pre vs post
                            if (op == UnaryOpExpr::Operator::PreInc ||
                                op == UnaryOpExpr::Operator::PreDec)
                                outExprs.emplace_back(std::move(newVal));
                            else
                                outExprs.emplace_back(std::move(oldVal));
                        } else if (op == UnaryOpExpr::Operator::Dereference) {
                            // *x
                            auto addr = unExpr->tryEvalAsOffsetedAddr();
                            if (addr == nullptr)
                                ERROR("Expected Address, got: " << unExpr->dump());
                            if (auto value = path->memoryState_.read(*addr); value == nullptr) {
                                SymbolicExpr::Type varType = deriveVarType(uop->getType());
                                string varName =
                                    addr->getBaseName() + "[" + addr->getOffset()->dump() +
                                    "]"; // TODO: impl function to get pointer/array base name.
                                auto varExpr = std::make_unique<Symbolic::Variable>(
                                    varName, varType, path->getNextSymVarId(),
                                    make_unique<Address>(*addr));
                                path->memoryState_.write(*addr, varExpr->clone());
                                outExprs.emplace_back(std::move(varExpr));
                            } else {
                                outExprs.emplace_back(std::move(value));
                            }
                        } else if (op == UnaryOpExpr::Operator::AddrOf)
                            TODO();
                        else
                            outExprs.emplace_back(make_unique<UnaryOpExpr>(op, std::move(unExpr)));
                    }();
                    if (i > 0)
                        outPaths.emplace_back(std::move(operand.first[i - 1]));
                }

                return {std::move(outPaths), std::move(outExprs)};
            })
            .Case<CastExpr>([this](const CastExpr *castExpr) -> EvalResult {
                auto sub = evalExpr(castExpr->getSubExpr());
                if (castExpr->getType()->isStructureType())
                    return sub;

                auto targetType = deriveVarType(castExpr->getType());

                for (auto &subExpr : sub.second)
                    subExpr->setValType(targetType);

                return {std::move(sub.first), std::move(sub.second)};
            })
            .Case<MemberExpr>([this](const MemberExpr *memberExpr) -> EvalResult {
                EvalResult base = evalExpr(memberExpr->getBase());
                if (base.second.size() != 1)
                    ERROR("No control flow branching permitted within a pointer-to-member "
                          "expression.");

                if (base.second[0]->getType() != SymbolicExpr::ExprType::Structure)
                    ERROR("LHS of a pointer-to-member expression with '.' is not a structure");

                auto st = unique_ptr<Structure>(static_cast<Structure *>(base.second[0].release()));
                if (auto member = dyn_cast_if_present<FieldDecl>(memberExpr->getMemberDecl())) {
                    auto symbolExpr = st->getFieldValue(member->getFieldIndex());
                    if (symbolExpr == nullptr)
                        ERROR("An incomplete Structure is accessed");

                    auto result = EvalResult{};
                    result.second.push_back(std::move(symbolExpr));
                    return result;
                } else {
                    ERROR("MemberDecl is not a FieldDecl.");
                }
            })
            .Case<ConstantExpr>([this](const ConstantExpr *ce) -> EvalResult {
                APSInt v;
                bool ok = false;
                if (ce->getResultAPValueKind() == APValue::Int) {
                    v  = ce->getResultAsAPSInt();
                    ok = (v.getBitWidth() != 0);
                }
                if (!ok) {
                    EvalResult sub = evalExpr(ce->getSubExpr());
                    if (sub.second.size() != 1)
                        ERROR("ConstantExpr subExpr produced multiple results");
                    auto resultTy = deriveVarType(ce->getType());
                    sub.second[0]->setValType(resultTy);
                    return {std::move(sub.first), std::move(sub.second)};
                }
                auto resultTy = deriveVarType(ce->getType());
                auto lit      = std::make_unique<LiteralExpr>(
                    v.isSigned() ? static_cast<int64_t>(v.getSExtValue())
                                 : static_cast<uint64_t>(v.getZExtValue()));
                lit->setValType(resultTy);
                EvalResult r;
                r.second.emplace_back(std::move(lit));
                return r;
            })
            .Default([](const Expr *e) -> EvalResult {
                UNIMPLEMENT("Unsupported Expr type: " << e->getStmtClassName());
                return Path::EvalResult{};
            });
    return eval_result;
}

string Path::dump() const {
    ostringstream oss;

    oss << "\nPath State: " << [&]() {
        switch (currentState_) {
            case Path::PathState::Step: return "Step";
            case Path::PathState::Continue: return "Continue";
            case Path::PathState::Break: return "Break";
            case Path::PathState::Return: return "Return";
            default: return "Unknown";
        }
    }() << "\n";
    oss << "Symbol variables and addresses Counter: " << symbolVarAndAddrCounter_ << "\n";

    oss << "Return Expression: ";
    if (returnExpr_)
        oss << returnExpr_.value()->dump();
    else
        oss << "null";
    oss << "\n";

    oss << "Path Conditions:\n";
    for (size_t i = 0; i < pathConditions_.size(); ++i) {
        oss << "  [" << i << "]: " << (pathConditions_[i] ? pathConditions_[i]->dump() : "null")
            << "\n";
    }

    unordered_set<Address, AddressHash> printedAddrs;

    oss << "Variable Address Mapping:\n";
    for (auto &[varDecl, addr] : varAddr_) {
        string name;
        if (auto opt = GlobalSM::getDeclInfo(varDecl))
            tie(name, ignore, ignore, ignore, ignore) = *opt;

        oss << "  @" << name << " -> " << addr->dump();

        if (auto value = memoryState_.read(*addr)) {
            oss << " -> " << value->dump();
            printedAddrs.insert(*addr);
        } else {
            oss << " -> null";
        }
        oss << "\n";
    }

    if (printedAddrs.size() < memoryState_.size()) {
        oss << "Memory State:\n";
        for (auto &&[addr, value] : memoryState_.flat()) {
            if (!printedAddrs.contains(addr)) {
                oss << "  " << addr.dump() << " -> " << (value ? value->dump() : "null") << "\n";
            }
        }
    }

    if (StmtCtx) {
        if (auto opt = GlobalSM::getStmtInfo(StmtCtx)) {
            StringRef sourceText;
            tie(sourceText, ignore, ignore, ignore) = *opt;
            if (!sourceText.empty()) {
                oss << "Stmt Context: " << sourceText.str() << "\n";
            }
        }
    }
    return oss.str();
}

bool Path::isUnchanged(const Address &addr) const {
    auto value = memoryState_.read(addr);
    if (value == nullptr)
        return false;

    if (value->getType() == SymbolicExpr::ExprType::Variable) {
        auto var =
            unique_ptr<Symbolic::Variable>(static_cast<Symbolic::Variable *>(value.release()));
        if (auto fromAddr = get_if<not_null<unique_ptr<const Address>>>(&var->getFrom())) {
            if (**fromAddr == addr)
                return true;
        } else
            TODO();
    }
    return false;
}

MemoryModel::MemoryModel(const MemoryModel &other) {
    for (auto &[addr, value] : other.memoryMap_noOffset_) {
        memoryMap_noOffset_.emplace(addr, value->clone());
    }
    for (auto &[idAddr, rangeValueMap] : other.memoryMap_constantRange_) {
        auto &mapToFill = memoryMap_constantRange_[idAddr];
        for (auto &[range, value] : rangeValueMap)
            mapToFill.emplace(range, value->clone());
    }

    for (auto &[idAddr, rangeValueMap] : other.memoryMap_symbolicRange_) {
        auto &mapToFill = memoryMap_symbolicRange_[idAddr];
        for (auto &[range, value] : rangeValueMap)
            mapToFill.emplace(range, value->clone());
    }
}

MemoryModel &MemoryModel::operator=(const MemoryModel &other) {
    if (this == &other)
        return *this;

    clear();

    for (auto &[addr, value] : other.memoryMap_noOffset_) {
        memoryMap_noOffset_.emplace(addr, value->clone());
    }

    for (auto &[idAddr, rangeValueMap] : other.memoryMap_constantRange_) {
        auto &mapToFill = memoryMap_constantRange_[idAddr];
        for (auto &[range, value] : rangeValueMap)
            mapToFill.emplace(range, value->clone());
    }

    for (auto &[idAddr, rangeValueMap] : other.memoryMap_symbolicRange_) {
        auto &mapToFill = memoryMap_symbolicRange_[idAddr];
        for (auto &[range, value] : rangeValueMap)
            mapToFill.emplace(range, value->clone());
    }

    return *this;
}

unique_ptr<SymbolicExpr> MemoryModel::read(const Address &addr) const {
    if (!addr.isOffseted()) {
        if (memoryMap_noOffset_.contains(addr))
            return memoryMap_noOffset_.at(addr)->clone();
        return nullptr;
    }

    auto idAddr = make_unique<Address>(addr);
    idAddr->resetOffset();
    idAddr->resetRange();

    auto offset = addr.getOffset();
    if (auto constOffset = offset->tryEvalAsConstant(); constOffset && !addr.isRange()) {
        if (constOffset.value() < 0)
            ERROR("Negetive offset.");
        if (!memoryMap_constantRange_.contains(*idAddr))
            return nullptr;
        auto unsignedOffset = static_cast<uint64_t>(constOffset.value());
        auto &rangeExprMap  = memoryMap_constantRange_.at(*idAddr);
        auto range          = pair{unsignedOffset, unsignedOffset + 1};
        auto rangeForSearch = pair{unsignedOffset, numeric_limits<uint64_t>::max()};
        auto upperBoundIt   = rangeExprMap.upper_bound(rangeForSearch);
        auto firstLEIt =
            upperBoundIt == rangeExprMap.begin() ? rangeExprMap.end() : prev(upperBoundIt);
        if (firstLEIt == rangeExprMap.end() || firstLEIt->first.second <= unsignedOffset)
            return nullptr;
        return firstLEIt->second->clone();
    } else if (constOffset && addr.isRange() && addr.getLength()->tryEvalAsConstant()) {
        UNIMPLEMENT("There doesn't appear to be a need for constant-range range queries at "
                    "this time.");
    }

    if (!memoryMap_symbolicRange_.contains(*idAddr))
        return nullptr;
    auto &rangeExprMap = memoryMap_symbolicRange_.at(*idAddr);
    auto it            = rangeExprMap.find(addr);
    if (it == rangeExprMap.end())
        return nullptr;
    return it->second->clone();
}

void MemoryModel::write(const Address &addr, unique_ptr<const SymbolicExpr> value) {
    if (value == nullptr)
        ERROR("Value is nullptr!");

    if (!addr.isOffseted()) {
        memoryMap_noOffset_[addr] = std::move(value);
        return;
    }

    auto idAddr = make_unique<Address>(addr);
    idAddr->resetOffset();
    idAddr->resetRange();

    auto constOffset = addr.getOffset()->tryEvalAsConstant();
    auto constLen    = addr.isRange() ? addr.getLength()->tryEvalAsConstant() : nullopt;

    if (constOffset && (!addr.isRange() || constLen)) {
        if (constOffset.value() < 0 || (constLen && constLen.value() <= 0))
            ERROR("Constant offset must be greater or equal to zero and length must be "
                  "greater than zero.");
        // constant range
        auto unsignedOffset = static_cast<uint64_t>(constOffset.value());
        auto unsignedLen    = constLen ? static_cast<uint64_t>(constLen.value()) : uint64_t{1};
        auto &rangeExprMap  = memoryMap_constantRange_[*idAddr];
        auto range          = pair{unsignedOffset, unsignedOffset + unsignedLen};
        auto rangeForSearch = pair{unsignedOffset, numeric_limits<uint64_t>::max()};
        auto endIt          = rangeExprMap.end();
        auto upperBoundIt   = rangeExprMap.upper_bound(rangeForSearch);
        auto firstLEIt      = upperBoundIt == rangeExprMap.begin() ? endIt : prev(upperBoundIt);
        if (firstLEIt != endIt && firstLEIt->first.second > range.first) {
            if (firstLEIt->first.first < range.first) {
                auto leftRange          = pair{firstLEIt->first.first, range.first};
                rangeExprMap[leftRange] = firstLEIt->second->clone();
            }
            if (firstLEIt->first.second > range.second) {
                auto rightRange          = pair{range.second, firstLEIt->first.second};
                rangeExprMap[rightRange] = firstLEIt->second->clone();
            }
            rangeExprMap.erase(firstLEIt);
        }
        if (upperBoundIt != endIt && upperBoundIt->first.first < range.second) {
            if (upperBoundIt->first.second > range.second) {
                auto rightRange          = pair{range.second, upperBoundIt->first.second};
                rangeExprMap[rightRange] = upperBoundIt->second->clone();
            }
            rangeExprMap.erase(upperBoundIt);
        }
        rangeExprMap[range] = std::move(value);
        return;
    }
    // symbolic range
    auto &rangeExprMap = memoryMap_symbolicRange_[*idAddr];
    rangeExprMap[addr] = std::move(value);
}

size_t MemoryModel::size() const {
    size_t sum = memoryMap_noOffset_.size();
    for (auto &[_, map] : memoryMap_constantRange_)
        sum += map.size();
    for (auto &[_, map] : memoryMap_symbolicRange_)
        sum += map.size();
    return sum;
}

// MemoryModel::flat_view MemoryModel::flat() { return MemoryModel::flat_view{*this}; }
const MemoryModel::flat_view MemoryModel::flat() const {
    return MemoryModel::flat_view{const_cast<MemoryModel &>(*this)};
}

ProgramState::ProgramState(unique_ptr<Path> initialPath, unique_ptr<ACSLFunction> context) {
    paths_.push_back(std::move(initialPath));
    context_ = std::move(context);
}

ProgramState::ProgramState(unique_ptr<ACSLFunction> context) { context_ = std::move(context); }

void ProgramState::init() {
    auto FD       = context_->getFunctionDecl();
    auto initPath = make_unique<Path>();

    for (const ParmVarDecl *param : FD->parameters()) {
        QualType paramType = param->getType();
        if (paramType->isStructureType()) {
            auto addr = initPath->allocMemory(param);
            auto RD   = paramType->getAsRecordDecl();
            if (RD == nullptr || !RD->isCompleteDefinition())
                ERROR("Incomplete definited structure.");
            RD           = RD->getDefinition();
            auto &layout = RD->getASTContext().getASTRecordLayout(RD);
            auto st      = make_unique<Structure>(initPath->getNextStructureId(), RD, layout,
                                                  make_unique<Address>(*addr));

            auto structureSymbolizeInit = [&initPath](auto f, Structure &stToInit) -> void {
                std::ranges::transform(
                    stToInit.getInfo()->definition_->fields(), stToInit.fieldsValues().begin(),
                    [&initPath, &stToInit, &f](const auto &fieldDecl) -> unique_ptr<SymbolicExpr> {
                        if (fieldDecl->getType()->isStructureType()) {
                            auto RD_nested = fieldDecl->getType()->getAsRecordDecl();
                            if (RD_nested == nullptr || !RD_nested->isCompleteDefinition())
                                ERROR("Incomplete definited structure.");
                            RD_nested = RD_nested->getDefinition();
                            auto &layout_nested =
                                RD_nested->getASTContext().getASTRecordLayout(RD_nested);
                            auto st_nested = make_unique<Structure>(
                                initPath->getNextStructureId(), RD_nested, layout_nested,
                                make_pair<std::shared_ptr<const Structure::Info>, const size_t>(
                                    stToInit.getInfo().get(), fieldDecl->getFieldIndex()));
                            f(f, *st_nested);
                            return st_nested;
                        }
                        SymbolicExpr::Type fieldType = deriveVarType(fieldDecl->getType());
                        return make_unique<Symbolic::Variable>(
                            fieldDecl->getNameAsString(), fieldType, initPath->getNextSymVarId(),
                            make_pair<std::shared_ptr<const Structure::Info>, const size_t>(
                                stToInit.getInfo().get(), fieldDecl->getFieldIndex()));
                    });
            };

            structureSymbolizeInit(structureSymbolizeInit, *st);
            initPath->updateMemory(addr, std::move(st));
        } else if (!paramType->isPointerType() && !paramType->isArrayType()) {
            Address *addr                    = initPath->allocMemory(param);
            SymbolicExpr::Type varType       = deriveVarType(paramType);
            unique_ptr<SymbolicExpr> varExpr = make_unique<Symbolic::Variable>(
                param->getNameAsString(), varType, initPath->getNextSymVarId(),
                std::unique_ptr<Address>(static_cast<Address *>(addr->clone().release())));

            initPath->updateMemory(addr, std::move(varExpr));
        } else if (paramType->isPointerType()) {
            QualType baseType = paramType->getPointeeType();
            if (baseType->isPointerType() || baseType->isArrayType())
                UNIMPLEMENT("Unsupported pointer to pointer/array");

            Address *ptrAddr = initPath->allocMemory(param);
            auto pointeeAddr = initPath->allocMemory(*ptrAddr);

            initPath->updateMemory(ptrAddr, std::move(pointeeAddr));
        } else if (paramType->isArrayType()) {
            TODO();
        }
    }
    paths_.clear();
    paths_.push_back(std::move(initPath));
}

void ProgramState::step(const Stmt *stmt) {
    if (!stmt)
        return;
    TypeSwitch<const Stmt *, void>(stmt)
        .Case<CompoundStmt>([this](const CompoundStmt *cs) {
            DEBUG("stepping CompoundStmt...");
            for (const Stmt *child : cs->children()) {
                if (child)
                    step(child);
            }
        })
        .Case<IfStmt>([this](const IfStmt *ifStmt) {
            DEBUG("stepping IfStmt...");
            vector<const Expr *> branchConds;
            vector<const Stmt *> branchStmts;
            branchConds.push_back(ifStmt->getCond());
            branchStmts.push_back(ifStmt->getThen());
            if (ifStmt->getElse())
                branchStmts.push_back(ifStmt->getElse());
            else
                branchStmts.push_back(nullptr);

            stepBranch(branchConds, branchStmts);
        })
        .Case<ReturnStmt>([this](const ReturnStmt *retStmt) {
            DEBUG("stepping ReturnStmt...");
            setReturnExpr(retStmt->getRetValue());
            setStates(Path::PathState::Return, NULL);
        })
        .Case<DeclStmt>([this](const DeclStmt *declStmt) {
            DEBUG("stepping DeclStmt...");
            vector<const VarDecl *> varDecls;
            for (auto it = declStmt->decl_begin(); it != declStmt->decl_end(); ++it) {
                Decl *decl = *it;
                if (!isa<VarDecl>(decl))
                    UNIMPLEMENT("Unhandled Decl type: "s + decl->getDeclKindName());
                varDecls.push_back(dyn_cast<VarDecl>(decl));
            }
            addNewDecls(varDecls);
        })
        .Case<BinaryOperator>([this](const BinaryOperator *binOp) {
            DEBUG("stepping BinaryOperator...");
            if (ignoreTopBinop(binOp))
                return;
            if (!isAssignOp(binOp))
                UNIMPLEMENT("BinaryOperator not implemented: " << binOp->getOpcode());
            updateVarState(binOp);
        })
        .Case<Expr>([this](const Expr *expr) {
            DEBUG("stepping Expr...");
            stepExpr(expr);
        })
        .Case<ImplicitCastExpr>(
            [](const ImplicitCastExpr *) -> unique_ptr<SymbolicExpr> { UNREACHABLE(); })
        .Case<CaseStmt>([this](const CaseStmt *caseStmt) {
            DEBUG("stepping CaseStmt...");
            // Can only be met during step(SwitchStmt), just ignore it.
            step(caseStmt->getSubStmt());
        })
        .Case<DefaultStmt>([this](const DefaultStmt *defaultStmt) {
            DEBUG("stepping DefaultStmt...");
            // Can only be met during step(SwitchStmt), just ignore it.
            step(defaultStmt->getSubStmt());
        })
        .Case<SwitchStmt>([this](const SwitchStmt *switchStmt) {
            DEBUG("stepping SwitchStmt...");
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx    = switchStmt;

            if (switchStmt->hasInitStorage())
                UNIMPLEMENT(
                    "Unsupported Switch type, Cond has init statement: " << switchStmt->getCond());
            if (auto bodyStmt = dyn_cast_if_present<CompoundStmt>(switchStmt->getBody())) {
                if (isa_and_present<CaseStmt>(bodyStmt->body_front())) {
                    stepSimpleSwitch(switchStmt);
                } else {
                    UNIMPLEMENT("Unsupported Switch type, body's first Stmt is not CaseStmt: "
                                << switchStmt->getBody());
                }
            } else {
                WARN("A SwtichStmt without CompoundStmt body (why?) has been ignored: "
                     << switchStmt);
            }

            resetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<ForStmt>([this](const ForStmt *forStmt) {
            DEBUG("stepping ForStmt...");
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx    = forStmt;
            stepLoop(forStmt);
            resetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<WhileStmt>([this](const WhileStmt *whileStmt) {
            DEBUG("stepping WhileStmt...");
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx    = whileStmt;
            stepLoop(whileStmt);
            resetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<DoStmt>([this](const DoStmt *doStmt) {
            DEBUG("stepping DoStmt...");
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx    = doStmt;
            stepLoop(doStmt);
            resetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<CXXForRangeStmt>([this](const CXXForRangeStmt *rangeStmt) {
            DEBUG("stepping CXXForRangeStmt...");
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx    = rangeStmt;
            stepLoop(rangeStmt);
            resetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<BreakStmt>([this](const BreakStmt *) {
            DEBUG("stepping BreakStmt...");
            setStates(Path::PathState::Break, StmtCtx);
        })
        .Case<ContinueStmt>([](const ContinueStmt *) {
            DEBUG("stepping ContinueStmt...");
            TODO();
        })
        .Case<NullStmt>([](const NullStmt *) { DEBUG("stepping NullStmt..."); })
        // .Case<UnaryExprOrTypeTraitExpr>([this](const UnaryExprOrTypeTraitExpr *u) -> EvalResult {
        //     SymbolicExpr::Type resultTy = deriveVarType(u->getType());
        //     QualType argTy =
        //         u->isArgumentType() ? u->getArgumentType() : u->getArgumentExpr()->getType();
        //     if (argTy->isVariableArrayType())
        //         UNIMPLEMENT("VLA in sizeof/alignof");

        //     uint64_t value = 0;
        //     switch (u->getKind()) {
        //         case UETT_SizeOf:
        //             value =
        //                 static_cast<uint64_t>(this->getContext().get(argTy).getQuantity());
        //             break;
        //         case UETT_AlignOf:
        //             value =
        //                 static_cast<uint64_t>(astContext_.getTypeAlignInChars(argTy).getQuantity());
        //             break;
        //         case UETT_PreferredAlignOf:
        //             value = static_cast<uint64_t>(
        //                 astContext_.getPreferredTypeAlignInChars(argTy).getQuantity());
        //             break;
        //         case UETT_VecStep: {
        //             if (auto vec = argTy->getAs<VectorType>())
        //                 value = static_cast<uint64_t>(vec->getNumElements());
        //             else if (auto ext = argTy->getAs<ExtVectorType>())
        //                 value = static_cast<uint64_t>(ext->getNumElements());
        //             else
        //                 UNIMPLEMENT("vec_step on non-vector");
        //             break;
        //         }
        //         default: UNIMPLEMENT("unsupported unary type trait");
        //     }

        //     auto lit = std::make_unique<LiteralExpr>(static_cast<uint64_t>(value));
        //     lit->setValType(resultTy);
        //     EvalResult R;
        //     R.second.emplace_back(std::move(lit));
        //     return R;
        // })
        .Default(
            [](const Stmt *s) { UNIMPLEMENT("Unsupported Stmt type: " << s->getStmtClassName()); });
    return;
}

Formulas ProgramState::stepExpr(const Expr *expr) {
    std::vector<std::unique_ptr<Path>> updatedPaths;
    Formulas evaluated;

    for (auto &path : paths_) {
        auto [newPathGroup, exprGroup] = path->evalExpr(expr);

        updatedPaths.push_back(std::move(path));
        evaluated.push_back(std::move(exprGroup[0]));

        for (size_t i = 0; i < newPathGroup.size(); ++i) {
            updatedPaths.push_back(std::move(newPathGroup[i]));
            evaluated.push_back(std::move(exprGroup[i + 1]));
        }
    }

    paths_ = std::move(updatedPaths);
    return evaluated;
}

void ProgramState::stepBranch(const vector<const Expr *> &branchConds,
                              const vector<const Stmt *> &branchStmts) {
    assert(branchStmts.size() == branchConds.size() + 1);

    vector<unique_ptr<ProgramState>> clones;
    vector<const ProgramState *> statesForMerge;

    auto splitPair = splitActiveInactive();
    size_t n       = branchConds.size();
    for (size_t i = 0; i < n; ++i) {
        auto newState = splitPair.first->clone();

        vector<unique_ptr<Path>> updatedPaths;

        for (auto &path : newState->paths_) {
            Path::EvalResult eval = path->evalExpr(branchConds[i]);

            size_t m = eval.second.size();
            for (size_t j = 0; j < m; ++j) {
                unique_ptr<Path> newPath =
                    (j == 0) ? std::move(path) : std::move(eval.first[j - 1]);

                newPath->insertPathCondition(std::move(eval.second[j]));
                updatedPaths.push_back(std::move(newPath));
            }
        }

        newState->paths_ = std::move(updatedPaths);

        newState->step(branchStmts[i]);

        statesForMerge.push_back(newState.get());
        clones.push_back(std::move(newState));
    }

    queue<pair<unique_ptr<Path>, size_t>> worklist;
    vector<unique_ptr<Path>> finalPaths;

    for (auto &path : splitPair.first->paths_) {
        worklist.emplace(std::move(path), 0);
    }

    while (!worklist.empty()) {
        auto [path, idx] = std::move(worklist.front());
        worklist.pop();

        if (idx == branchConds.size()) {
            finalPaths.push_back(std::move(path));
            continue;
        }

        Path::EvalResult eval = path->evalExpr(branchConds[idx]);

        for (size_t j = 0; j < eval.second.size(); ++j) {
            unique_ptr<Path> newPath = (j == 0) ? std::move(path) : std::move(eval.first[j - 1]);

            newPath->insertPathCondition(createLNotExpr(std::move(eval.second[j])));
            worklist.emplace(std::move(newPath), idx + 1);
        }
    }

    splitPair.first->paths_ = std::move(finalPaths);
    splitPair.first->step(branchStmts.back());

    statesForMerge.push_back(splitPair.first.get());
    auto mergedActive = merge(statesForMerge);
    for (auto &path : splitPair.second->paths_)
        mergedActive->paths_.push_back(std::move(path));

    paths_ = std::move(mergedActive->paths_);
}

void ProgramState::stepLoop(const Stmt *loopStmt) {
    auto &preState = *this;
    auto loopEntry = preState.clone();
    if (auto forLoop = dyn_cast<ForStmt>(loopStmt); forLoop && forLoop->getInit())
        loopEntry->step(forLoop->getInit());

    const Expr *cond = nullptr;
    const Stmt *inc  = nullptr;
    const Stmt *body = nullptr;

    if (const auto *forStmt = dyn_cast<ForStmt>(loopStmt)) {
        cond = forStmt->getCond();
        inc  = forStmt->getInc();
        body = forStmt->getBody();
    } else if (const auto *whileStmt = dyn_cast<WhileStmt>(loopStmt)) {
        cond = whileStmt->getCond();
        body = whileStmt->getBody();
    } else {
        UNIMPLEMENT("Loop type not supported yet: " << loopStmt->getStmtClassName());
    }

    auto loopInfo = parseLoopInfo(preState, *loopEntry, cond, inc, body);

    string spec;
    vector<unique_ptr<Path>> invs;
    if (loopInfo == nullopt) {
        // Note: Dangerous! Change this.
        auto symbolLoopEntry = clone();
        symbolLoopEntry->resymbolize();
        loopInfo = LoopInfo{std::move(symbolLoopEntry)};
        tie(spec, invs) =
            emitLoopInvariant(preState, *loopEntry, cond, inc, body, *loopInfo, "ComplexLoop");
    } else {
        tie(spec, invs) = emitLoopInvariant(preState, *loopEntry, cond, inc, body, *loopInfo);
    }
    INFO(spec);

    auto beginLoc = loopStmt->getSourceRange().getBegin();
    GlobalSM::getRewriter().InsertText(beginLoc, spec, /*after*/ false,
                                       /*indentNewLines*/ true);

    this->paths_ = std::move(invs);
    INFO(this->dump());
}

void ProgramState::setStates(Path::PathState state, const Stmt *stmt) {
    for (auto &pathPtr : paths_) {
        if (pathPtr->isActive()) {
            pathPtr->setPathState(state);
            pathPtr->StmtCtx = stmt;
        }
    }
}

void ProgramState::setReturnExpr(const Expr *expr) {
    if (expr == nullptr) {
        for (auto &pathPtr : paths_) {
            if (!pathPtr->isActive())
                continue;
            pathPtr->setReturnExpr(nullptr);
        }
        return;
    }

    vector<unique_ptr<Path>> updatedPaths;

    for (auto &pathPtr : paths_) {
        if (!pathPtr->isActive()) {
            updatedPaths.emplace_back(std::move(pathPtr));
            continue;
        }

        Path::EvalResult eval                    = pathPtr->evalExpr(expr);
        vector<unique_ptr<Path>> &generatedPaths = eval.first;
        Formulas &results                        = eval.second;

        size_t n = results.size();
        for (size_t i = 0; i < n; ++i) {
            unique_ptr<Path> newPath;
            if (i == 0) {
                newPath = std::move(pathPtr);
            } else {
                newPath = std::move(generatedPaths[i - 1]);
            }

            newPath->setReturnExpr(std::move(results[i]));
            updatedPaths.emplace_back(std::move(newPath));
        }
    }

    paths_ = std::move(updatedPaths);
}

void ProgramState::updateVarState(const BinaryOperator *binOp) {
    vector<unique_ptr<Path>> updatedPaths;

    for (auto &path : paths_) {
        auto lvalue = path->extractLValue(binOp->getLHS());
        if (!path->isActive()) {
            updatedPaths.push_back(std::move(path));
            continue;
        }

        Path::EvalResult eval;

        if (binOp->isCompoundAssignmentOp()) {
            BinaryOpExpr::Operator op = getCompoundAssignOp(binOp->getOpcode());

            Path::EvalResult lhs = path->evalExpr(binOp->getLHS());

            vector<unique_ptr<Path>> outPaths;
            Formulas outExprs;

            for (size_t i = 0; i < lhs.second.size(); ++i) {
                Path *lhsPath = (i == 0) ? path.get() : lhs.first[i - 1].get();

                Path::EvalResult rhs = lhsPath->evalExpr(binOp->getRHS());

                for (size_t j = 0; j < rhs.second.size(); ++j) {
                    outExprs.emplace_back(make_unique<BinaryOpExpr>(lhs.second[i]->clone(), op,
                                                                    std::move(rhs.second[j])));

                    if (i != 0 || j != 0)
                        outPaths.emplace_back(std::move(rhs.first[j - 1]));
                }
            }
            eval = {std::move(outPaths), std::move(outExprs)};
        } else {
            eval = path->evalExpr(binOp->getRHS());
        }

        size_t n = eval.second.size();
        for (size_t i = 0; i < n; ++i) {
            unique_ptr<Path> newPath = (i == 0) ? std::move(path) : std::move(eval.first[i - 1]);
            if (holds_alternative<const VarDecl *>(lvalue)) {
                auto *var = get<const VarDecl *>(lvalue);
                newPath->updateVarState(var, std::move(eval.second[i]));
            } else if (holds_alternative<unique_ptr<Address>>(lvalue)) {
                auto &addrUptr = get<unique_ptr<Address>>(lvalue);
                Address *addr  = addrUptr.get();
                newPath->updateMemory(addr, std::move(eval.second[i]));
            } else
                UNREACHABLE();

            updatedPaths.push_back(std::move(newPath));
        }
    }

    paths_ = std::move(updatedPaths);
}

void ProgramState::addNewDecls(const vector<const VarDecl *> &varDecls) {
    for (const VarDecl *varDecl : varDecls) {
        const Expr *initExpr = varDecl->getInit();
        vector<unique_ptr<Path>> updatedPaths;

        for (auto &path : paths_) {
            if (!path->isActive()) {
                updatedPaths.push_back(std::move(path));
                continue;
            }

            auto varAddr_ = path->allocMemory(varDecl);

            if (initExpr == nullptr) {
                // TODO: may add a class named "Unknown" derived from Variable.
                WARN("Uninitialized variable " + varDecl->getNameAsString());

                auto varType = varDecl->getType();
                if (auto RD = varType->getAsRecordDecl();
                    RD != nullptr && varType->isStructureType()) {
                    // If varDecl is a struct, memoryState_ should map an incomplete Structure (with
                    // no field values initialized).
                    if (!RD->isCompleteDefinition())
                        ERROR("Struct with incomplete definition!");

                    RD      = RD->getDefinition();
                    auto st = make_unique<Structure>(path->getNextStructureId(), RD,
                                                     RD->getASTContext().getASTRecordLayout(RD),
                                                     make_unique<Address>(*varAddr_));
                    path->updateVarState(varDecl, std::move(st));
                }

                updatedPaths.push_back(std::move(path));
                continue;
            }
            if (auto initListExpr = dyn_cast<InitListExpr>(initExpr)) {
                auto varType = varDecl->getType();

                if (varType->isAnyPointerType() || varType->isArrayType()) {
                    TODO();
                } else if (auto RD = varType->getAsRecordDecl();
                           RD != nullptr && varType->isStructureType()) {
                    if (!RD->isCompleteDefinition())
                        ERROR("Struct with incomplete definition!");

                    RD      = RD->getDefinition();
                    auto st = make_unique<Structure>(path->getNextStructureId(), RD,
                                                     RD->getASTContext().getASTRecordLayout(RD),
                                                     make_unique<Address>(*varAddr_));
                    if (initListExpr->getNumInits() != st->getNumFields())
                        ERROR("Initializer list size mismatches the struct's field count.");
                    std::ranges::transform(
                        initListExpr->inits(), st->fieldsValues().begin(), [&](const auto &init) {
                            Path::EvalResult eval = path->evalExpr(init);

                            if (eval.second.size() != 1)
                                UNIMPLEMENT(
                                    "No control flow branching permitted within an initializer "
                                    "list now.");
                            return std::move(eval.second[0]);
                        });
                    path->updateVarState(varDecl, std::move(st));
                    updatedPaths.push_back(std::move(path));
                    continue;
                } else {
                    UNIMPLEMENT("An initializer list was used to initialize an unimplemented or "
                                "incorrect type "s +
                                varType->getTypeClassName() + ".");
                }
            } else {
                Path::EvalResult eval = path->evalExpr(initExpr);

                size_t n = eval.second.size();
                for (size_t i = 0; i < n; ++i) {
                    unique_ptr<Path> newPath =
                        (i == 0) ? std::move(path) : std::move(eval.first[i - 1]);

                    newPath->updateVarState(varDecl, std::move(eval.second[i]));
                    updatedPaths.push_back(std::move(newPath));
                }
                continue;
            }
        }

        paths_ = std::move(updatedPaths);
    }
}

pair<unique_ptr<ProgramState>, unique_ptr<ProgramState>> ProgramState::splitActiveInactive() {
    auto activeState   = make_unique<ProgramState>(context_->clone());
    auto inactiveState = make_unique<ProgramState>(context_->clone());

    for (auto &path : paths_) {
        if (path->isActive())
            activeState->paths_.push_back(std::move(path));
        else
            inactiveState->paths_.push_back(std::move(path));
    }
    paths_.clear();
    return {std::move(activeState), std::move(inactiveState)};
}

unique_ptr<ProgramState> ProgramState::merge(const vector<const ProgramState *> &states) {
    if (states.size() == 0)
        ERROR("Nothing to be merged.");
    auto merged = make_unique<ProgramState>(states[0]->getContext()->clone());

    for (auto &state : states) {
        if (*state->getContext() != *merged->getContext())
            ERROR("States to be merged have different context.");
        for (const auto &path : state->paths_)
            merged->paths_.push_back(path->clone());
    }
    return merged;
}

unique_ptr<ProgramState> ProgramState::merge(const vector<unique_ptr<ProgramState>> &states) {
    if (states.size() == 0)
        ERROR("Nothing to be merged.");
    auto merged = make_unique<ProgramState>(states[0]->getContext()->clone());

    for (auto &state : states) {
        if (*state->getContext() != *merged->getContext())
            ERROR("States to be merged have different context.");
        for (const auto &path : state->paths_)
            merged->paths_.push_back(path->clone());
    }
    return merged;
}

unique_ptr<ProgramState> ProgramState::clone() const {
    auto newState = make_unique<ProgramState>(context_->clone());

    for (const auto &path : paths_) {
        if (path)
            newState->paths_.push_back(path->clone());
    }

    newState->StmtCtx = StmtCtx;
    return newState;
}

unique_ptr<ProgramState> ProgramState::cloneWithPaths(vector<unique_ptr<Path>> &newPaths) const {
    auto clone    = this->clone();
    clone->paths_ = std::move(newPaths);
    return clone;
}

void ProgramState::resetState() {
    for (auto &path : paths_) {
        if (!path->isActive() && (path->StmtCtx && path->StmtCtx == this->StmtCtx))
            path->setPathState(Path::PathState::Step);
    }
}
static void collectCaseBlocks(const CompoundStmt *body,
                              vector<vector<const Stmt *>> &blocks,
                              vector<const Expr *> &conds) {
    if (!body)
        ERROR("dyn_cast failed for switch body.");
    blocks.clear();
    conds.clear();

    for (const Stmt *top : body->body()) {
        if (const CaseStmt *cs = dyn_cast<CaseStmt>(top)) {
            const CaseStmt *cur = cs;
            while (cur) {
                blocks.emplace_back();
                conds.push_back(cur->getLHS());
                const Stmt *sub = cur->getSubStmt();
                if (const CaseStmt *next = dyn_cast<CaseStmt>(sub)) {
                    cur = next;
                } else {
                    if (sub) {
                        for (size_t i = 0; i < blocks.size(); ++i)
                            blocks[i].push_back(sub);
                    }
                    break;
                }
            }
        } else if (const DefaultStmt *ds = dyn_cast<DefaultStmt>(top)) {
            blocks.emplace_back();
            conds.push_back(nullptr);
            const Stmt *sub = ds->getSubStmt();
            if (sub) {
                for (size_t i = 0; i < blocks.size(); ++i)
                    blocks[i].push_back(sub);
            }
        } else {
            for (size_t i = 0; i < blocks.size(); ++i)
                blocks[i].push_back(top);
        }
    }
}

vector<pair<unique_ptr<ProgramState>, unique_ptr<SymbolicExpr>>> ProgramState::splitStateBySwitchCond(
    const Expr *switchCond) {
    if (!switchCond) {
        TODO();
    }
    vector<pair<unique_ptr<ProgramState>, unique_ptr<SymbolicExpr>>> result;

    for (auto &path : paths_) {
        auto evalResult = path->evalExpr(switchCond);

        if (evalResult.first.size() != 0) {
            ERROR("evalExpr produced unexpected side paths");
        }

        vector<unique_ptr<Path>> onePath;
        onePath.push_back(std::move(path));
        auto stateClone = cloneWithPaths(onePath);

        result.emplace_back(std::move(stateClone), std::move(evalResult.second[0]));
    }

    return result;
}

void ProgramState::stepSimpleSwitch(const SwitchStmt *switchStmt) {
    auto partitions = splitStateBySwitchCond(switchStmt->getCond());
    vector<vector<const Stmt *>> blocks;
    vector<const Expr *> conds;
    collectCaseBlocks(dyn_cast<CompoundStmt>(switchStmt->getBody()), blocks, conds);

    vector<unique_ptr<ProgramState>> finalStates;
    for (auto &pr : partitions) {
        auto current  = std::move(pr.first);
        auto symValue = std::move(pr.second);
        for (size_t i = 0; i < blocks.size(); ++i) {
            auto &stmts    = blocks[i];
            auto *caseCond = conds[i];
            if (caseCond == nullptr) {
                for (auto *s : stmts)
                    current->step(s);
                if (current->isInactive())
                    break;
                continue;
            }

            // TODO: pack a static function in Path.
            Path tmpPath;
            auto caseCondEval = tmpPath.evalExpr(caseCond);
            assert(caseCondEval.second.size() == 1);

            auto caseSymExpr = std::move(caseCondEval.second[0]);
            auto eqState     = current->clone();

            auto condExprEq = make_unique<BinaryOpExpr>(
                symValue->clone(), BinaryOpExpr::Operator::Equal, caseSymExpr->clone());
            for (auto &p : eqState->paths_)
                p->insertPathCondition(condExprEq->clone());

            for (auto *s : stmts)
                eqState->step(s);

            auto condExprNe = make_unique<BinaryOpExpr>(
                symValue->clone(), BinaryOpExpr::Operator::NotEqual, std::move(caseSymExpr));
            for (auto &p : current->paths_)
                p->insertPathCondition(condExprNe->clone());

            if (eqState->isInactive()) {
                finalStates.push_back(std::move(eqState));
            } else {
                vector<const ProgramState *> mergeInputs;
                mergeInputs.push_back(eqState.get());
                mergeInputs.push_back(current.get());
                auto merged = merge(mergeInputs);
                current     = std::move(merged);
            }
        }

        finalStates.push_back(std::move(current));
    }

    vector<const ProgramState *> ptrs;
    ptrs.reserve(finalStates.size());
    for (auto &st : finalStates)
        ptrs.push_back(st.get());

    auto merged = merge(ptrs);
    paths_      = std::move(merged->paths_);
}

bool ProgramState::isInactive() const {
    for (const auto &p : paths_) {
        if (p->isActive()) {
            return false;
        }
    }
    return true;
}

void ProgramState::resymbolize() {
    for (auto &path : paths_) {
        if (path->isActive()) {
            auto temp = std::move(path);
            temp->resymbolize();
            paths_.clear();
            paths_.push_back(std::move(temp));
            return;
        }
    }
}

string ProgramState::dump() const {
    ostringstream oss;
    for (const auto &p : paths_) {
        oss << p->dump() << "\n";
    }
    return oss.str();
}