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

namespace acslg::analyzer {
    using std::literals::string_literals::operator""s;

    Path::Path(const Path &other, bool shallowCopy)
        : context_(other.context_), startPoint_(other.startPoint_) {
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
        swap(startPoint_, o.startPoint_);
    }

    void Path::resymbolize(symbolic::SourcePoint newStartPoint) {
        memoryState_.clear();
        pathConditions_.clear();

        startPoint_ = std::move(newStartPoint);
        for (auto &[varDecl, addr] : varAddr_) {
            clang::QualType ty = varDecl->getType();

            auto symbol = getSymbol(ty, addr->addressClone().into_underlying(), startPoint_);
            updateMemory(*addr, std::move(symbol));
        }
    }

    utils::not_null<std::unique_ptr<symbolic::Address>> Path::extractLValue(const clang::Expr *lhs) {
        auto lexpr = lhs->IgnoreParenImpCasts();
        if (auto declRef = dyn_cast<clang::DeclRefExpr>(lexpr)) {
            if (auto varDecl = llvm::dyn_cast<clang::VarDecl>(declRef->getDecl())) {
                if (auto it = varAddr_.find(varDecl->getCanonicalDecl()); it != varAddr_.end()) {
                    return std::make_unique<symbolic::VariableAddress>(*it->second);
                } else {
                    ERROR("varState has no ArraySubscriptExpr's base, undefined variable?");
                }
            }
            ERROR("Not a varDecl Ref.");
        }

        if (auto *arr = dyn_cast<clang::ArraySubscriptExpr>(lexpr)) {
            auto baseAddr = extractLValue(arr->getBase());
            if (const auto symbol = memoryState_.read(*baseAddr)) {
                auto symbolAddr =
                    llvm::dyn_cast<const symbolic::SymbolAddress>(symbol.value().get());
                if (symbolAddr == nullptr)
                    ERROR("Value of ArraySubscriptExpr's base is not 'symbolic::SymbolAddress', "
                          "base is "
                          "neither pointer nor std::array?");
                auto resultAddr = std::make_unique<symbolic::SymbolAddress>(*symbolAddr);
                auto idxEval    = evalExpr(arr->getIdx());
                if (idxEval.second.size() != 1)
                    ERROR("This location does not support control flow branches.");
                auto idxExpr = std::move(idxEval.second[0]);
                resultAddr->addOffset(std::move(idxExpr));
                if (!memoryState_.contains(*resultAddr)) {
                    auto newSymbol = getSymbol(
                        arr->getType(), resultAddr->addressClone().into_underlying(), startPoint_);
                    memoryState_.write(*resultAddr, std::move(newSymbol));
                }
                return resultAddr;
            } else {
                ERROR("memoryState_ has no ArraySubscriptExpr's base, base is neither pointer nor "
                      "array?");
            }
        }

        if (auto *uop = dyn_cast<clang::UnaryOperator>(lexpr)) {
            if (uop->getOpcode() == clang::UnaryOperatorKind::UO_Deref) {
                auto addrEval = evalExpr(uop->getSubExpr());
                if (addrEval.second.size() != 1)
                    TODO();

                auto addrExpr = std::move(addrEval.second[0]);
                if (auto addr = addrExpr->tryEvalAsSymbolAddr()) {
                    if (!memoryState_.contains(*addr.value())) {
                        auto symbol =
                            getSymbol(uop->getType(),
                                      addr.value()->addressClone().into_underlying(), startPoint_);
                        memoryState_.write(*addr.value(), std::move(symbol));
                    }
                    return std::move(addr).value().into_underlying();
                } else {
                    ERROR("Expected symbolic::Address in deref, got: " << addrExpr->dump());
                }
            }
        }

        if (auto *mem = dyn_cast<clang::MemberExpr>(lexpr)) {
            auto base = mem->getBase()->IgnoreParenImpCasts();
            auto FD   = llvm::dyn_cast<clang::FieldDecl>(mem->getMemberDecl());
            if (FD == nullptr)
                ERROR("RHS of memberExpr is not a `clang::FieldDecl`?");
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
                if (baseAddr == std::nullopt)
                    ERROR("Expected symbolic::Address for '->' base, got: " << baseExpr->dump());

                if (!memoryState_.contains(*baseAddr.value())) {
                    auto st = std::make_unique<symbolic::Structure>(
                        RD, layout, baseAddr.value()->addressClone().into_underlying(),
                        startPoint_);
                    memoryState_.write(*baseAddr.value(), std::move(st));
                }
                return std::make_unique<symbolic::FieldAddress>(
                    RD, std::pair{baseAddr.value()->addressClone().into_underlying(),
                                  FD->getFieldIndex()});
            } else {
                auto baseAddr = extractLValue(base);

                if (!memoryState_.contains(*baseAddr)) {
                    auto st = std::make_unique<symbolic::Structure>(
                        RD, layout, baseAddr->addressClone().into_underlying(), startPoint_);
                    memoryState_.write(*baseAddr, std::move(st));
                }
                return std::make_unique<symbolic::FieldAddress>(
                    RD, std::pair{baseAddr->addressClone().into_underlying(), FD->getFieldIndex()});
            }
        }

        UNIMPLEMENT("Unsupported LHS expression: " << lexpr->getStmtClassName());
    }

    utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> Path::getVarState(
        const clang::VarDecl *var) const {
        auto canonicalVar = var->getCanonicalDecl();
        auto varIt        = varAddr_.find(canonicalVar);
        if (varIt == varAddr_.end())
            ERROR("Variable '" + canonicalVar->getNameAsString() + "' has no allocated address");
        auto addr  = varIt->second.get().get();
        auto value = memoryState_.read(*addr);
        if (value == std::nullopt)
            ERROR("Variable '" + canonicalVar->getNameAsString() +
                  "' has no memory state entry for allocated address");
        return value.value()->clone();
    }

    const Formulas &Path::getPathConditions() const { return pathConditions_; }

    utils::not_null<symbolic::VariableAddress *> Path::allocMemory(const clang::VarDecl *var) {
        auto canonicalVar = var->getCanonicalDecl();
        if (varAddr_.contains(canonicalVar)) {
            // All `symbolic::VariableAddress` built from same canonicalVar are same.
            return varAddr_.at(canonicalVar).get().get();
        }
        auto newAddr = std::make_unique<symbolic::VariableAddress>(var);
        auto rawPtr  = newAddr.get();
        varAddr_.emplace(canonicalVar, std::move(newAddr));

        // Prevent uninitialized variables.
        memoryState_.write(*rawPtr, symbolic::UnknownExpr::makeUnknown().into_underlying());
        return rawPtr;
    }

    void Path::updateMemory(const symbolic::Address &addr,
                            utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> expr) {
        memoryState_.write(addr, std::move(expr).into_underlying());
    }

    void Path::updateVarState(utils::not_null<const clang::VarDecl *> var,
                              utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> expr) {
        auto canonicalVar = var->getCanonicalDecl();
        auto addrIt       = varAddr_.find(canonicalVar);
        if (addrIt == varAddr_.end())
            ERROR("Variable has no allocated address");

        auto &addr = addrIt->second;
        memoryState_.write(*addr, std::move(expr).into_underlying());
    }

    void Path::insertPathCondition(utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> cond) {
        pathConditions_.push_back(std::move(cond));
    }

    std::unique_ptr<Path> Path::clone() const {
        auto cloned           = std::make_unique<Path>(context_, startPoint_);
        cloned->currentState_ = currentState_;
        for (const auto &entry : varAddr_)
            cloned->varAddr_.emplace(entry.first,
                                     std::make_unique<symbolic::VariableAddress>(*entry.second));
        cloned->memoryState_ = memoryState_;
        for (const auto &cond : pathConditions_)
            cloned->pathConditions_.push_back(cond->clone());
        if (returnExpr_)
            cloned->returnExpr_.emplace(returnExpr_.value()->clone().into_underlying());
        else
            cloned->returnExpr_ = std::nullopt;
        cloned->StmtCtx = StmtCtx;
        return cloned;
    }

    struct CallArgs {
        std::unique_ptr<Path> path;
        std::vector<std::unique_ptr<symbolic::SymbolicExpr>> args;
    };

    void bindParams(Path *calleePath,
                    const clang::FunctionDecl *FD,
                    const std::vector<std::unique_ptr<symbolic::SymbolicExpr>> &args) {
        const unsigned n = FD->getNumParams();
        assert(args.size() == n);

        for (unsigned i = 0; i < n; ++i) {
            const clang::ParmVarDecl *param = FD->getParamDecl(i);
            clang::QualType T               = param->getType();

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

    static std::vector<CallArgs> evalCallArgs(Path *basePath, const clang::CallExpr *call) {
        std::vector<CallArgs> args;
        args.push_back({nullptr, {}});

        const unsigned n = call->getNumArgs();
        for (unsigned i = 0; i < n; ++i) {
            const clang::Expr *arg = call->getArg(i);
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

    Path::EvalResult Path::evalExpr(const clang::Expr *expr) {
        if (!expr)
            ERROR("Fail to convert an empty clang::Expr");

        EvalResult eval_result =
            llvm::TypeSwitch<const clang::Expr *, EvalResult>(expr)
                .Case<clang::IntegerLiteral>([](const clang::IntegerLiteral *lit) -> EvalResult {
                    DEBUG("evaluating IntegerLiteral...");
                    llvm::APInt ap          = lit->getValue();
                    clang::QualType litType = lit->getType();
                    std::unique_ptr<symbolic::SymbolicExpr> result;

                    if (litType->isBooleanType()) {
                        result = std::make_unique<symbolic::LiteralExpr>(
                            static_cast<bool>(ap.getZExtValue()));
                    } else if (litType->isUnsignedIntegerType()) {
                        if (ap.getBitWidth() <= 8)
                            result = std::make_unique<symbolic::LiteralExpr>(
                                static_cast<unsigned char>(ap.getZExtValue()));
                        else if (ap.getBitWidth() <= 16)
                            result = std::make_unique<symbolic::LiteralExpr>(
                                static_cast<unsigned short>(ap.getZExtValue()));
                        else if (ap.getBitWidth() <= 32)
                            result = std::make_unique<symbolic::LiteralExpr>(
                                static_cast<unsigned int>(ap.getZExtValue()));
                        else if (ap.getBitWidth() <= 64)
                            result = std::make_unique<symbolic::LiteralExpr>(
                                static_cast<uint64_t>(ap.getZExtValue()));
                        else
                            UNIMPLEMENT("Unsupported unsigned integer literal with bit width > 64: "
                                        << ap.getBitWidth());
                    } else {
                        if (ap.getBitWidth() <= 8)
                            result = std::make_unique<symbolic::LiteralExpr>(
                                static_cast<char>(ap.getSExtValue()));
                        else if (ap.getBitWidth() <= 16)
                            result = std::make_unique<symbolic::LiteralExpr>(
                                static_cast<short>(ap.getSExtValue()));
                        else if (ap.getBitWidth() <= 32)
                            result = std::make_unique<symbolic::LiteralExpr>(
                                static_cast<int>(ap.getSExtValue()));
                        else if (ap.getBitWidth() <= 64)
                            result = std::make_unique<symbolic::LiteralExpr>(
                                static_cast<int64_t>(ap.getSExtValue()));
                        else
                            UNIMPLEMENT("Unsupported signed integer literal with bit width > 64: "
                                        << ap.getBitWidth());
                    }

                    Formulas exprs;
                    exprs.reserve(1);
                    exprs.push_back(std::move(result));

                    return {std::vector<utils::not_null<std::unique_ptr<Path>>>{},
                            std::move(exprs)};
                })
                .Case<clang::BinaryOperator>([this](
                                                 const clang::BinaryOperator *binOp) -> EvalResult {
                    DEBUG("evaluating BinaryOperator...");
                    // TODO: maybe pack the logic in BO, ArraySub into a function?
                    EvalResult lhs                      = evalExpr(binOp->getLHS());
                    symbolic::BinaryOpExpr::Operator op = symbolic::getBinaryOp(binOp->getOpcode());

                    std::vector<utils::not_null<std::unique_ptr<Path>>> outPaths;
                    Formulas outExprs;

                    size_t lhsCount = lhs.second.size();
                    for (size_t i = 0; i < lhsCount; ++i) {
                        auto lhsExpr    = std::move(lhs.second[i]);
                        Path *path      = (i == 0) ? this : lhs.first[i - 1].get().get();
                        EvalResult rhs  = path->evalExpr(binOp->getRHS());
                        size_t rhsCount = rhs.second.size();

                        for (size_t j = 0; j < rhsCount; ++j) {
                            auto rhsExpr = std::move(rhs.second[j]);

                            outExprs.emplace_back(std::make_unique<symbolic::BinaryOpExpr>(
                                lhsExpr->clone(), op, std::move(rhsExpr)));

                            if (i == 0 && j == 0)
                                continue;

                            outPaths.emplace_back(j == 0 ? std::move(lhs.first[i - 1])
                                                         : std::move(rhs.first[j - 1]));
                        }
                    }

                    return {std::move(outPaths), std::move(outExprs)};
                })
                .Case<clang::ParenExpr>([this](const clang::ParenExpr *paren) -> EvalResult {
                    DEBUG("evaluating ParenExpr...");
                    return evalExpr(paren->getSubExpr());
                })
                .Case<clang::DeclRefExpr>([this](const clang::DeclRefExpr *declRef) -> EvalResult {
                    DEBUG("evaluating clang::DeclRefExpr...");
                    if (const auto *varDecl = dyn_cast<clang::VarDecl>(declRef->getDecl())) {
                        auto varExpr = getVarState(varDecl);
                        Formulas exprs;
                        exprs.push_back(std::move(varExpr));
                        return {std::vector<utils::not_null<std::unique_ptr<Path>>>{},
                                std::move(exprs)};
                    }

                    if (const auto *enumDecl =
                            dyn_cast<clang::EnumConstantDecl>(declRef->getDecl())) {
                        llvm::APSInt value = enumDecl->getInitVal();
                        auto litExpr       = std::make_unique<symbolic::LiteralExpr>(
                            static_cast<int>(value.getSExtValue()));
                        Formulas exprs;
                        exprs.push_back(std::move(litExpr));
                        return {std::vector<utils::not_null<std::unique_ptr<Path>>>{},
                                std::move(exprs)};
                    }

                    UNIMPLEMENT(
                        "Unsupported clang::Decl type: " << declRef->getDecl()->getDeclKindName());
                    return Path::EvalResult{};
                })
                .Case<clang::ArraySubscriptExpr>(
                    [this](const clang::ArraySubscriptExpr *arrSub) -> EvalResult {
                        DEBUG("evaluating ArraySubscriptExpr...");
                        auto variableAddr = extractLValue(arrSub->getBase());
                        std::unique_ptr<symbolic::SymbolAddress> addr;
                        if (const auto symbol = memoryState_.read(*variableAddr)) {
                            auto ptr =
                                llvm::dyn_cast<const symbolic::SymbolAddress>(symbol.value().get());
                            if (ptr == nullptr)
                                ERROR("Value of ArraySubscriptExpr's base is not "
                                      "'symbolic::SymbolAddress', base "
                                      "is "
                                      "neither "
                                      "pointer nor "
                                      "array?");
                            addr = std::make_unique<symbolic::SymbolAddress>(*ptr);
                        } else {
                            ERROR("memoryState_ has no ArraySubscriptExpr's base, base is neither "
                                  "pointer "
                                  "nor "
                                  "array?");
                        }
                        EvalResult idx = evalExpr(arrSub->getIdx());

                        std::vector<utils::not_null<std::unique_ptr<Path>>> outPaths;
                        Formulas outExprs;

                        for (size_t i = 0; i < idx.second.size(); ++i) {
                            auto idxExpr        = std::move(idx.second[i]);
                            std::string idxDump = idxExpr->dump();
                            auto newAddr        = std::make_unique<symbolic::SymbolAddress>(*addr);
                            newAddr->setOffset(std::move(idxExpr));
                            if (auto value = memoryState_.read(*newAddr); value == std::nullopt) {
                                auto elemType = arrSub->getType();
                                auto symbol =
                                    getSymbol(elemType, newAddr->addressClone().into_underlying(),
                                              startPoint_);
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
                .Case<clang::CallExpr>([this](const clang::CallExpr *call) -> EvalResult {
                    DEBUG("evaluating clang::CallExpr...");
                    const clang::FunctionDecl *callee = call->getDirectCallee();
                    if (!callee) {
                        Formulas exprs;
                        exprs.emplace_back(symbolic::UnknownExpr::makeUnknown().into_underlying());
                        std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                        return Path::EvalResult(std::move(empty), std::move(exprs));
                    }
                    static const std::set<std::string> ignoreNames = {
                        "llvm.dbg.declare", "llvm.lifetime.start", "llvm.lifetime.end", "printf",
                        "__assert_fail"};
                    std::string name = callee->getNameAsString();
                    if (ignoreNames.contains(name)) {
                        Formulas exprs;
                        exprs.emplace_back(symbolic::UnknownExpr::makeUnknown().into_underlying());
                        std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                        return Path::EvalResult(std::move(empty), std::move(exprs));
                    }

                    auto callArgs = evalCallArgs(this, call);

                    std::vector<utils::not_null<std::unique_ptr<Path>>> outPaths;
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
                                (p->getReturnExpr()
                                     ? p->getReturnExpr().value()->clone()
                                     : symbolic::UnknownExpr::makeUnknown().into_underlying());

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
                        outExprs.emplace_back(
                            symbolic::UnknownExpr::makeUnknown().into_underlying());
                    }

                    return {std::move(outPaths), std::move(outExprs)};
                })
                .Case<clang::ConditionalOperator>(
                    [this](const clang::ConditionalOperator *condOp) -> EvalResult {
                        DEBUG("evaluating ConditionalOperator...");
                        EvalResult cond = evalExpr(condOp->getCond());

                        std::vector<utils::not_null<std::unique_ptr<Path>>> outPaths;
                        Formulas outExprs;

                        for (size_t i = 0; i < cond.second.size(); ++i) {
                            Path *condPath = (i == 0) ? this : cond.first[i - 1].get().get();
                            auto condExpr  = std::move(cond.second[i]);

                            // false branch
                            auto falsePath   = condPath->clone();
                            auto negatedCond = std::make_unique<symbolic::UnaryOpExpr>(
                                symbolic::UnaryOpExpr::Operator::LogicalNot, condExpr->clone());
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
                .Case<clang::UnaryOperator>([this](const clang::UnaryOperator *uop) -> EvalResult {
                    DEBUG("evaluating UnaryOperator...");
                    auto operand = evalExpr(uop->getSubExpr());
                    std::vector<utils::not_null<std::unique_ptr<Path>>> outPaths;
                    Formulas outExprs;

                    symbolic::UnaryOpExpr::Operator op;
                    switch (uop->getOpcode()) {
                        using enum clang::UnaryOperatorKind;
                        using enum symbolic::UnaryOpExpr::Operator;
                        case UO_Plus: op = Plus; break;
                        case UO_Minus: op = Minus; break;
                        case UO_LNot: op = LogicalNot; break;
                        case UO_Not: op = BitwiseNot; break;
                        case UO_PreInc: op = PreInc; break;
                        case UO_PreDec: op = PreDec; break;
                        case UO_PostInc: op = PostInc; break;
                        case UO_PostDec: op = PostDec; break;
                        case UO_AddrOf: op = AddrOf; break;
                        case UO_Deref: op = Dereference; break;
                        default:
                            UNIMPLEMENT("Unsupported UnaryOperator: "
                                        << uop->getOpcodeStr(uop->getOpcode()).str());
                    }

                    for (size_t i = 0; i < operand.second.size(); ++i) {
                        Path *path  = (i == 0 ? this : operand.first[i - 1].get().get());
                        auto unExpr = std::move(operand.second[i]);

                        // Prevent misuse by not capturing this and operand.
                        [this, &path, &unExpr, &op, &outExprs, &uop]() {
                            using enum symbolic::UnaryOpExpr::Operator;
                            if (op == PreInc || op == PostInc || op == PreDec || op == PostDec) {
                                // ++x / x++ / --x / x--
                                auto addr = path->extractLValue(uop->getSubExpr());
                                // old value
                                auto oldVal = path->memoryState_.read(*addr);
                                if (oldVal == std::nullopt)
                                    ERROR("memoryState_ doesn't contain addr.");
                                // compute new = old +/- 1
                                auto one    = std::make_unique<symbolic::LiteralExpr>(1);
                                auto binOp  = (op == PreInc || op == PostInc)
                                                  ? symbolic::BinaryOpExpr::Operator::Add
                                                  : symbolic::BinaryOpExpr::Operator::Subtract;
                                auto newVal = std::make_unique<symbolic::BinaryOpExpr>(
                                    oldVal.value()->clone(), binOp, std::move(one));
                                // return pre vs post
                                if (op == PreInc || op == PreDec)
                                    outExprs.emplace_back(newVal->clone());
                                else {
                                    outExprs.emplace_back(oldVal.value()->clone());
                                }
                                // Writing back first will cause oldVal to become dangling.
                                // write back
                                path->memoryState_.write(*addr, std::move(newVal));
                            } else if (op == Dereference) {
                                // *x
                                auto addr = unExpr->tryEvalAsSymbolAddr();
                                if (addr == std::nullopt)
                                    ERROR("Expected symbolic::Address, got: " << unExpr->dump());
                                if (auto value = path->memoryState_.read(*addr.value());
                                    value == std::nullopt) {
                                    auto symbol =
                                        getSymbol(uop->getType(),
                                                  addr.value()->addressClone().into_underlying(),
                                                  startPoint_);
                                    path->memoryState_.write(*addr.value(), symbol->clone());
                                    outExprs.emplace_back(std::move(symbol));
                                } else {
                                    outExprs.emplace_back(value.value()->clone());
                                }
                            } else if (op == AddrOf)
                                TODO();
                            else
                                outExprs.emplace_back(
                                    std::make_unique<symbolic::UnaryOpExpr>(op, std::move(unExpr)));
                        }();
                        if (i > 0)
                            outPaths.emplace_back(std::move(operand.first[i - 1]));
                    }

                    return {std::move(outPaths), std::move(outExprs)};
                })
                .Case<clang::CastExpr>([this](const clang::CastExpr *castExpr) -> EvalResult {
                    DEBUG("evaluating CastExpr...");
                    auto sub = evalExpr(castExpr->getSubExpr());
                    if (castExpr->getType()->isStructureType())
                        return sub;

                    auto targetType = symbolic::deriveVarType(castExpr->getType());

                    for (auto &subExpr : sub.second)
                        subExpr->setValType(targetType);

                    return {std::move(sub.first), std::move(sub.second)};
                })
                .Case<clang::MemberExpr>([this](const clang::MemberExpr *memberExpr) -> EvalResult {
                    DEBUG("evaluating MemberExpr...");
                    auto FD = dyn_cast_if_present<clang::FieldDecl>(memberExpr->getMemberDecl());
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

                    std::unique_ptr<symbolic::Structure> st;
                    auto baseExpr = std::move(base.second[0]).into_underlying();

                    if (memberExpr->isArrow()) {
                        auto baseAddr = baseExpr->tryEvalAsSymbolAddr();
                        if (baseAddr == std::nullopt)
                            ERROR("LHS of '->' is not an address");
                        auto val = memoryState_.read(*baseAddr.value());
                        if (val == std::nullopt) {
                            st = std::make_unique<symbolic::Structure>(
                                RD, layout, baseAddr.value()->addressClone().into_underlying(),
                                startPoint_);
                            memoryState_.write(*baseAddr.value(), st->clone());
                        } else if (auto stVal = llvm::dyn_cast<const symbolic::Structure>(
                                       val.value().get())) {
                            st = std::make_unique<symbolic::Structure>(*stVal);
                        } else {
                            ERROR("Dereferenced value is not a structure");
                        }
                    } else {
                        if (auto stExpr = llvm::dyn_cast<symbolic::Structure>(baseExpr))
                            st = std::move(stExpr);
                        else
                            ERROR("LHS of '.' is not a structure");
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

                .Case<clang::ConstantExpr>([this](const clang::ConstantExpr *ce) -> EvalResult {
                    DEBUG("evaluating ConstantExpr...");
                    llvm::APSInt v;
                    bool ok = false;
                    if (ce->getResultAPValueKind() == clang::APValue::Int) {
                        v  = ce->getResultAsAPSInt();
                        ok = (v.getBitWidth() != 0);
                    }
                    if (!ok) {
                        EvalResult sub = evalExpr(ce->getSubExpr());
                        if (sub.second.size() != 1)
                            ERROR("ConstantExpr subExpr produced multiple results");
                        auto resultTy = symbolic::deriveVarType(ce->getType());
                        sub.second[0]->setValType(resultTy);
                        return {std::move(sub.first), std::move(sub.second)};
                    }
                    auto resultTy = symbolic::deriveVarType(ce->getType());
                    auto lit      = std::make_unique<symbolic::LiteralExpr>(
                        v.isSigned() ? static_cast<int64_t>(v.getSExtValue())
                                     : static_cast<uint64_t>(v.getZExtValue()));
                    lit->setValType(resultTy);
                    EvalResult r;
                    r.second.emplace_back(std::move(lit));
                    return r;
                })
                .Case<clang::UnaryExprOrTypeTraitExpr>(
                    [this](const clang::UnaryExprOrTypeTraitExpr *uett) -> EvalResult {
                        DEBUG("evaluating UnaryExprOrTypeTraitExpr...");

                        // Obtain the AST context, which encodes target-dependent size/alignment.
                        auto &ctx = this->context_.getASTContext();

                        // Resolve the operand type for either type- or expression-based arguments.
                        auto operandQT = [&]() -> clang::QualType {
                            return uett->isArgumentType() ? uett->getArgumentType()
                                                          : uett->getArgumentExpr()->getType();
                        }();

                        uint64_t value = 0;
                        switch (uett->getKind()) {
                            using enum clang::UnaryExprOrTypeTrait;
                            case UETT_SizeOf:
                                // Size is measured in bytes per the target data layout.
                                value = static_cast<uint64_t>(
                                    ctx.getTypeSizeInChars(operandQT).getQuantity());
                                break;
                            case UETT_AlignOf:
                            case UETT_PreferredAlignOf:
                                // Alignment is reported in bytes as required by the ABI.
                                value = static_cast<uint64_t>(
                                    ctx.getTypeAlignInChars(operandQT).getQuantity());
                                break;
                            default:
                                UNIMPLEMENT("Unsupported UnaryExprOrTypeTraitExpr kind: "
                                            << static_cast<int>(uett->getKind()));
                        }

                        // Materialize a literal of the expression’s result type (typically size_t).
                        auto resultTy = symbolic::deriveVarType(uett->getType());
                        auto lit      = std::make_unique<symbolic::LiteralExpr>(value);
                        lit->setValType(resultTy);

                        EvalResult r;
                        r.second.emplace_back(std::move(lit));
                        return r;
                    })

                .Default([](const clang::Expr *e) -> EvalResult {
                    UNIMPLEMENT("Unsupported clang::Expr type: " << e->getStmtClassName());
                    return Path::EvalResult{};
                });
        return eval_result;
    }

    std::string Path::dump() const {
        using namespace utils::dump_fmt;

        std::ostringstream oss;

        // ---- Header: Path + State ------------------------------------------------
        oss << type("Path") << " { " << key("state") << "=" << [&]() {
            switch (currentState_) {
                case Path::PathState::Step: return "Step";
                case Path::PathState::Continue: return "Continue";
                case Path::PathState::Break: return "Break";
                case Path::PathState::Return: return "Return";
                default: return "Unknown";
            }
        }() << " }\n";

        // ---- Return Expression ---------------------------------------------------
        oss << "  " << key("return") << ": ";
        if (returnExpr_) {
            oss << returnExpr_.value()->dump() << "\n";
        } else {
            oss << hint("null") << "\n";
        }

        // ---- Path Conditions -----------------------------------------------------
        oss << "  " << key("conditions") << ":\n";
        for (size_t i = 0; i < pathConditions_.size(); ++i) {
            oss << "    "
                << "[" << lit(std::to_string(i)) << "] " << pathConditions_[i]->dump() << "\n";
        }
        if (pathConditions_.empty()) {
            oss << "    " << hint("<empty>") << "\n";
        }

        // ---- Variable -> Address -> Value mapping -------------------------------
        oss << "  " << key("var→addr→value") << ":\n";
        for (auto &[varDecl, addr] : varAddr_) {
            std::string name;
            if (auto opt = context_.getDeclInfo(varDecl)) {
                std::tie(name, std::ignore, std::ignore, std::ignore, std::ignore) = *opt;
            }

            oss << "    @" << (name.empty() ? hint("<unnamed>") : path(name)) << " " << op("->")
                << " " << addr->dump();

            if (auto value = memoryState_.read(*addr)) {
                oss << " " << op("->") << " " << value.value()->dump();
            } else {
                oss << " " << op("->") << " " << hint("null");
            }
            oss << "\n";
        }
        if (varAddr_.empty()) {
            oss << "    " << hint("<empty>") << "\n";
        }

        // ---- Memory State flat view ---------------------------------------------
        oss << "  " << key("memory") << ":\n";
        {
            bool any = false;
            for (auto &&[addr, value] : memoryState_.flat()) {
                any = true;
                oss << "    " << addr.get().dump() << " " << op("->") << " " << value->dump()
                    << "\n";
            }
            if (!any) {
                oss << "    " << hint("<empty>") << "\n";
            }
        }

        // ---- Statement Context (source snippet) ---------------------------------
        if (StmtCtx) {
            if (auto opt = context_.getStmtInfo(StmtCtx)) {
                const auto &sourceText = std::get<0>(*opt);
                if (!sourceText.empty()) {
                    oss << "  " << key("stmt") << ": " << path(sourceText.str()) << "\n";
                }
            }
        }

        return oss.str();
    }

    std::string ProgramState::dump() const {
        using namespace utils::dump_fmt;

        std::ostringstream oss;

        if (paths_.empty()) {
            oss << type("ProgramState") << " " << hint("<no paths>") << "\n";
            return oss.str();
        }

        oss << type("ProgramState") << " " << key("paths") << "="
            << lit(std::to_string(paths_.size())) << "\n";

        for (size_t i = 0; i < paths_.size(); ++i) {
            oss << "  " << key("path") << "[" << lit(std::to_string(i)) << "]\n";
            std::istringstream is(paths_[i]->dump());
            std::string line;
            while (std::getline(is, line)) {
                if (!line.empty())
                    oss << "    " << line << "\n";
                else
                    oss << "\n";
            }
        }

        return oss.str();
    }

    bool Path::isUnchanged(const symbolic::Address &addr,
                           std::optional<symbolic::SourcePoint> since) const {
        auto value = memoryState_.read(addr);
        if (value == std::nullopt)
            return true; // Assume it has not been accessed yet.

        if (since != std::nullopt)
            return isFrom(*value.value(), addr, std::move(since).value());
        return isFrom(*value.value(), addr, startPoint_);
    }

    bool Path::is_point_to_structure(const symbolic::Address &addr) const {
        auto opt = memoryState_.read(addr);
        if (!opt)
            return false;

        const symbolic::SymbolicExpr *expr = opt.value().get();
        return llvm::isa<symbolic::Structure>(expr);
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

    std::optional<utils::not_null<const symbolic::SymbolicExpr *>> MemoryModel::read(
        const symbolic::Address &addr) const {
        if (auto varAddr = llvm::dyn_cast<const symbolic::VariableAddress>(&addr)) {
            if (memoryMap_variableAddr_.contains(*varAddr))
                return memoryMap_variableAddr_.at(*varAddr).get().get();
            return std::nullopt;
        } else if (auto symbolAddr = llvm::dyn_cast<const symbolic::SymbolAddress>(&addr)) {
            auto baseInfo = symbolAddr->getBaseInfo();

            if (memoryMap_constantRange_.contains(baseInfo)) {
                auto offset = symbolAddr->getOffset();
                if (auto constOffset = offset->tryEvalAsConstant();
                    constOffset && !symbolAddr->isRange()) {
                    if (constOffset.value() < 0)
                        ERROR("Negetive offset.");
                    auto unsignedOffset = static_cast<uint64_t>(constOffset.value());
                    auto &rangeExprMap  = memoryMap_constantRange_.at(baseInfo);
                    // auto range          = std::pair{unsignedOffset, unsignedOffset + 1};
                    auto rangeForSearch =
                        std::pair{unsignedOffset, std::numeric_limits<uint64_t>::max()};
                    auto upperBoundIt = rangeExprMap.upper_bound(rangeForSearch);
                    auto firstLEIt    = upperBoundIt == rangeExprMap.begin() ? rangeExprMap.end()
                                                                             : prev(upperBoundIt);
                    if (firstLEIt == rangeExprMap.end() ||
                        firstLEIt->first.second <= unsignedOffset)
                        return std::nullopt;
                    return firstLEIt->second.get().get();
                } else if (constOffset && symbolAddr->isRange() &&
                           symbolAddr->getLength()->tryEvalAsConstant()) {
                    UNIMPLEMENT(
                        "There doesn't appear to be a need for constant-range range queries at "
                        "this time.");
                }
            }

            if (!memoryMap_symbolicRange_.contains(baseInfo))
                return std::nullopt;
            auto &addrValueMap = memoryMap_symbolicRange_.at(baseInfo);
            auto it            = addrValueMap.find(*symbolAddr);
            if (it == addrValueMap.end())
                return std::nullopt;
            return it->second.get().get();
        } else if (auto fieldAddr = llvm::dyn_cast<const symbolic::FieldAddress>(&addr)) {
            return std::visit(
                [this](
                    auto &&arg) -> std::optional<utils::not_null<const symbolic::SymbolicExpr *>> {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        TODO();
                    } else if constexpr (std::is_same_v<T, std::pair<utils::not_null<std::unique_ptr<
                                                                         const symbolic::Address>>,
                                                                     const size_t>>) {
                        auto &[baseAddr, index] = arg;
                        auto baseValue          = read(*baseAddr);
                        if (baseValue == std::nullopt)
                            return std::nullopt;
                        auto baseSt =
                            llvm::dyn_cast<const symbolic::Structure>(baseValue.value().get());
                        if (baseSt == nullptr)
                            ERROR("Value of address from a `fieldAddress` is not a structure.");
                        return baseSt->getFieldValue(index);
                    }
                },
                fieldAddr->getFrom());
        }
        UNREACHABLE();
    }

    std::optional<utils::not_null<symbolic::SymbolicExpr *>> MemoryModel::read(
        const symbolic::Address &addr) {
        auto value = std::as_const(*this).read(addr);
        if (value == std::nullopt)
            return std::nullopt;
        return const_cast<symbolic::SymbolicExpr *>(value.value().get());
    }

    void MemoryModel::write(const symbolic::Address &addr,
                            utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> value) {
        if (auto varAddr = llvm::dyn_cast<const symbolic::VariableAddress>(&addr)) {
            memoryMap_variableAddr_.insert_or_assign(*varAddr, std::move(value));
            return;
        } else if (auto symbolAddr = llvm::dyn_cast<const symbolic::SymbolAddress>(&addr)) {
            auto baseInfo = symbolAddr->getBaseInfo();

            auto constOffset = symbolAddr->getOffset()->tryEvalAsConstant();
            auto constLen =
                symbolAddr->isRange() ? symbolAddr->getLength()->tryEvalAsConstant() : std::nullopt;

            if (constOffset && (!symbolAddr->isRange() || constLen)) {
                if (constOffset.value() < 0 || (constLen && constLen.value() <= 0))
                    ERROR("Constant offset must be greater or equal to zero and length must be "
                          "greater than zero.");
                // constant range
                auto unsignedOffset = static_cast<uint64_t>(constOffset.value());
                auto unsignedLen = constLen ? static_cast<uint64_t>(constLen.value()) : uint64_t{1};
                memoryMap_constantRange_.try_emplace(
                    baseInfo, std::map<ConstRange,
                                       utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>>{});
                auto &rangeValueMap = memoryMap_constantRange_.at(baseInfo);
                auto range          = std::pair{unsignedOffset, unsignedOffset + unsignedLen};
                auto rangeForSearch =
                    std::pair{unsignedOffset, std::numeric_limits<uint64_t>::max()};
                auto endIt        = rangeValueMap.end();
                auto upperBoundIt = rangeValueMap.upper_bound(rangeForSearch);
                auto firstLEIt = upperBoundIt == rangeValueMap.begin() ? endIt : prev(upperBoundIt);

                auto &[range_leftBound, range_rightBound] = range;
                if (firstLEIt != endIt && firstLEIt->first.second > range.first) {
                    auto &[firstLE_leftBound, firstLE_rightBound] = firstLEIt->first;
                    auto &firstLE_value                           = firstLEIt->second;
                    if (firstLE_leftBound < range_leftBound) {
                        auto leftRange = std::pair{firstLE_leftBound, range_leftBound};
                        rangeValueMap.insert_or_assign(leftRange, firstLE_value->clone());
                    }
                    if (firstLE_rightBound > range_rightBound) {
                        auto rightRange = std::pair{range_rightBound, firstLE_rightBound};
                        rangeValueMap.insert_or_assign(rightRange, firstLE_value->clone());
                    }
                    rangeValueMap.erase(firstLEIt);
                }
                if (upperBoundIt != endIt && upperBoundIt->first.first < range.second) {
                    auto &[upperBound_leftBound, upperBound_rightBound] = upperBoundIt->first;
                    auto &upperBound_value                              = upperBoundIt->second;
                    if (upperBound_rightBound > range_rightBound) {
                        auto rightRange = std::pair{range_rightBound, upperBound_rightBound};
                        rangeValueMap.insert_or_assign(rightRange, upperBound_value->clone());
                    }
                    rangeValueMap.erase(upperBoundIt);
                }
                rangeValueMap.insert_or_assign(range, std::move(value));
                return;
            }
            // symbolic range
            auto &addrValueMap = memoryMap_symbolicRange_[baseInfo];
            addrValueMap.insert_or_assign(*symbolAddr, std::move(value));
            return;
        } else if (auto fieldAddr = llvm::dyn_cast<const symbolic::FieldAddress>(&addr)) {
            std::visit(
                [&, this](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        TODO();
                    } else if constexpr (std::is_same_v<T, std::pair<utils::not_null<std::unique_ptr<
                                                                         const symbolic::Address>>,
                                                                     const size_t>>) {
                        auto &[baseAddr, index] = arg;
                        auto baseValue          = read(*baseAddr);
                        if (baseValue == std::nullopt)
                            ERROR("Structure isn't existed in MemoryModel, insert it first.");
                        auto baseSt = llvm::dyn_cast<symbolic::Structure>(baseValue.value().get());
                        if (baseSt == nullptr)
                            ERROR("Value of address from a `fieldAddress` is not a structure.");
                        baseSt->setFieldValue(index, std::move(value));
                    }
                },
                fieldAddr->getFrom());
            return;
        }
        UNREACHABLE();
    }

    bool MemoryModel::contains(const symbolic::Address &addr) const {
        return read(addr) ? true : false;
    }

    // MemoryModel::flat_view MemoryModel::flat() { return MemoryModel::flat_view{*this}; }
    const MemoryModel::flat_view MemoryModel::flat() const {
        return MemoryModel::flat_view{const_cast<MemoryModel &>(*this)};
    }

    void MemoryModel::eraseExpiredLocals(
        const std::unordered_set<const clang::VarDecl *> &localVars) {
        std::erase_if(memoryMap_variableAddr_, [&](auto const &kv) {
            auto fromRoot = kv.first.getFromRoot();
            if (fromRoot == std::nullopt)
                TODO();
            return localVars.contains(fromRoot.value());
        });
        std::erase_if(memoryMap_constantRange_, [&](auto const &kv) {
            auto fromRoot = kv.first.getFromRoot();
            if (fromRoot == std::nullopt)
                TODO();
            return localVars.contains(fromRoot.value());
        });
        std::erase_if(memoryMap_symbolicRange_, [&](auto const &kv) {
            auto fromRoot = kv.first.getFromRoot();
            if (fromRoot == std::nullopt)
                TODO();
            return localVars.contains(fromRoot.value());
        });
    }

    void MemoryModel::mergeConstantRanges() {
        using ExprUP = utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>;

        for (auto &[base, cmap] : memoryMap_constantRange_) {
            if (cmap.empty())
                continue;

            // 1) Move to a vector to allow reordering and in-place merging
            std::vector<std::pair<ConstRange, ExprUP>> v;
            v.reserve(cmap.size());
            for (auto &kv : cmap)
                v.emplace_back(kv.first, std::move(kv.second));
            cmap.clear();

            // Sort by offset (ascending)
            auto byOffset = [](auto &a, auto &b) {
                return a.first.first < b.first.first; // compare offset
            };
            std::sort(v.begin(), v.end(), byOffset);

            // 2) Single-pass merge
            std::vector<std::pair<ConstRange, ExprUP>> merged;
            merged.reserve(v.size());

            auto pushOrMerge = [&](std::pair<ConstRange, ExprUP> &&cur) {
                if (merged.empty()) {
                    merged.push_back(std::move(cur));
                    return;
                }
                auto &[pr, pexpr] = merged.back();
                auto &[cr, cexpr] = cur;

                uint64_t pRight = pr.second;
                uint64_t cLeft = cr.first, cRight = cr.second;

                // Adjacent endpoints and equal values → coalesce by extending the length
                if (pRight == cLeft && *pexpr->simplifiedExpr() == *cexpr->simplifiedExpr()) {
                    pr.second = cRight;
                } else {
                    merged.push_back(std::move(cur));
                }
            };

            for (auto &e : v)
                pushOrMerge(std::move(e));

            // 3) Write back (map keeps ranges sorted by key)
            for (auto &e : merged) {
                cmap.emplace(e.first, std::move(e.second));
            }
        }
    }

    void MemoryModel::mergeSymbolicRanges() {
        using SA     = symbolic::SymbolAddress;
        using Expr   = symbolic::SymbolicExpr;
        using ExprUP = utils::not_null<std::unique_ptr<Expr>>;

        auto valueEquivalent = [](const ExprUP &a, const ExprUP &b) -> bool {
            return *a->simplifiedExpr() == *b->simplifiedExpr();
        };

        // Compute hash of (a + b) by constructing a BinaryOp(Add), simplifying, then hashing.
        auto addedHash = [](const Expr &a, const Expr &b) {
            return std::make_unique<symbolic::BinaryOpExpr>(
                       a.clone(), symbolic::BinaryOpExpr::Operator::Add, b.clone())
                ->simplifiedExpr()
                ->hash();
        };

        for (auto &[base, umap] : memoryMap_symbolicRange_) {
            if (umap.empty())
                continue;

            struct Item {
                SA key;                             ///< Symbolic address (offset[/length])
                ExprUP val;                         ///< Stored value expression
                std::optional<uint64_t> constOff{}; ///< If offset folds to constant
                std::optional<uint64_t> constLen{}; ///< If length folds to constant
                size_t offHash{0};                  ///< hash(offset.simplified)
                size_t lenHash{0}; ///< hash(length.simplified) if range (debug/consistency)
                size_t rightHash{
                    0}; ///< hash((offset+length).simplified) or hash(offset+1) for non-range
                size_t valHash{0}; ///< hash(val.simplified) for coarse grouping
                bool used{false};  ///< Whether this edge is already merged into a chain
            };

            std::vector<Item> items;
            items.reserve(umap.size());

            // (1) Move entries to items and precompute hashes/constants
            for (auto &[addr, expr] : umap) {
                Item it{addr, std::move(expr)};

                it.constOff = it.key.getOffset()->tryEvalAsConstant();
                if (it.key.isRange()) {
                    it.constLen = it.key.getLength()->tryEvalAsConstant();
                }

                // Both offset and length constant → should have been inserted into constant map.
                if (it.constOff && it.constLen)
                    ERROR("Offset and length are both constant, they should be inserted into "
                          "memoryMap_constantRange_ instead.");

                // Precompute endpoint/value hashes (using simplified forms)
                it.offHash = it.key.getOffset()->simplifiedExpr()->hash();
                it.valHash = it.val->simplifiedExpr()->hash();
                if (it.key.isRange())
                    it.lenHash = it.key.getLength()->simplifiedExpr()->hash();

                // Right endpoint hash:
                // - range: hash(offset + length)
                // - non-range: hash(offset + 1) → single-address treated as [off, off+1)
                if (it.key.isRange()) {
                    it.rightHash = addedHash(*it.key.getOffset(), *it.key.getLength());
                } else {
                    it.rightHash = addedHash(*it.key.getOffset(), symbolic::LiteralExpr{1});
                }

                items.emplace_back(std::move(it));
            }
            umap.clear();

            // (2) Group by value-hash (coarse buckets to cut comparisons)
            std::unordered_map<size_t, std::vector<size_t>> groups;
            groups.reserve(items.size() * 2); // perf: reduce rehashes; not semantically required
            for (size_t i = 0; i < items.size(); ++i) {
                groups[items[i].valHash].push_back(i);
            }

            // Result map being rebuilt for this BaseInfo
            std::unordered_map<SA, ExprUP> newMap;
            newMap.reserve(items.size());

            // (3) For each value group, build adjacency and emit chains
            for (auto &[vh, idxs] : groups) {
                // Lh → outgoing edges; Rh → incoming edges
                std::unordered_map<size_t, std::vector<size_t>> outByLeft, inByRight;
                outByLeft.reserve(idxs.size() * 2);
                inByRight.reserve(idxs.size() * 2);

                for (size_t i : idxs) {
                    outByLeft[items[i].offHash].push_back(i);
                    inByRight[items[i].rightHash].push_back(i);
                }

                // Try to emit a maximal chain starting from a given edge
                auto tryEmitChain = [&](size_t startIdx) {
                    if (items[startIdx].used)
                        return;

                    // Seed the merged key from the first segment
                    SA mergedKey     = items[startIdx].key;
                    size_t cur       = startIdx;
                    size_t rightHash = items[cur].rightHash;
                    items[cur].used  = true;

                    // Greedily follow a UNIQUE successor whose Lh == current Rh and value equivalent
                    while (true) {
                        auto it = outByLeft.find(rightHash);
                        if (it == outByLeft.end())
                            break;

                        size_t next_idx = SIZE_MAX;
                        for (size_t j : it->second) {
                            if (items[j].used)
                                continue;
                            if (valueEquivalent(items[cur].val, items[j].val)) {
                                if (next_idx == SIZE_MAX) {
                                    next_idx = j;
                                } else {
                                    // Ambiguity (multiple candidates): stop conservatively
                                    UNIMPLEMENT(
                                        "Multiple `symbolAddresses` with the same offset can "
                                        "be merged. Process this when encountered.");
                                }
                            }
                        }
                        if (next_idx == SIZE_MAX)
                            break;

                        auto &itemToBeMerged = items[next_idx];

                        // Append the successor's length:
                        // - If successor is range: add its length expression
                        // - If successor is non-range: add 1
                        if (itemToBeMerged.key.isRange()) {
                            mergedKey.addLength(itemToBeMerged.key.getLength()->clone());
                        } else {
                            mergedKey.addLength(std::make_unique<symbolic::LiteralExpr>(1));
                        }

                        // Advance to successor
                        rightHash           = itemToBeMerged.rightHash;
                        itemToBeMerged.used = true;
                        cur                 = next_idx;
                    }

                    // Emit the merged interval with the value from the starting edge
                    newMap.emplace(std::move(mergedKey), std::move(items[startIdx].val));
                };

                // Prefer starting at "obvious starts": Lh with zero in-degree
                for (auto &[leftHash, outs] : outByLeft) {
                    size_t indeg = 0;
                    if (auto it = inByRight.find(leftHash); it != inByRight.end())
                        indeg = it->second.size();
                    if (indeg == 0) {
                        for (size_t e : outs) {
                            if (!items[e].used)
                                tryEmitChain(e);
                        }
                    } else if (indeg != 1) {
                        // Not an error per se, but indicates possible forks; we log for visibility.
                        WARN("Multiple `symbolAddresses` with the same offset are present.");
                    }
                }

                // Handle remaining edges (cycles or ambiguous starts) conservatively
                for (size_t e : idxs) {
                    if (!items[e].used) {
                        WARN("If this location is reached, it indicates either a cycle is present "
                             "or a merge has multiple candidates, and the result may be incorrect "
                             "or unstable.");
                        tryEmitChain(e);
                    }
                }
            }

            // (4) Write merged map back for this BaseInfo
            memoryMap_symbolicRange_[base] = std::move(newMap);
        }
    }

    ProgramState::ProgramState(std::unique_ptr<Path> initialPath,
                               std::unique_ptr<ACSLFunction> func,
                               context::ACSLContext &context)
        : func_(std::move(func)), context_(context),
          startPoint_(symbolic::SourcePoint::fromFuncDeclBefore(func_->getFunctionDecl(),
                                                                context_.getSourceManager(),
                                                                context_.getLangOptions())) {
        paths_.push_back(std::move(initialPath));
    }

    ProgramState::ProgramState(std::unique_ptr<ACSLFunction> func, context::ACSLContext &context)
        : func_(std::move(func)), context_(context),
          startPoint_(symbolic::SourcePoint::fromFuncDeclBefore(func_->getFunctionDecl(),
                                                                context_.getSourceManager(),
                                                                context_.getLangOptions())) {}

    ProgramState::ProgramState(const ProgramState &other)
        : func_(other.func_->clone()), context_(other.context_), startPoint_(other.startPoint_) {
        paths_.reserve(other.paths_.size());
        std::ranges::transform(other.paths_, std::back_inserter(paths_),
                               [](auto &path) { return path->clone(); });
    }

    ProgramState &ProgramState::operator=(ProgramState &&other) {
        if (&context_ != &other.context_)
            ERROR("Different contexts!");
        func_       = std::move(other.func_);
        paths_      = std::move(other.paths_);
        startPoint_ = std::move(other.startPoint_);
        return *this;
    }

    // @WindOctober: A preliminary scan of the function is also required to identify all
    // global variables (i.e., variables whose scope is greater than or equal
    // to the current function). These variables must then be either properly
    // initialized or subjected to special handling.

    void ProgramState::init() {
        auto FD       = func_->getFunctionDecl();
        auto initPath = std::make_unique<Path>(context_, startPoint_);
        for (const clang::ParmVarDecl *param : FD->parameters()) {
            clang::QualType paramType = param->getType();

            auto paramAddr = initPath->allocMemory(param);
            auto value =
                getSymbol(paramType, paramAddr->addressClone().into_underlying(), startPoint_);
            initPath->updateMemory(*paramAddr, std::move(value));
        }
        paths_.clear();
        paths_.push_back(std::move(initPath));
    }

    void ProgramState::step(const clang::Stmt *stmt) {
        if (!stmt)
            return;
        llvm::TypeSwitch<const clang::Stmt *, void>(stmt)
            .Case<clang::CompoundStmt>([this](const clang::CompoundStmt *cs) {
                DEBUG("stepping clang::CompoundStmt...");
                for (const clang::Stmt *child : cs->children()) {
                    if (child)
                        step(child);
                }
            })
            .Case<clang::IfStmt>([this](const clang::IfStmt *ifStmt) {
                DEBUG("stepping IfStmt...");
                std::vector<const clang::Expr *> branchConds;
                std::vector<const clang::Stmt *> branchStmts;
                branchConds.push_back(ifStmt->getCond());
                branchStmts.push_back(ifStmt->getThen());
                if (ifStmt->getElse())
                    branchStmts.push_back(ifStmt->getElse());
                else
                    branchStmts.push_back(nullptr);

                stepBranch(branchConds, branchStmts);
            })
            .Case<clang::ReturnStmt>([this](const clang::ReturnStmt *retStmt) {
                DEBUG("stepping ReturnStmt...");
                setReturnExpr(retStmt->getRetValue());
                setStates(Path::PathState::Return, NULL);
            })
            .Case<clang::DeclStmt>([this](const clang::DeclStmt *declStmt) {
                DEBUG("stepping clang::DeclStmt...");
                std::vector<const clang::VarDecl *> varDecls;
                for (auto it = declStmt->decl_begin(); it != declStmt->decl_end(); ++it) {
                    clang::Decl *decl = *it;
                    if (!isa<clang::VarDecl>(decl))
                        UNIMPLEMENT("Unhandled clang::Decl type: "s + decl->getDeclKindName());
                    varDecls.push_back(dyn_cast<clang::VarDecl>(decl));
                }
                addNewDecls(varDecls);
            })
            .Case<clang::BinaryOperator>([this](const clang::BinaryOperator *binOp) {
                DEBUG("stepping BinaryOperator...");
                if (utils::ignoreTopBinop(binOp))
                    return;
                if (!utils::isAssignOp(binOp))
                    UNIMPLEMENT("BinaryOperator not implemented: " << binOp->getOpcode());
                updateVarState(binOp);
            })
            .Case<clang::Expr>([this](const clang::Expr *expr) {
                DEBUG("stepping clang::Expr...");
                stepExpr(expr);
            })
            .Case<clang::ImplicitCastExpr>(
                [](const clang::ImplicitCastExpr *) -> std::unique_ptr<symbolic::SymbolicExpr> {
                    UNREACHABLE();
                })
            .Case<clang::CaseStmt>([this](const clang::CaseStmt *caseStmt) {
                DEBUG("stepping CaseStmt...");
                // Can only be met during step(SwitchStmt), just ignore it.
                step(caseStmt->getSubStmt());
            })
            .Case<clang::DefaultStmt>([this](const clang::DefaultStmt *defaultStmt) {
                DEBUG("stepping DefaultStmt...");
                // Can only be met during step(SwitchStmt), just ignore it.
                step(defaultStmt->getSubStmt());
            })
            .Case<clang::SwitchStmt>([this](const clang::SwitchStmt *switchStmt) {
                DEBUG("stepping SwitchStmt...");
                auto prevStmtCtx = this->StmtCtx;
                this->StmtCtx    = switchStmt;

                if (switchStmt->hasInitStorage())
                    UNIMPLEMENT("Unsupported Switch type, Cond has init statement: "
                                << switchStmt->getCond());
                if (auto bodyStmt =
                        dyn_cast_if_present<clang::CompoundStmt>(switchStmt->getBody())) {
                    if (isa_and_present<clang::CaseStmt>(bodyStmt->body_front())) {
                        stepSimpleSwitch(switchStmt);
                    } else {
                        UNIMPLEMENT("Unsupported Switch type, body's first clang::Stmt is not "
                                    "CaseStmt: "
                                    << switchStmt->getBody());
                    }
                } else {
                    WARN("A SwtichStmt without clang::CompoundStmt body (why?) has been "
                         "ignored: "
                         << switchStmt);
                }

                resetState();
                this->StmtCtx = prevStmtCtx;
            })
            .Case<clang::ForStmt>([this](const clang::ForStmt *forStmt) {
                DEBUG("stepping clang::ForStmt...");
                auto prevStmtCtx = this->StmtCtx;
                this->StmtCtx    = forStmt;
                stepLoop(forStmt);
                resetState();
                this->StmtCtx = prevStmtCtx;
            })
            .Case<clang::WhileStmt>([this](const clang::WhileStmt *whileStmt) {
                DEBUG("stepping clang::WhileStmt...");
                auto prevStmtCtx = this->StmtCtx;
                this->StmtCtx    = whileStmt;
                stepLoop(whileStmt);
                resetState();
                this->StmtCtx = prevStmtCtx;
            })
            .Case<clang::DoStmt>([this](const clang::DoStmt *doStmt) {
                DEBUG("stepping clang::DoStmt...");
                auto prevStmtCtx = this->StmtCtx;
                this->StmtCtx    = doStmt;
                stepLoop(doStmt);
                resetState();
                this->StmtCtx = prevStmtCtx;
            })
            .Case<clang::CXXForRangeStmt>([this](const clang::CXXForRangeStmt *rangeStmt) {
                DEBUG("stepping CXXForRangeStmt...");
                auto prevStmtCtx = this->StmtCtx;
                this->StmtCtx    = rangeStmt;
                stepLoop(rangeStmt);
                resetState();
                this->StmtCtx = prevStmtCtx;
            })
            .Case<clang::BreakStmt>([this](const clang::BreakStmt *) {
                DEBUG("stepping BreakStmt...");
                setStates(Path::PathState::Break, StmtCtx);
            })
            .Case<clang::ContinueStmt>([](const clang::ContinueStmt *) {
                DEBUG("stepping ContinueStmt...");
                TODO();
            })
            .Case<clang::NullStmt>([](const clang::NullStmt *) { DEBUG("stepping NullStmt..."); })
            // .Case<UnaryExprOrTypeTraitExpr>([this](const UnaryExprOrTypeTraitExpr *u) ->
            // EvalResult {
            //     SymbolicExpr::Type resultTy = deriveVarType(u->getType());
            //     clang::QualType argTy =
            //         u->isArgumentType() ? u->getArgumentType() :
            //         u->getArgumentExpr()->getType();
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
            //                 UNIMPLEMENT("vec_step on non-std::vector");
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
            .Default([](const clang::Stmt *s) {
                UNIMPLEMENT("Unsupported clang::Stmt type: " << s->getStmtClassName());
            });

        auto localVars = utils::collectLocalVars(stmt);
        for (auto &path : paths_) {
            auto &memoryState = path->getMutMemoryState();
            std::erase_if(path->varAddr_, [&](auto &&kv) { return localVars.contains(kv.first); });
            memoryState.eraseExpiredLocals(localVars);
            memoryState.mergeRanges();
        }
        return;
    }

    Formulas ProgramState::stepExpr(const clang::Expr *expr) {
        std::vector<utils::not_null<std::unique_ptr<Path>>> updatedPaths;
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

    void ProgramState::stepBranch(const std::vector<const clang::Expr *> &branchConds,
                                  const std::vector<const clang::Stmt *> &branchStmts) {
        assert(branchStmts.size() == branchConds.size() + 1);

        std::vector<std::unique_ptr<ProgramState>> clones;
        std::vector<const ProgramState *> statesForMerge;

        auto splitPair = splitActiveInactive();
        size_t n       = branchConds.size();
        for (size_t i = 0; i < n; ++i) {
            auto newState = splitPair.first->clone();

            std::vector<utils::not_null<std::unique_ptr<Path>>> updatedPaths;

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

        std::queue<std::pair<utils::not_null<std::unique_ptr<Path>>, size_t>> worklist;
        std::vector<utils::not_null<std::unique_ptr<Path>>> finalPaths;

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

    void ProgramState::stepLoop(const clang::Stmt *loopStmt) {
        auto preState  = clone();
        auto loopEntry = preState->clone();

        if (auto forLoop = dyn_cast<clang::ForStmt>(loopStmt); forLoop && forLoop->getInit())
            loopEntry->step(forLoop->getInit());

        if (const auto *doWhileStmt = dyn_cast<clang::DoStmt>(loopStmt)) {
            auto cond = doWhileStmt->getCond()->IgnoreParenImpCasts();
            auto body = doWhileStmt->getBody();
            if (auto *literal = llvm::dyn_cast<clang::IntegerLiteral>(cond);
                literal && literal->getValue() == 0) {
                step(body);
                return;
            }
            UNIMPLEMENT("Loop type not supported yet: " << loopStmt->getStmtClassName());
        }

        auto [loopInfo, ok] = spec_generator::parseLoopInfo(*preState, *loopEntry, loopStmt);

        std::string spec;
        std::unique_ptr<ProgramState> postState;
        if (ok) {
            tie(spec, postState) = emitLoopInvariant(*preState, *loopEntry, loopInfo);
        } else {
            parseComplexLoopInfo(*preState, *loopEntry, loopInfo);
            tie(spec, postState) =
                emitLoopInvariant(*preState, *loopEntry, loopInfo, "ComplexLoopInvariant");
        }
        INFO(spec);

        auto beginLoc = loopStmt->getSourceRange().getBegin();
        context_.insertText(beginLoc, spec, /*after*/ false,
                            /*indentNewLines*/ true);

        if (this == postState.get())
            UNREACHABLE();
        *this = std::move(*postState);

        INFO(this->dump());
    }

    void ProgramState::setStates(Path::PathState state, const clang::Stmt *stmt) {
        for (auto &pathPtr : paths_) {
            if (pathPtr->isActive()) {
                pathPtr->setPathState(state);
                pathPtr->StmtCtx = stmt;
            }
        }
    }

    void ProgramState::setReturnExpr(const clang::Expr *expr) {
        if (expr == nullptr) {
            for (auto &pathPtr : paths_) {
                if (!pathPtr->isActive())
                    continue;
                pathPtr->setReturnExpr(std::nullopt);
            }
            return;
        }
        expr = expr->IgnoreParenImpCasts();

        std::vector<utils::not_null<std::unique_ptr<Path>>> updatedPaths;

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

    void ProgramState::updateVarState(const clang::BinaryOperator *binOp) {
        std::vector<utils::not_null<std::unique_ptr<Path>>> updatedPaths;

        for (auto &path : paths_) {
            if (!path->isActive()) {
                updatedPaths.push_back(std::move(path));
                continue;
            }

            Path::EvalResult eval;

            if (binOp->isCompoundAssignmentOp()) {
                symbolic::BinaryOpExpr::Operator op =
                    symbolic::getCompoundAssignOp(binOp->getOpcode());

                Path::EvalResult lhs = path->evalExpr(binOp->getLHS());

                std::vector<utils::not_null<std::unique_ptr<Path>>> outPaths;
                Formulas outExprs;

                for (size_t i = 0; i < lhs.second.size(); ++i) {
                    auto &lhsPath = (i == 0) ? *path : *lhs.first[i - 1];

                    Path::EvalResult rhs = lhsPath.evalExpr(binOp->getRHS());

                    for (size_t j = 0; j < rhs.second.size(); ++j) {
                        outExprs.emplace_back(std::make_unique<symbolic::BinaryOpExpr>(
                            lhs.second[i]->clone(), op, std::move(rhs.second[j])));

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

    void ProgramState::addNewDecls(const std::vector<const clang::VarDecl *> &varDecls) {
        for (const clang::VarDecl *varDecl : varDecls) {
            const clang::Expr *initExpr = varDecl->getInit();
            std::vector<utils::not_null<std::unique_ptr<Path>>> updatedPaths;

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
                        // If varDecl is a struct, memoryState_ should std::map an incomplete
                        // symbolic::Structure (with no field values initialized).
                        if (!RD->isCompleteDefinition())
                            ERROR("Struct with incomplete definition!");

                        RD      = RD->getDefinition();
                        auto st = std::make_unique<symbolic::Structure>(
                            RD, RD->getASTContext().getASTRecordLayout(RD),
                            varAddr_->addressClone().into_underlying(), startPoint_);
                        path->updateVarState(varDecl, std::move(st));
                    }

                    updatedPaths.push_back(std::move(path));
                    continue;
                }
                if (auto initListExpr = dyn_cast<clang::InitListExpr>(initExpr)) {
                    auto varType = varDecl->getType();

                    if (varType->isAnyPointerType() || varType->isArrayType()) {
                        TODO();
                    } else if (auto RD = varType->getAsRecordDecl();
                               RD != nullptr && varType->isStructureType()) {
                        if (!RD->isCompleteDefinition())
                            ERROR("Struct with incomplete definition!");

                        RD = RD->getDefinition();

                        auto st = std::make_unique<symbolic::Structure>(
                            RD, RD->getASTContext().getASTRecordLayout(RD),
                            varAddr_->addressClone().into_underlying(), startPoint_);
                        if (initListExpr->getNumInits() != st->getNumFields())
                            ERROR("Initializer std::list size mismatches the struct's field "
                                  "count.");
                        auto slots = st->fieldsValues();
                        for (size_t i = 0; i < slots.size(); ++i) {
                            const clang::Expr *init = initListExpr->getInit(i);

                            Path::EvalResult eval = path->evalExpr(init);
                            if (eval.second.size() != 1)
                                UNIMPLEMENT(
                                    "No control flow branching permitted within an initializer "
                                    "list now.");
                            slots[i] = std::move(eval.second[0]);
                        }

                        path->updateVarState(varDecl, std::move(st));
                        updatedPaths.push_back(std::move(path));
                        continue;
                    } else {
                        UNIMPLEMENT("An initializer std::list was used to initialize an "
                                    "unimplemented or "
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

    std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<ProgramState>> ProgramState::
        splitActiveInactive() {
        auto activeState           = std::make_unique<ProgramState>(func_->clone(), context_);
        activeState->startPoint_   = startPoint_;
        auto inactiveState         = std::make_unique<ProgramState>(func_->clone(), context_);
        inactiveState->startPoint_ = startPoint_;

        for (auto &path : paths_) {
            if (path->isActive())
                activeState->paths_.push_back(std::move(path));
            else
                inactiveState->paths_.push_back(std::move(path));
        }
        paths_.clear();
        return {std::move(activeState), std::move(inactiveState)};
    }

    std::unique_ptr<ProgramState> ProgramState::merge(
        const std::vector<const ProgramState *> &states) {
        if (states.empty())
            ERROR("Nothing to be merged.");
        std::optional<utils::not_null<std::unique_ptr<ProgramState>>> merged{};

        for (auto &state : states) {
            if (merged == std::nullopt) {
                merged.emplace(state->clone());
                continue;
            }
            if (*state->getFunction() != *merged.value()->getFunction())
                ERROR("States to be merged are dealing with different functions.");
            if (&state->getContext() != &merged.value()->getContext())
                ERROR("States to be merged have different context.");
            if (state->getStartPoint() != merged.value()->getStartPoint())
                ERROR("States to be merged have different start point.");

            for (const auto &path : state->paths_)
                merged.value()->paths_.push_back(path->clone());
        }
        return std::move(merged).value().into_underlying();
    }

    std::unique_ptr<ProgramState> ProgramState::merge(
        const std::vector<std::unique_ptr<ProgramState>> &states) {
        if (states.empty())
            ERROR("Nothing to be merged.");
        std::optional<utils::not_null<std::unique_ptr<ProgramState>>> merged{};

        for (auto &state : states) {
            if (merged == std::nullopt) {
                merged.emplace(state->clone());
                continue;
            }
            if (*state->getFunction() != *merged.value()->getFunction())
                ERROR("States to be merged are dealing with different functions.");
            if (&state->getContext() != &merged.value()->getContext())
                ERROR("States to be merged have different context.");
            if (state->getStartPoint() != merged.value()->getStartPoint())
                ERROR("States to be merged have different start point.");

            for (const auto &path : state->paths_)
                merged.value()->paths_.push_back(path->clone());
        }
        return std::move(merged).value().into_underlying();
    }

    std::unique_ptr<ProgramState> ProgramState::clone(bool withPath) const {
        auto newState         = std::make_unique<ProgramState>(func_->clone(), context_);
        newState->startPoint_ = startPoint_;

        if (withPath) {
            for (const auto &path : paths_) {
                newState->paths_.push_back(path->clone());
            }
        }

        newState->StmtCtx = StmtCtx;
        return newState;
    }

    std::unique_ptr<ProgramState> ProgramState::cloneWithPaths(
        std::vector<std::unique_ptr<Path>> &newPaths) const {
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
    static void collectCaseBlocks(const clang::CompoundStmt *body,
                                  std::vector<std::vector<const clang::Stmt *>> &blocks,
                                  std::vector<const clang::Expr *> &conds) {
        if (!body)
            ERROR("dyn_cast failed for switch body.");
        blocks.clear();
        conds.clear();

        for (const clang::Stmt *top : body->body()) {
            if (auto cs = dyn_cast<const clang::CaseStmt>(top)) {
                auto cur = cs;
                while (cur) {
                    blocks.emplace_back();
                    conds.push_back(cur->getLHS());
                    const clang::Stmt *sub = cur->getSubStmt();
                    if (auto next = dyn_cast<const clang::CaseStmt>(sub)) {
                        cur = next;
                    } else {
                        if (sub) {
                            for (size_t i = 0; i < blocks.size(); ++i)
                                blocks[i].push_back(sub);
                        }
                        break;
                    }
                }
            } else if (auto ds = dyn_cast<const clang::DefaultStmt>(top)) {
                blocks.emplace_back();
                conds.push_back(nullptr);
                const clang::Stmt *sub = ds->getSubStmt();
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

    std::vector<std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<symbolic::SymbolicExpr>>> ProgramState::
        splitStateBySwitchCond(const clang::Expr *switchCond) {
        if (!switchCond) {
            TODO();
        }
        std::vector<std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<symbolic::SymbolicExpr>>>
            result;

        for (auto &path : paths_) {
            auto evalResult = path->evalExpr(switchCond);

            if (evalResult.first.size() != 0) {
                ERROR("evalExpr produced unexpected side paths");
            }

            std::vector<std::unique_ptr<Path>> onePath;
            onePath.push_back(std::move(path).into_underlying());
            auto stateClone = cloneWithPaths(onePath);

            result.emplace_back(std::move(stateClone),
                                std::move(evalResult.second[0]).into_underlying());
        }

        return result;
    }

    void ProgramState::stepSimpleSwitch(const clang::SwitchStmt *switchStmt) {
        auto partitions = splitStateBySwitchCond(switchStmt->getCond());
        std::vector<std::vector<const clang::Stmt *>> blocks;
        std::vector<const clang::Expr *> conds;
        collectCaseBlocks(llvm::dyn_cast<clang::CompoundStmt>(switchStmt->getBody()), blocks,
                          conds);

        std::vector<std::unique_ptr<ProgramState>> finalStates;
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
                Path tmpPath(context_, startPoint_);
                auto caseCondEval = tmpPath.evalExpr(caseCond);
                assert(caseCondEval.second.size() == 1);

                auto caseSymExpr = std::move(caseCondEval.second[0]);
                auto eqState     = current->clone();

                auto condExprEq = std::make_unique<symbolic::BinaryOpExpr>(
                    symValue->clone(), symbolic::BinaryOpExpr::Operator::Equal,
                    caseSymExpr->clone());
                for (auto &p : eqState->paths_)
                    p->insertPathCondition(condExprEq->clone());

                for (auto *s : stmts)
                    eqState->step(s);

                auto condExprNe = std::make_unique<symbolic::BinaryOpExpr>(
                    symValue->clone(), symbolic::BinaryOpExpr::Operator::NotEqual,
                    std::move(caseSymExpr));
                for (auto &p : current->paths_)
                    p->insertPathCondition(condExprNe->clone());

                if (eqState->isInactive()) {
                    finalStates.push_back(std::move(eqState));
                } else {
                    std::vector<const ProgramState *> mergeInputs;
                    mergeInputs.push_back(eqState.get());
                    mergeInputs.push_back(current.get());
                    auto merged = merge(mergeInputs);
                    current     = std::move(merged);
                }
            }

            finalStates.push_back(std::move(current));
        }

        std::vector<const ProgramState *> ptrs;
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

    void ProgramState::resymbolize(symbolic::SourcePoint newStartPoint) {
        startPoint_ = std::move(newStartPoint);
        for (auto &path : paths_) {
            if (path->isActive()) {
                auto temp = std::move(path);
                temp->resymbolize(startPoint_);
                paths_.clear();
                paths_.push_back(std::move(temp));
                return;
            }
        }
    }

    std::optional<utils::not_null<std::unique_ptr<Path>>> ProgramState::takePath(size_t i) {
        if (i >= paths_.size())
            return std::nullopt;
        auto p = std::move(paths_.at(i));
        paths_.erase(paths_.begin() + i);
        return p;
    }

    std::vector<utils::not_null<std::unique_ptr<Path>>> ProgramState::takeAllPaths() {
        std::vector<utils::not_null<std::unique_ptr<Path>>> out;
        out.swap(paths_);
        return out;
    }
} // namespace acslg::analyzer