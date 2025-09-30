#include <queue>
#include <unordered_map>
#include <memory>
#include <set>
#include <variant>
#include <ranges>
#include <llvm/ADT/TypeSwitch.h>
#include <llvm/ADT/APSInt.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/Decl.h>
#include <clang/AST/StmtCXX.h>
#include <clang/AST/ParentMapContext.h>
#include "state.h"
#include "macros.h"
#include "Utils/utils.h"
#include "SpecGenerator/specGenerator.h"

using namespace std;
using namespace clang;
using namespace llvm;
using namespace Symbolic;

using LValueTarget = std::variant<const VarDecl *, std::unique_ptr<Address>, MemberTarget>;

MemberTarget::MemberTarget(const MemberTarget &other)
    : field(other.field), is_arrow(other.is_arrow) {
    if (other.base) {
        base = other.base->addressClone().into_underlying();
    }
}

MemberTarget &MemberTarget::operator=(const MemberTarget &other) {
    if (this != &other) {
        field    = other.field;
        is_arrow = other.is_arrow;
        if (other.base) {
            base = other.base->addressClone().into_underlying();
        } else {
            base.reset();
        }
    }
    return *this;
}

bool operator==(const MemberTarget &a, const MemberTarget &b) noexcept {
    if (a.field != b.field)
        return false;
    if (a.is_arrow != b.is_arrow)
        return false;
    const bool ahas = static_cast<bool>(a.base);
    const bool bhas = static_cast<bool>(b.base);
    if (ahas != bhas)
        return false;
    if (!ahas)
        return true;
    return *a.base == *b.base;
}

bool operator!=(const MemberTarget &a, const MemberTarget &b) noexcept { return !(a == b); }

namespace {
    static not_null<unique_ptr<SymbolicExpr>> getSymbol(
        QualType type,
        std::variant<std::monostate, not_null<std::unique_ptr<const Address>>> from) {
        if (type->isPointerType()) {
            return make_unique<SymbolAddress>(std::move(from));
        } else if (type->isArrayType()) {
            TODO();
        } else if (type->isStructureType()) {
            auto *RD = type->getAsRecordDecl();
            if (!RD || !RD->isCompleteDefinition())
                ERROR("Incomplete struct definition");
            RD           = RD->getDefinition();
            auto &layout = RD->getASTContext().getASTRecordLayout(RD);
            return make_unique<Structure>(RD, layout, std::move(from));

        } else {
            SymbolicExpr::Type vty = deriveVarType(type);
            return std::make_unique<Symbolic::Variable>(vty, std::move(from));
        }
    }
} // namespace

Path::Path(const Path &other, bool shallowCopy) : context_(other.context_) {
    if (shallowCopy) {
        for (const auto &cond : other.pathConditions_) {
            pathConditions_.push_back(cond->clone());
        }
        currentState_ = other.currentState_;
        if (other.returnExpr_)
            returnExpr_.emplace(other.returnExpr_.value()->clone().into_underlying());
        else
            returnExpr_ = std::nullopt;
    } else {
        TODO();
    }
}

void Path::swap(Path &o) noexcept {
    using std::swap;
    swap(currentState_, o.currentState_);
    swap(returnExpr_, o.returnExpr_);
    swap(pathConditions_, o.pathConditions_);
    swap(varAddr_, o.varAddr_);
    swap(memoryState_, o.memoryState_);
}

void Path::resymbolize() {
    memoryState_.clear();
    pathConditions_.clear();

    for (auto &[varDecl, addr] : varAddr_) {
        clang::QualType ty = varDecl->getType();

        auto symbol = getSymbol(ty, addr->addressClone().into_underlying());
        updateMemory(*addr, std::move(symbol));
    }
}

not_null<unique_ptr<Address>> Path::extractLValue(const Expr *lhs) {
    auto lexpr = lhs->IgnoreParenImpCasts();
    if (auto declRef = dyn_cast<DeclRefExpr>(lexpr)) {
        if (auto varDecl = llvm::dyn_cast<clang::VarDecl>(declRef->getDecl())) {
            if (auto it = varAddr_.find(varDecl->getCanonicalDecl()); it != varAddr_.end()) {
                return make_unique<VariableAddress>(*it->second);
            } else {
                ERROR("varState has no ArraySubscriptExpr's base, undefined variable?");
            }
        }
        ERROR("Not a varDecl Ref.");
    }

    if (auto *arr = dyn_cast<ArraySubscriptExpr>(lexpr)) {
        auto baseAddr = extractLValue(arr->getBase());
        if (const auto symbol = memoryState_.read(*baseAddr)) {
            auto symbolAddr = dynamic_cast<const SymbolAddress *>(symbol.value().get());
            if (symbolAddr == nullptr)
                ERROR("Value of ArraySubscriptExpr's base is not 'SymbolAddress', base is "
                      "neither pointer nor array?");
            auto resultAddr = make_unique<SymbolAddress>(*symbolAddr);
            auto idxEval    = evalExpr(arr->getIdx());
            if (idxEval.second.size() != 1)
                ERROR("This location does not support control flow branches.");
            auto idxExpr = std::move(idxEval.second[0]);
            resultAddr->addOffset(std::move(idxExpr));
            if (!memoryState_.contains(*resultAddr)) {
                auto newSymbol =
                    getSymbol(arr->getType(), resultAddr->addressClone().into_underlying());
                memoryState_.write(*resultAddr, std::move(newSymbol));
            }
            return std::move(resultAddr);
        } else {
            ERROR("memoryState_ has no ArraySubscriptExpr's base, base is neither pointer nor "
                  "array?");
        }
    }

    if (auto *uop = dyn_cast<UnaryOperator>(lexpr)) {
        if (uop->getOpcode() == UO_Deref) {
            auto addrEval = evalExpr(uop->getSubExpr());
            if (addrEval.second.size() != 1)
                TODO();

            auto addrExpr = std::move(addrEval.second[0]);
            if (auto addr = addrExpr->tryEvalAsSymbolAddr()) {
                if (!memoryState_.contains(*addr.value())) {
                    auto symbol =
                        getSymbol(uop->getType(), addr.value()->addressClone().into_underlying());
                    memoryState_.write(*addr.value(), std::move(symbol));
                }
                return std::move(addr).value().into_underlying();
            } else {
                ERROR("Expected Address in deref, got: " << addrExpr->dump());
            }
        }
    }

    if (auto *mem = dyn_cast<MemberExpr>(lexpr)) {
        auto base = mem->getBase()->IgnoreParenImpCasts();
        auto FD   = llvm::dyn_cast<clang::FieldDecl>(mem->getMemberDecl());
        if (FD == nullptr)
            ERROR("RHS of memberExpr is not a `FieldDecl`?");
        if (FD->getParent() == nullptr)
            ERROR("No parent!");
        auto RD      = FD->getParent();
        auto &layout = RD->getASTContext().getASTRecordLayout(RD);

        if (mem->isArrow()) {
            auto addrEval = evalExpr(base);
            if (addrEval.second.size() != 1)
                ERROR("This location does not support control flow branches.");
            auto baseExpr = std::move(addrEval.second[0]);
            auto baseAddr = baseExpr->tryEvalAsSymbolAddr();
            if (baseAddr == nullopt)
                ERROR("Expected Address for '->' base, got: " << baseExpr->dump());

            if (!memoryState_.contains(*baseAddr.value())) {
                auto st = make_unique<Structure>(
                    RD, layout, baseAddr.value()->addressClone().into_underlying());
                memoryState_.write(*baseAddr.value(), std::move(st));
            }
            return make_unique<FieldAddress>(
                RD, pair{baseAddr.value()->addressClone().into_underlying(), FD->getFieldIndex()});
        } else {
            auto baseAddr = extractLValue(base);

            if (!memoryState_.contains(*baseAddr)) {
                auto st =
                    make_unique<Structure>(RD, layout, baseAddr->addressClone().into_underlying());
                memoryState_.write(*baseAddr, std::move(st));
            }
            return make_unique<FieldAddress>(
                RD, pair{baseAddr->addressClone().into_underlying(), FD->getFieldIndex()});
        }
    }

    UNIMPLEMENT("Unsupported LHS expression: " << lexpr->getStmtClassName());
}

not_null<unique_ptr<SymbolicExpr>> Path::getVarState(const VarDecl *var) const {
    auto canonicalVar = var->getCanonicalDecl();
    auto varIt        = varAddr_.find(canonicalVar);
    if (varIt == varAddr_.end())
        ERROR("Variable '" + canonicalVar->getNameAsString() + "' has no allocated address");
    auto addr  = varIt->second.get().get();
    auto value = memoryState_.read(*addr);
    if (value == nullopt)
        ERROR("Variable '" + canonicalVar->getNameAsString() +
              "' has no memory state entry for allocated address");
    return value.value()->clone();
}

const Formulas &Path::getPathConditions() const { return pathConditions_; }

not_null<VariableAddress *> Path::allocMemory(const VarDecl *var) {
    auto canonicalVar = var->getCanonicalDecl();
    if (varAddr_.contains(canonicalVar)) {
        // All `VariableAddress` built from same canonicalVar are same.
        return varAddr_.at(canonicalVar).get().get();
    }
    auto newAddr = make_unique<VariableAddress>(var);
    auto rawPtr  = newAddr.get();
    varAddr_.emplace(canonicalVar, std::move(newAddr));

    // Prevent uninitialized variables.
    memoryState_.write(*rawPtr, UnknownExpr::makeUnknown().into_underlying());
    return rawPtr;
}

void Path::updateMemory(const Address &addr, not_null<unique_ptr<SymbolicExpr>> expr) {
    memoryState_.write(addr, std::move(expr).into_underlying());
}

void Path::updateVarState(not_null<const VarDecl *> var, not_null<unique_ptr<SymbolicExpr>> expr) {
    auto canonicalVar = var->getCanonicalDecl();
    auto addrIt       = varAddr_.find(canonicalVar);
    if (addrIt == varAddr_.end())
        ERROR("Variable has no allocated address");

    auto &addr = addrIt->second;
    memoryState_.write(*addr, std::move(expr).into_underlying());
}

void Path::insertPathCondition(not_null<unique_ptr<SymbolicExpr>> cond) {
    pathConditions_.push_back(std::move(cond));
}

unique_ptr<Path> Path::clone() const {
    auto cloned           = make_unique<Path>(context_);
    cloned->currentState_ = currentState_;
    for (const auto &entry : varAddr_)
        cloned->varAddr_.emplace(entry.first, make_unique<VariableAddress>(*entry.second));
    cloned->memoryState_ = memoryState_;
    for (const auto &cond : pathConditions_)
        cloned->pathConditions_.push_back(cond->clone());
    if (returnExpr_)
        cloned->returnExpr_.emplace(returnExpr_.value()->clone().into_underlying());
    else
        cloned->returnExpr_ = nullopt;
    cloned->StmtCtx = StmtCtx;
    return cloned;
}

struct CallArgs {
    std::unique_ptr<Path> path;
    std::vector<std::unique_ptr<SymbolicExpr>> args;
};

void bindParams(Path *calleePath,
                const FunctionDecl *FD,
                const std::vector<std::unique_ptr<SymbolicExpr>> &args) {
    const unsigned n = FD->getNumParams();
    assert(args.size() == n);

    for (unsigned i = 0; i < n; ++i) {
        const ParmVarDecl *param = FD->getParamDecl(i);
        QualType T               = param->getType();

        auto slot = calleePath->allocMemory(param);

        if (T->isPointerType()) {
            auto m = args[i]->tryEvalAsSymbolAddr();
            if (!m)
                ERROR("pointer parameter expects address-like argument");
            calleePath->updateMemory(*slot, m.value()->addressClone().into_underlying());
        } else if (T->isStructureType()) {
            calleePath->updateMemory(*slot, args[i]->clone());
        } else if (T->isArrayType()) {
            UNIMPLEMENT("array parameter");
        } else {
            calleePath->updateMemory(*slot, args[i]->clone());
        }
    }
}

static std::vector<CallArgs> evalCallArgs(Path *basePath, const CallExpr *call) {
    std::vector<CallArgs> args;
    args.push_back({nullptr, {}});

    const unsigned n = call->getNumArgs();
    for (unsigned i = 0; i < n; ++i) {
        const Expr *arg = call->getArg(i);
        std::vector<CallArgs> next;
        for (auto &evalArg : args) {
            Path *p = evalArg.path ? evalArg.path.get() : basePath;

            auto [newPaths, values] = p->evalExpr(arg);
            for (size_t j = 0; j < values.size(); ++j) {
                CallArgs nc;
                if (j == 0)
                    nc.path = std::move(evalArg.path);
                else
                    nc.path = std::move(newPaths[j - 1]).into_underlying();
                nc.args.reserve(evalArg.args.size() + 1);
                for (auto &a : evalArg.args)
                    nc.args.emplace_back(a->clone().into_underlying());
                nc.args.emplace_back(std::move(values[j]).into_underlying());
                next.emplace_back(std::move(nc));
            }
        }
        args = std::move(next);
    }
    for (auto &c : args)
        if (!c.path)
            c.path = basePath->clone();
    return args;
}

Path::EvalResult Path::evalExpr(const Expr *expr) {
    if (!expr)
        ERROR("Fail to convert an empty Expr");

    EvalResult eval_result =
        TypeSwitch<const Expr *, EvalResult>(expr)
            .Case<IntegerLiteral>([](const IntegerLiteral *lit) -> EvalResult {
                DEBUG("evaluating IntegerLiteral...");
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

                Formulas exprs;
                exprs.reserve(1);
                exprs.push_back(std::move(result));

                return {vector<not_null<unique_ptr<Path>>>{}, std::move(exprs)};
            })
            .Case<BinaryOperator>([this](const BinaryOperator *binOp) -> EvalResult {
                DEBUG("evaluating BinaryOperator...");
                // TODO: maybe pack the logic in BO, ArraySub into a function?
                EvalResult lhs            = evalExpr(binOp->getLHS());
                BinaryOpExpr::Operator op = getBinaryOp(binOp->getOpcode());

                vector<not_null<unique_ptr<Path>>> outPaths;
                Formulas outExprs;

                size_t lhsCount = lhs.second.size();
                for (size_t i = 0; i < lhsCount; ++i) {
                    auto lhsExpr    = std::move(lhs.second[i]);
                    Path *path      = (i == 0) ? this : lhs.first[i - 1].get().get();
                    EvalResult rhs  = path->evalExpr(binOp->getRHS());
                    size_t rhsCount = rhs.second.size();

                    for (size_t j = 0; j < rhsCount; ++j) {
                        auto rhsExpr = std::move(rhs.second[j]);

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
                DEBUG("evaluating ParenExpr...");
                return evalExpr(paren->getSubExpr());
            })
            .Case<DeclRefExpr>([this](const DeclRefExpr *declRef) -> EvalResult {
                DEBUG("evaluating DeclRefExpr...");
                if (const auto *varDecl = dyn_cast<VarDecl>(declRef->getDecl())) {
                    auto varExpr = getVarState(varDecl);
                    Formulas exprs;
                    exprs.push_back(std::move(varExpr));
                    return {vector<not_null<unique_ptr<Path>>>{}, std::move(exprs)};
                }

                if (const auto *enumDecl = dyn_cast<EnumConstantDecl>(declRef->getDecl())) {
                    APSInt value = enumDecl->getInitVal();
                    auto litExpr = make_unique<LiteralExpr>(static_cast<int>(value.getSExtValue()));
                    Formulas exprs;
                    exprs.push_back(std::move(litExpr));
                    return {vector<not_null<unique_ptr<Path>>>{}, std::move(exprs)};
                }

                UNIMPLEMENT("Unsupported Decl type: " << declRef->getDecl()->getDeclKindName());
                return Path::EvalResult{};
            })
            .Case<ArraySubscriptExpr>([this](const ArraySubscriptExpr *arrSub) -> EvalResult {
                DEBUG("evaluating ArraySubscriptExpr...");
                auto variableAddr = extractLValue(arrSub->getBase());
                unique_ptr<SymbolAddress> addr;
                if (const auto symbol = memoryState_.read(*variableAddr)) {
                    auto ptr = dynamic_cast<const SymbolAddress *>(symbol.value().get());
                    if (ptr == nullptr)
                        ERROR("Value of ArraySubscriptExpr's base is not 'SymbolAddress', base is "
                              "neither "
                              "pointer nor "
                              "array?");
                    addr = make_unique<SymbolAddress>(*ptr);
                } else {
                    ERROR("memoryState_ has no ArraySubscriptExpr's base, base is neither pointer "
                          "nor "
                          "array?");
                }
                EvalResult idx = evalExpr(arrSub->getIdx());

                vector<not_null<unique_ptr<Path>>> outPaths;
                Formulas outExprs;

                for (size_t i = 0; i < idx.second.size(); ++i) {
                    auto idxExpr   = std::move(idx.second[i]);
                    string idxDump = idxExpr->dump();
                    auto newAddr   = make_unique<SymbolAddress>(*addr);
                    newAddr->setOffset(std::move(idxExpr));
                    if (auto value = memoryState_.read(*newAddr); value == nullopt) {
                        auto elemType = arrSub->getType();
                        auto symbol =
                            getSymbol(elemType, newAddr->addressClone().into_underlying());
                        memoryState_.write(*newAddr, symbol->clone());
                        outExprs.emplace_back(std::move(symbol));
                    } else {
                        outExprs.emplace_back(value.value()->clone());
                    }
                    if (i > 0)
                        outPaths.emplace_back(std::move(idx.first[i - 1]));
                }

                return {std::move(outPaths), std::move(outExprs)};
            })
            .Case<CallExpr>([this](const CallExpr *call) -> EvalResult {
                DEBUG("evaluating CallExpr...");
                const FunctionDecl *callee = call->getDirectCallee();
                if (!callee) {
                    Formulas exprs;
                    exprs.emplace_back(UnknownExpr::makeUnknown().into_underlying());
                    std::vector<not_null<std::unique_ptr<Path>>> empty;
                    return Path::EvalResult(std::move(empty), std::move(exprs));
                }
                static const set<string> ignoreNames = {"llvm.dbg.declare", "llvm.lifetime.start",
                                                        "llvm.lifetime.end", "printf",
                                                        "__assert_fail"};
                string name                          = callee->getNameAsString();
                if (ignoreNames.contains(name)) {
                    Formulas exprs;
                    exprs.emplace_back(UnknownExpr::makeUnknown().into_underlying());
                    std::vector<not_null<std::unique_ptr<Path>>> empty;
                    return Path::EvalResult(std::move(empty), std::move(exprs));
                }

                auto callArgs = evalCallArgs(this, call);

                std::vector<not_null<std::unique_ptr<Path>>> outPaths;
                Formulas outExprs;

                bool firstTaken = false;
                for (size_t k = 0; k < callArgs.size(); ++k) {
                    auto initPath = std::move(callArgs[k].path);
                    bindParams(initPath.get(), callee, callArgs[k].args);

                    auto func = std::make_unique<ACSLFunction>(callee);
                    ProgramState calleeState(std::move(initPath), std::move(func), context_);
                    calleeState.step(callee->getBody());

                    auto produced = calleeState.takeAllPaths();
                    for (size_t i = 0; i < produced.size(); ++i) {
                        auto p = std::move(produced[i]);
                        auto ret =
                            (p->getReturnExpr() ? p->getReturnExpr().value()->clone()
                                                : UnknownExpr::makeUnknown().into_underlying());

                        p->setPathState(PathState::Step);

                        if (!firstTaken) {
                            this->swap(*p);
                            outExprs.emplace_back(std::move(ret));
                            firstTaken = true;
                        } else {
                            outExprs.emplace_back(std::move(ret));
                            outPaths.emplace_back(std::move(p));
                        }
                    }
                }

                if (!firstTaken) {
                    outExprs.emplace_back(UnknownExpr::makeUnknown().into_underlying());
                }

                return {std::move(outPaths), std::move(outExprs)};
            })
            .Case<ConditionalOperator>([this](const ConditionalOperator *condOp) -> EvalResult {
                DEBUG("evaluating ConditionalOperator...");
                EvalResult cond = evalExpr(condOp->getCond());

                vector<not_null<unique_ptr<Path>>> outPaths;
                Formulas outExprs;

                for (size_t i = 0; i < cond.second.size(); ++i) {
                    Path *condPath = (i == 0) ? this : cond.first[i - 1].get().get();
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
                DEBUG("evaluating UnaryOperator...");
                auto operand = evalExpr(uop->getSubExpr());
                vector<not_null<unique_ptr<Path>>> outPaths;
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
                    Path *path  = (i == 0 ? this : operand.first[i - 1].get().get());
                    auto unExpr = std::move(operand.second[i]);

                    // Prevent misuse by not capturing this and operand.
                    [&path, &unExpr, &op, &outExprs, &uop]() {
                        if (op == UnaryOpExpr::Operator::PreInc ||
                            op == UnaryOpExpr::Operator::PostInc ||
                            op == UnaryOpExpr::Operator::PreDec ||
                            op == UnaryOpExpr::Operator::PostDec) {
                            // ++x / x++ / --x / x--
                            auto addr = path->extractLValue(uop->getSubExpr());
                            // old value
                            auto oldVal = path->memoryState_.read(*addr);
                            if (oldVal == nullopt)
                                ERROR("memoryState_ doesn't contain addr.");
                            // compute new = old +/- 1
                            auto one    = make_unique<LiteralExpr>(1);
                            auto binOp  = (op == UnaryOpExpr::Operator::PreInc ||
                                          op == UnaryOpExpr::Operator::PostInc)
                                              ? BinaryOpExpr::Operator::Add
                                              : BinaryOpExpr::Operator::Subtract;
                            auto newVal = make_unique<BinaryOpExpr>(oldVal.value()->clone(), binOp,
                                                                    std::move(one));
                            // return pre vs post
                            if (op == UnaryOpExpr::Operator::PreInc ||
                                op == UnaryOpExpr::Operator::PreDec)
                                outExprs.emplace_back(newVal->clone());
                            else {
                                outExprs.emplace_back(oldVal.value()->clone());
                            }
                            // Writing back first will cause oldVal to become dangling.
                            // write back
                            path->memoryState_.write(*addr, std::move(newVal));
                        } else if (op == UnaryOpExpr::Operator::Dereference) {
                            // *x
                            auto addr = unExpr->tryEvalAsSymbolAddr();
                            if (addr == nullopt)
                                ERROR("Expected Address, got: " << unExpr->dump());
                            if (auto value = path->memoryState_.read(*addr.value());
                                value == nullopt) {
                                auto symbol = getSymbol(
                                    uop->getType(), addr.value()->addressClone().into_underlying());
                                path->memoryState_.write(*addr.value(), symbol->clone());
                                outExprs.emplace_back(std::move(symbol));
                            } else {
                                outExprs.emplace_back(value.value()->clone());
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
                DEBUG("evaluating CastExpr...");
                auto sub = evalExpr(castExpr->getSubExpr());
                if (castExpr->getType()->isStructureType())
                    return sub;

                auto targetType = deriveVarType(castExpr->getType());

                for (auto &subExpr : sub.second)
                    subExpr->setValType(targetType);

                return {std::move(sub.first), std::move(sub.second)};
            })
            .Case<MemberExpr>([this](const MemberExpr *memberExpr) -> EvalResult {
                DEBUG("evaluating MemberExpr...");
                auto FD = dyn_cast_if_present<FieldDecl>(memberExpr->getMemberDecl());
                if (FD == nullptr)
                    UNREACHABLE();
                if (!FD->getParent())
                    UNREACHABLE();
                auto RD         = FD->getParent();
                auto &layout    = RD->getASTContext().getASTRecordLayout(RD);
                EvalResult base = evalExpr(memberExpr->getBase());
                if (base.second.size() != 1)
                    ERROR("No control flow branching permitted within a pointer-to-member "
                          "expression.");

                std::unique_ptr<Structure> st;
                auto baseExpr = std::move(base.second[0]).into_underlying();

                if (memberExpr->isArrow()) {
                    auto baseAddr = baseExpr->tryEvalAsSymbolAddr();
                    if (baseAddr == nullopt)
                        ERROR("LHS of '->' is not an address");
                    auto val = memoryState_.read(*baseAddr.value());
                    if (val == nullopt) {
                        st = make_unique<Structure>(
                            RD, layout, baseAddr.value()->addressClone().into_underlying());
                        memoryState_.write(*baseAddr.value(), st->clone());
                    } else if (val.value()->getType() == SymbolicExpr::ExprType::Structure) {
                        auto &stVal = dynamic_cast<Structure &>(*val.value());
                        st          = make_unique<Structure>(stVal);
                    } else {
                        ERROR("Dereferenced value is not a structure");
                    }
                } else {
                    if (baseExpr->getType() != SymbolicExpr::ExprType::Structure)
                        ERROR("LHS of '.' is not a structure");
                    st = std::unique_ptr<Structure>(static_cast<Structure *>(baseExpr.release()));
                }

                size_t idx = FD->getFieldIndex();
                auto slots = st->fieldsValues();
                if (idx >= slots.size())
                    UNREACHABLE();
                auto fieldValue = slots[idx]->clone();
                EvalResult result{};
                result.second.push_back(std::move(fieldValue));
                return result;
            })

            .Case<ConstantExpr>([this](const ConstantExpr *ce) -> EvalResult {
                DEBUG("evaluating ConstantExpr...");
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

    oss << "Return Expression: ";
    if (returnExpr_)
        oss << returnExpr_.value()->dump();
    else
        oss << "null";
    oss << "\n";

    oss << "Path Conditions:\n";
    for (size_t i = 0; i < pathConditions_.size(); ++i) {
        oss << "  [" << i << "]: " << pathConditions_[i]->dump() << "\n";
    }

    oss << "Variable Address Mapping:\n";
    for (auto &[varDecl, addr] : varAddr_) {
        string name;
        if (auto opt = context_.getDeclInfo(varDecl))
            tie(name, ignore, ignore, ignore, ignore) = *opt;

        oss << "  @" << name << " -> " << addr->dump();

        if (auto value = memoryState_.read(*addr)) {
            oss << " -> " << value.value()->dump();
        } else {
            oss << " -> null";
        }
        oss << "\n";
    }

    oss << "Memory State:\n";
    for (auto &&[addr, value] : memoryState_.flat()) {
        oss << "  " << addr.get().dump() << " -> " << value->dump() << "\n";
    }

    if (StmtCtx) {
        if (auto opt = context_.getStmtInfo(StmtCtx)) {
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
    if (value == nullopt)
        return true; // Assume it has not been accessed yet.

    return isFrom(addr, *value.value());
}

bool Path::is_point_to_structure(const Address &addr) const {
    auto opt = memoryState_.read(addr);
    if (!opt)
        return false;

    const SymbolicExpr *expr = opt.value().get();
    return expr->getType() == SymbolicExpr::ExprType::Structure;
}

MemoryModel::MemoryModel(const MemoryModel &other) {
    for (auto &[addr, value] : other.memoryMap_variableAddr_) {
        memoryMap_variableAddr_.emplace(addr, value->clone());
    }
    for (auto &[baseInfo, rangeValueMap] : other.memoryMap_constantRange_) {
        auto &mapToFill = memoryMap_constantRange_[baseInfo];
        for (auto &[range, value] : rangeValueMap) {
            mapToFill.emplace(range, value->clone());
        }
    }

    for (auto &[baseHash, addrValueMap] : other.memoryMap_symbolicRange_) {
        auto &mapToFill = memoryMap_symbolicRange_[baseHash];
        for (auto &[addr, value] : addrValueMap) {
            mapToFill.emplace(addr, value->clone());
        }
    }
}

MemoryModel &MemoryModel::operator=(const MemoryModel &other) {
    if (this == &other)
        return *this;

    clear();

    for (auto &[addr, value] : other.memoryMap_variableAddr_) {
        memoryMap_variableAddr_.emplace(addr, value->clone());
    }
    for (auto &[baseInfo, rangeValueMap] : other.memoryMap_constantRange_) {
        auto &mapToFill = memoryMap_constantRange_[baseInfo];
        for (auto &[range, value] : rangeValueMap) {
            mapToFill.emplace(range, value->clone());
        }
    }

    for (auto &[baseHash, addrValueMap] : other.memoryMap_symbolicRange_) {
        auto &mapToFill = memoryMap_symbolicRange_[baseHash];
        for (auto &[addr, value] : addrValueMap) {
            mapToFill.emplace(addr, value->clone());
        }
    }

    return *this;
}

optional<not_null<const SymbolicExpr *>> MemoryModel::read(const Address &addr) const {
    using enum Address::AddressType;
    if (addr.getAddressType() == VariableAddr) {
        auto &varAddr = dynamic_cast<const VariableAddress &>(addr);
        if (memoryMap_variableAddr_.contains(varAddr))
            return memoryMap_variableAddr_.at(varAddr).get().get();
        return nullopt;
    } else if (addr.getAddressType() == SymbolAddr) {
        auto symbolAddr = dynamic_cast<const SymbolAddress &>(addr);
        auto baseInfo   = symbolAddr.getBaseInfo();

        if (memoryMap_constantRange_.contains(baseInfo)) {
            auto offset = symbolAddr.getOffset();
            if (auto constOffset = offset->tryEvalAsConstant();
                constOffset && !symbolAddr.isRange()) {
                if (constOffset.value() < 0)
                    ERROR("Negetive offset.");
                auto unsignedOffset = static_cast<uint64_t>(constOffset.value());
                auto &rangeExprMap  = memoryMap_constantRange_.at(baseInfo);
                auto range          = pair{unsignedOffset, unsignedOffset + 1};
                auto rangeForSearch = pair{unsignedOffset, numeric_limits<uint64_t>::max()};
                auto upperBoundIt   = rangeExprMap.upper_bound(rangeForSearch);
                auto firstLEIt =
                    upperBoundIt == rangeExprMap.begin() ? rangeExprMap.end() : prev(upperBoundIt);
                if (firstLEIt == rangeExprMap.end() || firstLEIt->first.second <= unsignedOffset)
                    return nullopt;
                return firstLEIt->second.get().get();
            } else if (constOffset && symbolAddr.isRange() &&
                       symbolAddr.getLength()->tryEvalAsConstant()) {
                UNIMPLEMENT("There doesn't appear to be a need for constant-range range queries at "
                            "this time.");
            }
        }

        if (!memoryMap_symbolicRange_.contains(baseInfo))
            return nullopt;
        auto &addrValueMap = memoryMap_symbolicRange_.at(baseInfo);
        auto it            = addrValueMap.find(symbolAddr);
        if (it == addrValueMap.end())
            return nullopt;
        return it->second.get().get();
    } else if (addr.getAddressType() == FieldAddr) {
        auto fieldAddr = dynamic_cast<const FieldAddress &>(addr);
        return std::visit(
            [this](auto &&arg) -> optional<not_null<const SymbolicExpr *>> {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    TODO();
                } else if constexpr (std::is_same_v<
                                         T,
                                         std::pair<not_null<std::unique_ptr<const Symbolic::Address>>,
                                                   const size_t>>) {
                    auto &[baseAddr, index] = arg;
                    auto baseValue          = read(*baseAddr);
                    if (baseValue == nullopt)
                        return nullopt;
                    if (baseValue.value()->getType() != SymbolicExpr::ExprType::Structure)
                        ERROR("Value of address from a `fieldAddress` is not a structure.");
                    auto &baseSt = dynamic_cast<const Structure &>(*baseValue.value());
                    return baseSt.getFieldValue(index);
                }
            },
            fieldAddr.getFrom());
    }
    UNREACHABLE();
}

optional<not_null<SymbolicExpr *>> MemoryModel::read(const Address &addr) {
    auto value = std::as_const(*this).read(addr);
    if (value == nullopt)
        return nullopt;
    return const_cast<SymbolicExpr *>(value.value().get());
}

void MemoryModel::write(const Address &addr, not_null<unique_ptr<SymbolicExpr>> value) {
    using enum Address::AddressType;
    if (addr.getAddressType() == VariableAddr) {
        auto &varAddr = dynamic_cast<const VariableAddress &>(addr);
        memoryMap_variableAddr_.insert_or_assign(varAddr, std::move(value));
        return;
    } else if (addr.getAddressType() == SymbolAddr) {
        auto &symbolAddr = dynamic_cast<const SymbolAddress &>(addr);
        auto baseInfo    = symbolAddr.getBaseInfo();

        auto constOffset = symbolAddr.getOffset()->tryEvalAsConstant();
        auto constLen =
            symbolAddr.isRange() ? symbolAddr.getLength()->tryEvalAsConstant() : nullopt;

        if (constOffset && (!symbolAddr.isRange() || constLen)) {
            if (constOffset.value() < 0 || (constLen && constLen.value() <= 0))
                ERROR("Constant offset must be greater or equal to zero and length must be "
                      "greater than zero.");
            // constant range
            auto unsignedOffset = static_cast<uint64_t>(constOffset.value());
            auto unsignedLen    = constLen ? static_cast<uint64_t>(constLen.value()) : uint64_t{1};
            memoryMap_constantRange_.try_emplace(
                baseInfo, map<ConstRange, not_null<unique_ptr<SymbolicExpr>>>{});
            auto &rangeValueMap = memoryMap_constantRange_.at(baseInfo);
            auto range          = pair{unsignedOffset, unsignedOffset + unsignedLen};
            auto rangeForSearch = pair{unsignedOffset, numeric_limits<uint64_t>::max()};
            auto endIt          = rangeValueMap.end();
            auto upperBoundIt   = rangeValueMap.upper_bound(rangeForSearch);
            auto firstLEIt = upperBoundIt == rangeValueMap.begin() ? endIt : prev(upperBoundIt);

            auto &[range_leftBound, range_rightBound] = range;
            if (firstLEIt != endIt && firstLEIt->first.second > range.first) {
                auto &[firstLE_leftBound, firstLE_rightBound] = firstLEIt->first;
                auto &firstLE_value                           = firstLEIt->second;
                if (firstLE_leftBound < range_leftBound) {
                    auto leftRange = pair{firstLE_leftBound, range_leftBound};
                    rangeValueMap.insert_or_assign(leftRange, firstLE_value->clone());
                }
                if (firstLE_rightBound > range_rightBound) {
                    auto rightRange = pair{range_rightBound, firstLE_rightBound};
                    rangeValueMap.insert_or_assign(rightRange, firstLE_value->clone());
                }
                rangeValueMap.erase(firstLEIt);
            }
            if (upperBoundIt != endIt && upperBoundIt->first.first < range.second) {
                auto &[upperBound_leftBound, upperBound_rightBound] = upperBoundIt->first;
                auto &upperBound_value                              = upperBoundIt->second;
                if (upperBound_rightBound > range_rightBound) {
                    auto rightRange = pair{range_rightBound, upperBound_rightBound};
                    rangeValueMap.insert_or_assign(rightRange, upperBound_value->clone());
                }
                rangeValueMap.erase(upperBoundIt);
            }
            rangeValueMap.insert_or_assign(range, std::move(value));
            return;
        }
        // symbolic range
        auto &addrValueMap = memoryMap_symbolicRange_[baseInfo];
        addrValueMap.insert_or_assign(symbolAddr, std::move(value));
        return;
    } else if (addr.getAddressType() == FieldAddr) {
        auto fieldAddr = dynamic_cast<const FieldAddress &>(addr);
        std::visit(
            [&, this](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    TODO();
                } else if constexpr (std::is_same_v<
                                         T,
                                         std::pair<not_null<std::unique_ptr<const Symbolic::Address>>,
                                                   const size_t>>) {
                    auto &[baseAddr, index] = arg;
                    auto baseValue          = read(*baseAddr);
                    if (baseValue == nullopt)
                        ERROR("Structure isn't existed in MemoryModel, insert it first.");
                    if (baseValue.value()->getType() != SymbolicExpr::ExprType::Structure)
                        ERROR("Value of address from a `fieldAddress` is not a structure.");
                    auto &baseSt = dynamic_cast<Structure &>(*baseValue.value());
                    baseSt.setFieldValue(index, std::move(value));
                }
            },
            fieldAddr.getFrom());
        return;
    }
    UNREACHABLE();
}

bool MemoryModel::contains(const Address &addr) const { return read(addr) ? true : false; }

// MemoryModel::flat_view MemoryModel::flat() { return MemoryModel::flat_view{*this}; }
const MemoryModel::flat_view MemoryModel::flat() const {
    return MemoryModel::flat_view{const_cast<MemoryModel &>(*this)};
}

MemoryModel::KeySet MemoryModel::keys_flat() const {
    KeySet out;
    for (auto &&[addr, value] : this->flat()) {
        out.insert(addr);
    }
    return out;
}

// --- retain_only() ---
void MemoryModel::retain_only(const KeySet &keep) {
    /**
     * Variable addresses
     * Iterate over the variable-address map and remove all entries
     * whose keys are not present in the preserved set.
     */
    for (auto it = memoryMap_variableAddr_.begin(); it != memoryMap_variableAddr_.end();) {
        AddressBox key_as_box{it->first};
        if (keep.find(key_as_box) == keep.end()) {
            it = memoryMap_variableAddr_.erase(it);
        } else {
            ++it;
        }
    }

    /**
     * Constant ranges
     * Each entry in this map is a pair consisting of a base information object
     * and a set of constant ranges. For every range, we reconstruct the symbolic
     * address in a manner consistent with flat_view::compose_address. If the
     * reconstructed address is not present in the preserved set, the entry is
     * removed. Empty inner maps are eliminated to maintain consistency.
     */
    for (auto outer = memoryMap_constantRange_.begin(); outer != memoryMap_constantRange_.end();) {
        auto &inner_map = outer->second;

        for (auto inner = inner_map.begin(); inner != inner_map.end();) {
            const auto off        = inner->first.first;
            const auto offPlusLen = inner->first.second;
            const auto len        = offPlusLen - off;

            auto base_copy = outer->first; // Copy ensures stability of map keys
            std::unique_ptr<Address> composed;
            if (len == 0) {
                // Zero-length ranges are ill-formed; treated as deletions.
                composed = nullptr;
            } else if (len == 1) {
                composed = std::make_unique<SymbolAddress>(std::move(base_copy.from_),
                                                           std::make_unique<LiteralExpr>(off));
            } else {
                composed = std::make_unique<SymbolAddress>(std::move(base_copy.from_),
                                                           std::make_unique<LiteralExpr>(off),
                                                           std::make_unique<LiteralExpr>(len));
            }

            bool keep_this = false;
            if (composed) {
                AddressBox box{std::move(composed)};
                keep_this = (keep.find(box) != keep.end());
            }

            if (!keep_this) {
                inner = inner_map.erase(inner);
            } else {
                ++inner;
            }
        }

        if (inner_map.empty()) {
            outer = memoryMap_constantRange_.erase(outer);
        } else {
            ++outer;
        }
    }

    /**
     * Symbolic ranges
     * Symbolic ranges are keyed directly by SymbolAddress. Each such key is
     * converted into an AddressBox and compared against the preserved set.
     * Entries not in the preserved set are removed. Outer maps are erased
     * once their inner maps become empty.
     */
    for (auto outer = memoryMap_symbolicRange_.begin(); outer != memoryMap_symbolicRange_.end();) {
        auto &inner_map = outer->second;

        for (auto inner = inner_map.begin(); inner != inner_map.end();) {
            AddressBox key_as_box{inner->first};
            if (keep.find(key_as_box) == keep.end()) {
                inner = inner_map.erase(inner);
            } else {
                ++inner;
            }
        }

        if (inner_map.empty()) {
            outer = memoryMap_symbolicRange_.erase(outer);
        } else {
            ++outer;
        }
    }
}

void MemoryModel::eraseExpiredLocals(const unordered_set<const clang::VarDecl *> &localVars) {
    std::erase_if(memoryMap_variableAddr_, [&](auto const &kv) {
        auto fromRoot = kv.first.getFromRoot();
        if (fromRoot == nullopt)
            TODO();
        return localVars.contains(fromRoot.value());
    });
    std::erase_if(memoryMap_constantRange_, [&](auto const &kv) {
        auto fromRoot = kv.first.getFromRoot();
        if (fromRoot == nullopt)
            TODO();
        return localVars.contains(fromRoot.value());
    });
    std::erase_if(memoryMap_symbolicRange_, [&](auto const &kv) {
        auto fromRoot = kv.first.getFromRoot();
        if (fromRoot == nullopt)
            TODO();
        return localVars.contains(fromRoot.value());
    });
}

ProgramState::ProgramState(unique_ptr<Path> initialPath,
                           unique_ptr<ACSLFunction> func,
                           ACSLContext &context)
    : context_(context) {
    paths_.push_back(std::move(initialPath));
    func_ = std::move(func);
}

ProgramState::ProgramState(unique_ptr<ACSLFunction> func, ACSLContext &context)
    : context_(context) {
    func_ = std::move(func);
}

ProgramState::ProgramState(const ProgramState &other)
    : func_(other.func_->clone()), context_(other.context_),
      incompleteLoopInfo_(other.incompleteLoopInfo_) {
    paths_.reserve(other.paths_.size());
    std::ranges::transform(other.paths_, std::back_inserter(paths_),
                           [](auto &path) { return path->clone(); });
}

ProgramState &ProgramState::operator=(ProgramState &&other) {
    if (&context_ != &other.context_)
        ERROR("Different contexts!");
    func_               = std::move(other.func_);
    paths_              = std::move(other.paths_);
    incompleteLoopInfo_ = std::move(other.incompleteLoopInfo_);
    return *this;
}

namespace {
    static void initParam(Path *path, const ParmVarDecl *param) {
        QualType paramType = param->getType();

        auto paramAddr = path->allocMemory(param);
        auto value     = getSymbol(paramType, paramAddr->addressClone().into_underlying());
        path->updateMemory(*paramAddr, std::move(value));
    }

} // namespace

// @WindOctober: A preliminary scan of the function is also required to identify all
// global variables (i.e., variables whose scope is greater than or equal
// to the current function). These variables must then be either properly
// initialized or subjected to special handling.

void ProgramState::init() {
    auto FD       = func_->getFunctionDecl();
    auto initPath = std::make_unique<Path>(context_);
    for (const ParmVarDecl *param : FD->parameters()) {
        initParam(initPath.get(), param);
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

    auto localVars = collectLocalVars(stmt);
    for (auto &path : paths_) {
        auto &memoryState = path->getMutMemoryState();
        std::erase_if(path->varAddr_, [&](auto &&kv) { return localVars.contains(kv.first); });
        memoryState.eraseExpiredLocals(localVars);
    }
    return;
}

Formulas ProgramState::stepExpr(const Expr *expr) {
    std::vector<not_null<std::unique_ptr<Path>>> updatedPaths;
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

        vector<not_null<unique_ptr<Path>>> updatedPaths;

        for (auto &path : newState->paths_) {
            Path::EvalResult eval = path->evalExpr(branchConds[i]);

            size_t m = eval.second.size();
            for (size_t j = 0; j < m; ++j) {
                auto newPath = (j == 0) ? std::move(path) : std::move(eval.first[j - 1]);
                auto cond    = std::move(eval.second[j]);
                if (auto lit = cond->evalToConstExpr()) {
                    const bool isTrue = (lit->getLiteralValue() != 0);

                    if (!isTrue) {
                        continue;
                    } else {
                        updatedPaths.push_back(std::move(newPath));
                        continue;
                    }
                }

                newPath->insertPathCondition(std::move(cond));
                updatedPaths.push_back(std::move(newPath));
            }
        }

        newState->paths_ = std::move(updatedPaths);

        newState->step(branchStmts[i]);

        statesForMerge.push_back(newState.get());
        clones.push_back(std::move(newState));
    }

    queue<pair<not_null<unique_ptr<Path>>, size_t>> worklist;
    vector<not_null<unique_ptr<Path>>> finalPaths;

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
            auto newPath = (j == 0) ? std::move(path) : std::move(eval.first[j - 1]);
            auto cond    = std::move(eval.second[j]);

            if (auto lit = cond->evalToConstExpr()) {
                const bool isTrue = (lit->getLiteralValue() != 0);
                if (isTrue) {
                    continue;
                } else {
                    worklist.emplace(std::move(newPath), idx + 1);
                    continue;
                }
            }

            newPath->insertPathCondition(createLNotExpr(std::move(cond)));
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
    unique_ptr<ProgramState> preState{};
    if (incompleteLoopInfo_ == nullopt) {
        preState = clone();
    } else {
        // The previous loop was incomplete, attempt to merge the loops. Treat the incomplete loop
        // as never executed, then analyze the current loop.

        // Is this a loop stmt kind?
        auto IsLoop = [](const Stmt *S) -> bool {
            return llvm::isa<ForStmt>(S) || llvm::isa<WhileStmt>(S) || llvm::isa<DoStmt>(S) ||
                   llvm::isa<CXXForRangeStmt>(S);
        };

        // Return true if child is (recursively) contained in ancestor subtree.
        auto IsDescendantOf = [&](auto f, const Stmt *child, const Stmt *ancestor) -> bool {
            if (!child || !ancestor)
                return false;
            if (child == ancestor)
                return true;
            for (const Stmt *C : ancestor->children())
                if (C && f(f, child, C))
                    return true;
            return false;
        };

        // Find the nearest node Above such that Above is a direct child of a CompoundStmt;
        // return {CompoundStmt*, index, Above}. We climb parents until we find a CompoundStmt
        // that lists the current node as a direct child in its body().
        struct CompoundLoc {
            const CompoundStmt *CS_;
            unsigned index_;
            const Stmt *directChild_;
        };
        auto LocateInCompoundDirectChild = [&](const Stmt *S) -> std::optional<CompoundLoc> {
            if (!S)
                return std::nullopt;
            const Stmt *cur = S;
            while (cur) {
                auto parents = context_.getASTContext().getParents(*cur);
                if (parents.empty())
                    return std::nullopt;

                const Stmt *P = nullptr;
                for (const auto &N : parents) {
                    if (const Stmt *PS = N.get<Stmt>()) {
                        P = PS;
                        break;
                    }
                }
                if (!P)
                    return std::nullopt; // reached a Decl (e.g., FunctionDecl)

                if (const auto *CS = llvm::dyn_cast<CompoundStmt>(P)) {
                    unsigned idx = 0;
                    for (const Stmt *Child : CS->body()) {
                        if (Child == cur) {
                            return CompoundLoc{CS, idx, Child};
                        }
                        ++idx;
                    }
                    // cur is not a direct child of this CompoundStmt; continue climbing.
                }
                cur = P;
            }
            return std::nullopt;
        };

        auto A = incompleteLoopInfo_.value().incompleteLoop_;
        auto B = loopStmt;
        // --- Validate inputs ---
        if (!A || !B || !IsLoop(A) || !IsLoop(B))
            ERROR("A & B must be loop.");

        // --- Locate both loops as direct children of some CompoundStmt ---
        auto LocA = LocateInCompoundDirectChild(A);
        auto LocB = LocateInCompoundDirectChild(B);
        if (!LocA || !LocB)
            ERROR("A & B must be child of CompoundStmt.");

        // Must belong to the same CompoundStmt
        if (LocA->CS_ != LocB->CS_)
            ERROR("A & B must have same CompoundStmt as parent.");

        // Extra sanity: confirm original loops lie inside the recorded direct child nodes
        if (!IsDescendantOf(IsDescendantOf, A, LocA->directChild_))
            UNREACHABLE();
        if (!IsDescendantOf(IsDescendantOf, B, LocB->directChild_))
            UNREACHABLE();

        // --- Build the slice between indices (exclusive) ---
        size_t ia = LocA->index_, ib = LocB->index_;
        if (ia == ib)
            UNREACHABLE();

        size_t lo = std::min(ia, ib), hi = std::max(ia, ib);

        preState = incompleteLoopInfo_.value().preState_->clone();

        size_t idx = 0;
        for (auto child : LocA->CS_->body()) {
            if (idx >= hi)
                break;
            if (idx > lo)
                preState->step(child);
            ++idx;
        }
        incompleteLoopInfo_ = nullopt;
    }
    assert(preState != nullptr);

    auto loopEntry = preState->clone();

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
    } else if (const auto *doWhileStmt = dyn_cast<DoStmt>(loopStmt)) {
        cond = doWhileStmt->getCond();
        body = doWhileStmt->getBody();
        cond = cond->IgnoreParenImpCasts();
        if (auto *literal = llvm::dyn_cast<IntegerLiteral>(cond);
            literal && literal->getValue() == 0) {
            step(body);
            return;
        }
        UNIMPLEMENT("Loop type not supported yet: " << loopStmt->getStmtClassName());
    } else {
        UNREACHABLE();
    }

    auto [loopInfo, ok] = parseLoopInfo(*preState, *loopEntry, cond, inc, body);

    string spec;
    unique_ptr<ProgramState> postState;
    if (ok) {
        tie(spec, postState) = emitLoopInvariant(*preState, *loopEntry, cond, inc, body, loopInfo);
    } else {
        parseComplexLoopInfo(*preState, *loopEntry, cond, inc, body, loopInfo);
        tie(spec, postState) = emitLoopInvariant(*preState, *loopEntry, cond, inc, body, loopInfo,
                                                 "ComplexLoopInvariant");
    }
    INFO(spec);

    auto beginLoc = loopStmt->getSourceRange().getBegin();
    context_.insertText(beginLoc, spec, /*after*/ false,
                        /*indentNewLines*/ true);

    if (loopInfo.isIncompleteLoop_) {
        postState->incompleteLoopInfo_ =
            IncompleteLoopInfo{shared_ptr<ProgramState>(preState.release()), loopStmt};
    }

    if (this == postState.get())
        UNREACHABLE();
    *this = std::move(*postState);

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
            pathPtr->setReturnExpr(nullopt);
        }
        return;
    }
    expr = expr->IgnoreParenImpCasts();

    vector<not_null<unique_ptr<Path>>> updatedPaths;

    for (auto &pathPtr : paths_) {
        if (!pathPtr->isActive()) {
            updatedPaths.emplace_back(std::move(pathPtr));
            continue;
        }

        Path::EvalResult eval = pathPtr->evalExpr(expr);
        auto &generatedPaths  = eval.first;
        Formulas &results     = eval.second;

        size_t n = results.size();
        for (size_t i = 0; i < n; ++i) {
            auto newPath = i == 0 ? std::move(pathPtr) : std::move(generatedPaths[i - 1]);

            newPath->setReturnExpr(std::move(results[i]));
            updatedPaths.emplace_back(std::move(newPath));
        }
    }

    paths_ = std::move(updatedPaths);
}

void ProgramState::updateVarState(const BinaryOperator *binOp) {
    vector<not_null<unique_ptr<Path>>> updatedPaths;

    for (auto &path : paths_) {
        if (!path->isActive()) {
            updatedPaths.push_back(std::move(path));
            continue;
        }

        Path::EvalResult eval;

        if (binOp->isCompoundAssignmentOp()) {
            BinaryOpExpr::Operator op = getCompoundAssignOp(binOp->getOpcode());

            Path::EvalResult lhs = path->evalExpr(binOp->getLHS());

            vector<not_null<unique_ptr<Path>>> outPaths;
            Formulas outExprs;

            for (size_t i = 0; i < lhs.second.size(); ++i) {
                auto &lhsPath = (i == 0) ? *path : *lhs.first[i - 1];

                Path::EvalResult rhs = lhsPath.evalExpr(binOp->getRHS());

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
            auto newPath  = (i == 0) ? std::move(path) : std::move(eval.first[i - 1]);
            auto newValue = std::move(eval.second.at(i));
            auto dstAddr  = newPath->extractLValue(binOp->getLHS());
            newPath->updateMemory(*dstAddr, std::move(newValue));
            updatedPaths.push_back(std::move(newPath));
        }
    }

    paths_ = std::move(updatedPaths);
}

void ProgramState::addNewDecls(const vector<const VarDecl *> &varDecls) {
    for (const VarDecl *varDecl : varDecls) {
        const Expr *initExpr = varDecl->getInit();
        vector<not_null<unique_ptr<Path>>> updatedPaths;

        for (auto &path : paths_) {
            if (!path->isActive()) {
                updatedPaths.push_back(std::move(path));
                continue;
            }

            auto varAddr_ = path->allocMemory(varDecl);

            if (initExpr == nullptr) {
                // TODO: add default initialization for basic types.
                WARN("Uninitialized variable " + varDecl->getNameAsString());

                auto varType = varDecl->getType();
                if (auto RD = varType->getAsRecordDecl();
                    RD != nullptr && varType->isStructureType()) {
                    // If varDecl is a struct, memoryState_ should map an incomplete Structure
                    // (with no field values initialized).
                    if (!RD->isCompleteDefinition())
                        ERROR("Struct with incomplete definition!");

                    RD      = RD->getDefinition();
                    auto st = make_unique<Structure>(RD, RD->getASTContext().getASTRecordLayout(RD),
                                                     varAddr_->addressClone().into_underlying());
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

                    RD = RD->getDefinition();

                    auto st = make_unique<Structure>(RD, RD->getASTContext().getASTRecordLayout(RD),
                                                     varAddr_->addressClone().into_underlying());
                    if (initListExpr->getNumInits() != st->getNumFields())
                        ERROR("Initializer list size mismatches the struct's field count.");
                    auto slots = st->fieldsValues();
                    for (size_t i = 0; i < slots.size(); ++i) {
                        const Expr *init = initListExpr->getInit(i);

                        Path::EvalResult eval = path->evalExpr(init);
                        if (eval.second.size() != 1)
                            UNIMPLEMENT("No control flow branching permitted within an initializer "
                                        "list now.");
                        slots[i] = std::move(eval.second[0]);
                    }

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
                    auto newPath  = (i == 0) ? std::move(path) : std::move(eval.first[i - 1]);
                    auto newValue = std::move(eval.second[i]);

                    if (auto it = newPath->getVarAddr().find(varDecl);
                        it != newPath->getVarAddr().end()) {
                        auto &dstAddr = it->second;
                        newPath->updateMemory(*dstAddr, std::move(newValue));
                    } else {
                        ERROR("Can't find varDecl's");
                    }
                    updatedPaths.push_back(std::move(newPath));
                }
                continue;
            }
        }

        paths_ = std::move(updatedPaths);
    }
}

pair<unique_ptr<ProgramState>, unique_ptr<ProgramState>> ProgramState::splitActiveInactive() {
    auto activeState   = make_unique<ProgramState>(func_->clone(), context_);
    auto inactiveState = make_unique<ProgramState>(func_->clone(), context_);

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
    if (states.empty())
        ERROR("Nothing to be merged.");
    optional<not_null<unique_ptr<ProgramState>>> merged{};

    for (auto &state : states) {
        if (merged == nullopt) {
            merged.emplace(state->clone());
            continue;
        }
        if (*state->getFunction() != *merged.value()->getFunction())
            ERROR("States to be merged are dealing with different functions.");
        for (const auto &path : state->paths_)
            merged.value()->paths_.push_back(path->clone());
    }
    return std::move(merged).value().into_underlying();
}

unique_ptr<ProgramState> ProgramState::merge(const vector<unique_ptr<ProgramState>> &states) {
    if (states.empty())
        ERROR("Nothing to be merged.");
    optional<not_null<unique_ptr<ProgramState>>> merged{};

    for (auto &state : states) {
        if (merged == nullopt) {
            merged.emplace(state->clone());
            continue;
        }
        if (*state->getFunction() != *merged.value()->getFunction())
            ERROR("States to be merged are dealing with different functions.");
        for (const auto &path : state->paths_)
            merged.value()->paths_.push_back(path->clone());
    }
    return std::move(merged).value().into_underlying();
}

unique_ptr<ProgramState> ProgramState::clone(bool withPath) const {
    auto newState                 = make_unique<ProgramState>(func_->clone(), context_);
    newState->incompleteLoopInfo_ = incompleteLoopInfo_;

    if (withPath) {
        for (const auto &path : paths_) {
            newState->paths_.push_back(path->clone());
        }
    }

    newState->StmtCtx = StmtCtx;
    return newState;
}

unique_ptr<ProgramState> ProgramState::cloneWithPaths(vector<unique_ptr<Path>> &newPaths) const {
    auto clone = this->clone(false);
    clone->paths_.clear();
    clone->paths_.reserve(newPaths.size());
    clone->paths_.insert(clone->paths_.end(), make_move_iterator(newPaths.begin()),
                         make_move_iterator(newPaths.end()));
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
        onePath.push_back(std::move(path).into_underlying());
        auto stateClone = cloneWithPaths(onePath);

        result.emplace_back(std::move(stateClone),
                            std::move(evalResult.second[0]).into_underlying());
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
            Path tmpPath(context_);
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

optional<not_null<std::unique_ptr<Path>>> ProgramState::takePath(size_t i) {
    if (i >= paths_.size())
        return nullopt;
    auto p = std::move(paths_.at(i));
    paths_.erase(paths_.begin() + i);
    return p;
}

std::vector<not_null<std::unique_ptr<Path>>> ProgramState::takeAllPaths() {
    std::vector<not_null<std::unique_ptr<Path>>> out;
    out.swap(paths_);
    return out;
}

[[deprecated("Some bugs, use `eraseExpiredLocals`")]]
MemoryModel::KeySet ProgramState::snapshot_all_path_keys() const {
    MemoryModel::KeySet uni;

    /**
     * Iterate over all paths and accumulate the union of flattened address
     * keys derived from each path's memory model. The flattening semantics
     * coincide with MemoryModel::flat_view, ensuring that ranges and symbolic
     * addresses are reconstructed consistently with iteration order.
     */
    for (const auto &p : paths_) {
        const auto &mm = p->getMemoryState();
        auto ks        = mm.keys_flat();
        // Union: insert all keys into the accumulator.
        uni.insert(ks.begin(), ks.end());
    }
    return uni;
}

[[deprecated("Some bugs, use `eraseExpiredLocals`")]]
void ProgramState::retain_only_keys_across_paths(const MemoryModel::KeySet &keep) {
    /**
     * For every path, enforce a retention filter on the underlying memory
     * model: only entries whose flattened addresses belong to @p keep are
     * preserved; all other entries are removed. This models the effect of
     * unwinding a call or scope, where ephemeral updates are discarded while
     * globally-relevant state is retained.
     */
    for (auto &p : paths_) {
        auto &mm = p->getMutMemoryState();
        mm.retain_only(keep);
    }
}

string ProgramState::dump() const {
    ostringstream oss;
    for (const auto &p : paths_) {
        oss << p->dump() << "\n";
    }
    return oss.str();
}