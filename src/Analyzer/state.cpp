#include "state.h"
#include "macros.h"
#include <clang/AST/Expr.h>
#include "clang/AST/Stmt.h"
#include "clang/AST/Decl.h"
#include "llvm/ADT/TypeSwitch.h"
#include <queue>
#include <unordered_map>
#include <memory>
#include <set>
#include "Utils/utils.h"
#include <llvm/ADT/APSInt.h>
#include <clang/AST/StmtCXX.h>
#include "Context/globalSM.h"
#include <variant>

using namespace std;
using namespace clang;
using namespace llvm;
using namespace Symbolic;

using LValueTarget = variant<const VarDecl *, unique_ptr<Address>>;

void Path::LoopInit(unordered_map<Symbolic::Variable *, unique_ptr<SymbolicExpr>> &initMap)
{
    initMap.clear();
    for (auto &entry : varAddr)
    {
        const VarDecl *varDecl = entry.first;
        QualType varType       = varDecl->getType();
        Address *addr          = entry.second.get();
        auto memIt             = memoryState.find(*addr);
        if (memIt == memoryState.end())
            ERROR("No memory state entry for allocated address");

        if (!varType->isPointerType() && !varType->isArrayType())
        {
            unique_ptr<SymbolicExpr> origExpr = std::move(memIt->second);

            SymbolicExpr::Type derived       = deriveVarType(varType);
            unique_ptr<SymbolicExpr> newExpr = make_unique<Symbolic::Variable>(
                varDecl->getNameAsString(), derived, symbolVarCounter++);

            memIt->second = std::move(newExpr);

            Symbolic::Variable *varPtr = dynamic_cast<Symbolic::Variable *>(memIt->second.get());
            if (varPtr)
                initMap[varPtr] = std::move(origExpr);
        }
        else if (varType->isPointerType())
        {
            unique_ptr<SymbolicExpr> ptrOrigExpr = std::move(memIt->second);
            Address *pointeeAddr                 = dynamic_cast<Address *>(ptrOrigExpr.get());
            if (!pointeeAddr)
                ERROR("Pointer stored value is not an Address");

            auto memItPointee = memoryState.find(*pointeeAddr);
            if (memItPointee == memoryState.end())
                ERROR("No memory state entry for pointee");
            unique_ptr<SymbolicExpr> pointeeOrigExpr = std::move(memItPointee->second);

            QualType baseType                       = varType->getPointeeType();
            SymbolicExpr::Type baseDerived          = deriveVarType(baseType);
            unique_ptr<SymbolicExpr> newPointeeExpr = make_unique<Symbolic::Variable>(
                "*" + varDecl->getNameAsString(), baseDerived, symbolVarCounter++);
            memItPointee->second = std::move(newPointeeExpr);
            Symbolic::Variable *varPtr =
                dynamic_cast<Symbolic::Variable *>(memItPointee->second.get());
            if (varPtr)
                initMap[varPtr] = std::move(pointeeOrigExpr);
            // Restore the pointer's memoryState entry.
            memIt->second = std::move(ptrOrigExpr);
        }
    }
}

LValueTarget Path::extractLValue(const Expr *lhs)
{
    const Expr *lexpr = lhs->IgnoreImpCasts();
    if (auto *declRef = dyn_cast<DeclRefExpr>(lexpr))
        return dyn_cast<VarDecl>(declRef->getDecl());

    if (auto *arr = dyn_cast<ArraySubscriptExpr>(lexpr))
    {
        auto baseLVal = extractLValue(arr->getBase());
        Address *addr;
        if (auto declPtr = get_if<const VarDecl *>(&baseLVal))
            addr = varAddr[*declPtr].get();
        else
            addr = get<unique_ptr<Address>>(baseLVal).get();
        auto idxEval = evalExpr(arr->getIdx());
        auto idxExpr = std::move(idxEval.second[0]);
        return addr->addOffset(std::move(idxExpr));
    }

    if (auto *uop = dyn_cast<UnaryOperator>(lexpr))
    {
        if (uop->getOpcode() == UO_Deref)
        {
            auto addrEval = evalExpr(uop->getSubExpr());
            if (addrEval.second.empty())
                UNIMPLEMENT("Failed to evaluate address in deref");

            auto addr = addrEval.second[0]->clone();
            if (!addr)
                UNIMPLEMENT("Expected Address in deref, got: " << addrEval.second[0]->dump());
            auto *raw = static_cast<Address *>(addr.release());
            return unique_ptr<Address>(raw);
        }
    }

    UNIMPLEMENT("Unsupported LHS expression: " << lexpr->getStmtClassName());
}

unique_ptr<Address> Path::extractAddress(const Expr *lhs)
{
    auto lv = extractLValue(lhs);
    if (auto varPtr = get_if<const VarDecl *>(&lv))
        return unique_ptr<Address>(static_cast<Address *>(varAddr[*varPtr]->clone().release()));
    if (auto addrPtr = get_if<unique_ptr<Address>>(&lv))
        return std::move(*addrPtr);
    UNIMPLEMENT("extractAddress: unsupported lvalue for address");
}

unique_ptr<SymbolicExpr> Path::getVarState(const VarDecl *var)
{
    auto canonicalVar = var->getCanonicalDecl();
    auto varIt        = varAddr.find(canonicalVar);
    if (varIt == varAddr.end())
        ERROR("Variable '" + canonicalVar->getNameAsString() + "' has no allocated address");
    auto addr  = varIt->second.get();
    auto memIt = memoryState.find(*addr);
    if (memIt == memoryState.end())
        ERROR("No memory state entry for allocated address");
    return memIt->second->clone();
}

const vector<unique_ptr<SymbolicExpr>> &Path::getPathConditions() const { return pathConditions; }

Address *Path::allocMemory(const VarDecl *var)
{
    auto canonicalVar = var->getCanonicalDecl();
    if (varAddr.find(canonicalVar) != varAddr.end())
        ERROR("Variable already has allocated memory");
    auto newAddr    = make_unique<Address>(addrCounter++);
    Address *rawPtr = newAddr.get();
    rawPtr->setVarDecl(var);
    varAddr.emplace(canonicalVar, std::move(newAddr));

    memoryState.emplace(*rawPtr, make_unique<NullExpr>());
    return rawPtr;
}

Address *Path::allocMemory() { return new Address(addrCounter++); }

void Path::updateMemory(Address *addr, unique_ptr<SymbolicExpr> expr)
{
    if (!expr)
        expr = make_unique<NullExpr>();
    memoryState[*addr] = std::move(expr);
}

void Path::updateVarState(const VarDecl *var, unique_ptr<SymbolicExpr> expr)
{
    unique_ptr<SymbolicExpr> exprPtr;
    if (expr)
        exprPtr = std::move(expr);
    else
        exprPtr = make_unique<NullExpr>();

    auto canonicalVar = var->getCanonicalDecl();
    auto addrIt       = varAddr.find(canonicalVar);
    if (addrIt == varAddr.end())
        ERROR("Variable has no allocated address");

    auto &addr         = addrIt->second;
    memoryState[*addr] = std::move(exprPtr);
}

void Path::insertPathCondition(unique_ptr<SymbolicExpr> cond)
{
    if (!cond)
        return;
    pathConditions.push_back(std::move(cond));
}

unique_ptr<Path> Path::clone() const
{
    auto cloned          = make_unique<Path>();
    cloned->currentState = currentState;
    for (const auto &entry : varAddr)
        cloned->varAddr.emplace(entry.first,
            unique_ptr<Address>(static_cast<Address *>(entry.second->clone().release())));
    for (const auto &entry : memoryState)
        cloned->memoryState.emplace(entry.first, entry.second->clone());
    for (const auto &cond : pathConditions)
        cloned->pathConditions.push_back(cond->clone());
    cloned->returnExpr  = returnExpr->clone();
    cloned->addrCounter = addrCounter;
    cloned->StmtCtx     = StmtCtx;
    return cloned;
}

Path::EvalResult Path::evalExpr(const Expr *expr)
{
    if (!expr)
        ERROR("Fail to convert an empty Expr");

    expr = expr->IgnoreParenImpCasts();
    return TypeSwitch<const Expr *, EvalResult>(expr)
        .Case<IntegerLiteral>([](const IntegerLiteral *lit) -> EvalResult {
            APInt ap         = lit->getValue();
            QualType litType = lit->getType();
            unique_ptr<SymbolicExpr> result;

            if (litType->isBooleanType())
            {
                result = make_unique<LiteralExpr>(static_cast<bool>(ap.getZExtValue()));
            }
            else if (litType->isUnsignedIntegerType())
            {
                if (ap.getBitWidth() <= 8)
                    result =
                        make_unique<LiteralExpr>(static_cast<unsigned char>(ap.getZExtValue()));
                else if (ap.getBitWidth() <= 16)
                    result =
                        make_unique<LiteralExpr>(static_cast<unsigned short>(ap.getZExtValue()));
                else if (ap.getBitWidth() <= 32)
                    result = make_unique<LiteralExpr>(static_cast<unsigned int>(ap.getZExtValue()));
                else if (ap.getBitWidth() <= 64)
                    result = make_unique<LiteralExpr>(static_cast<uint64_t>(ap.getZExtValue()));
                else
                    UNIMPLEMENT("Unsupported unsigned integer literal with bit width > 64: "
                                << ap.getBitWidth());
            }
            else
            {
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
            vector<unique_ptr<SymbolicExpr>> exprs;
            exprs.reserve(1);
            exprs.push_back(std::move(result));

            return {std::move(paths), std::move(exprs)};
        })
        .Case<BinaryOperator>([this](const BinaryOperator *binOp) -> EvalResult {
            // TODO: maybe pack the logic in BO, ArraySub into a function?
            EvalResult lhs            = evalExpr(binOp->getLHS());
            BinaryOpExpr::Operator op = getBinaryOp(binOp->getOpcode());

            vector<unique_ptr<Path>> outPaths;
            vector<unique_ptr<SymbolicExpr>> outExprs;

            size_t lhsCount = lhs.second.size();
            for (size_t i = 0; i < lhsCount; ++i)
            {
                auto lhsExpr    = std::move(lhs.second[i]);
                Path *path      = (i == 0) ? this : lhs.first[i - 1].get();
                EvalResult rhs  = path->evalExpr(binOp->getRHS());
                size_t rhsCount = rhs.second.size();

                for (size_t j = 0; j < rhsCount; ++j)
                {
                    unique_ptr<SymbolicExpr> rhsExpr = std::move(rhs.second[j]);

                    outExprs.emplace_back(
                        make_unique<BinaryOpExpr>(lhsExpr->clone(), op, std::move(rhsExpr)));

                    if (i == 0 && j == 0)
                        continue;

                    outPaths.emplace_back(
                        j == 0 ? std::move(lhs.first[i - 1]) : std::move(rhs.first[j - 1]));
                }
            }

            return {std::move(outPaths), std::move(outExprs)};
        })
        .Case<ParenExpr>(
            [this](const ParenExpr *paren) -> EvalResult { return evalExpr(paren->getSubExpr()); })
        .Case<DeclRefExpr>([this](const DeclRefExpr *declRef) -> EvalResult {
            if (const auto *varDecl = dyn_cast<VarDecl>(declRef->getDecl()))
            {
                auto varExpr = getVarState(varDecl);
                vector<unique_ptr<Path>> paths;
                vector<unique_ptr<SymbolicExpr>> exprs;
                exprs.push_back(std::move(varExpr));
                return {std::move(paths), std::move(exprs)};
            }

            if (const auto *enumDecl = dyn_cast<EnumConstantDecl>(declRef->getDecl()))
            {
                APSInt value = enumDecl->getInitVal();
                auto litExpr = make_unique<LiteralExpr>(static_cast<int>(value.getSExtValue()));
                vector<unique_ptr<Path>> paths;
                vector<unique_ptr<SymbolicExpr>> exprs;
                exprs.push_back(std::move(litExpr));
                return {std::move(paths), std::move(exprs)};
            }

            UNIMPLEMENT("Unsupported Decl type: " << declRef->getDecl()->getDeclKindName());
        })
        .Case<ArraySubscriptExpr>([this](const ArraySubscriptExpr *arrSub) -> EvalResult {
            LValueTarget lval = extractLValue(arrSub->getBase());
            string baseName;
            if (auto varPtr = get_if<const VarDecl *>(&lval))
                baseName = (*varPtr)->getNameAsString();
            else
                UNIMPLEMENT("Array base is not a single Variable");

            unique_ptr<Address> addr   = extractAddress(arrSub->getBase());
            EvalResult idx             = evalExpr(arrSub->getIdx());
            SymbolicExpr::Type varType = deriveVarType(arrSub->getBase()->getType());

            vector<unique_ptr<Path>> outPaths;
            vector<unique_ptr<SymbolicExpr>> outExprs;

            for (size_t i = 0; i < idx.second.size(); ++i)
            {
                auto idxExpr   = std::move(idx.second[i]);
                string idxDump = idxExpr->dump();
                auto newAddr   = addr->addOffset(std::move(idxExpr));
                auto it        = memoryState.find(*newAddr);
                if (it == memoryState.end())
                {
                    string varName = baseName + "[" + idxDump +
                                     "]"; // TODO: impl function to get pointer/array base name.
                    auto varExpr =
                        make_unique<Symbolic::Variable>(varName, varType, symbolVarCounter++);
                    it = memoryState.emplace(*newAddr, varExpr->clone()).first;
                    outExprs.emplace_back(std::move(varExpr));
                }
                else
                {
                    outExprs.emplace_back(it->second->clone());
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
                "llvm.lifetime.end", "printf", "__assert_fail"};
            string name                          = callee->getNameAsString();
            if (ignoreNames.count(name))
                return Path::EvalResult{};

            // TODO: complete the logic to call function.
            UNIMPLEMENT("Unhandled CallExpr to function: " << name);
        })
        .Case<ConditionalOperator>([this](const ConditionalOperator *condOp) -> EvalResult {
            EvalResult cond = evalExpr(condOp->getCond());

            vector<unique_ptr<Path>> outPaths;
            vector<unique_ptr<SymbolicExpr>> outExprs;

            for (size_t i = 0; i < cond.second.size(); ++i)
            {
                Path *condPath = (i == 0) ? this : cond.first[i - 1].get();
                auto condExpr  = std::move(cond.second[i]);

                // false branch
                auto falsePath = condPath->clone();
                auto negatedCond =
                    make_unique<UnaryOpExpr>(UnaryOpExpr::Operator::LogicalNot, condExpr->clone());
                falsePath->insertPathCondition(std::move(negatedCond));

                EvalResult falseVal = falsePath->evalExpr(condOp->getFalseExpr());

                // true branch
                auto truePath = condPath;
                truePath->insertPathCondition(std::move(condExpr));

                EvalResult trueVal = truePath->evalExpr(condOp->getTrueExpr());

                // merge results
                for (size_t j = 0; j < trueVal.second.size(); ++j)
                {
                    outExprs.emplace_back(std::move(trueVal.second[j]));
                    if (j > 0)
                        outPaths.emplace_back(std::move(trueVal.first[j - 1]));
                }
                for (size_t j = 0; j < falseVal.second.size(); ++j)
                {
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
            vector<unique_ptr<SymbolicExpr>> outExprs;

            UnaryOpExpr::Operator op;
            switch (uop->getOpcode())
            {
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
                UNIMPLEMENT(
                    "Unsupported UnaryOperator: " << uop->getOpcodeStr(uop->getOpcode()).str());
            }

            for (size_t i = 0; i < operand.second.size(); ++i)
            {
                Path *path  = (i == 0 ? this : operand.first[i - 1].get());
                auto unExpr = std::move(operand.second[i]);

                if (op == UnaryOpExpr::Operator::PreInc || op == UnaryOpExpr::Operator::PostInc ||
                    op == UnaryOpExpr::Operator::PreDec || op == UnaryOpExpr::Operator::PostDec)
                {
                    // ++x / x++ / --x / x--
                    unique_ptr<Address> addr = extractAddress(uop->getSubExpr());
                    if (!addr)
                        ERROR("extractAddress returned null");
                    // old value
                    auto oldVal = path->memoryState[*addr]->clone();
                    // compute new = old +/- 1
                    auto one    = make_unique<LiteralExpr>(1);
                    auto binOp  = (op == UnaryOpExpr::Operator::PreInc ||
                                     op == UnaryOpExpr::Operator::PostInc)
                                      ? BinaryOpExpr::Operator::Add
                                      : BinaryOpExpr::Operator::Subtract;
                    auto newVal = make_unique<BinaryOpExpr>(oldVal->clone(), binOp, std::move(one));
                    // write back
                    path->memoryState[*addr] = newVal->clone();
                    // return pre vs post
                    if (op == UnaryOpExpr::Operator::PreInc || op == UnaryOpExpr::Operator::PreDec)
                        outExprs.emplace_back(std::move(newVal));
                    else
                        outExprs.emplace_back(std::move(oldVal));
                }
                else if (op == UnaryOpExpr::Operator::Dereference)
                {
                    // *x
                    auto se = evalExpr(uop->getSubExpr());
                    if (!se.first.empty() || se.second.size() != 1)
                        UNIMPLEMENT("Dereference produced unexpected side paths");
                    auto addr = dynamic_cast<Address *>(se.second[0].get());
                    if (!addr)
                        UNIMPLEMENT("Expected Address*, got: " << se.second[0]->dump());

                    auto writedAddr = addr->addOffset(make_unique<LiteralExpr>(0U));

                    if (auto it = memoryState.find(*writedAddr); it == memoryState.end())
                    {
                        SymbolicExpr::Type varType = deriveVarType(uop->getSubExpr()->getType());
                        string varName             = writedAddr->getBaseName() + "[" +
                                         writedAddr->getOffset()->dump() +
                                         "]"; // TODO: impl function to get pointer/array base name.
                        auto varExpr = std::make_unique<Symbolic::Variable>(
                            varName, varType, symbolVarCounter++);
                        it = memoryState.emplace(*writedAddr, varExpr->clone()).first;
                        outExprs.emplace_back(std::move(varExpr));
                    }
                    else
                    {
                        outExprs.emplace_back(it->second->clone());
                    }
                }
                else if (op == UnaryOpExpr::Operator::AddrOf)
                    TODO();
                else
                    outExprs.emplace_back(make_unique<UnaryOpExpr>(op, std::move(unExpr)));

                if (i > 0)
                    outPaths.emplace_back(std::move(operand.first[i - 1]));
            }

            return {std::move(outPaths), std::move(outExprs)};
        })
        .Case<CStyleCastExpr>([this](const CStyleCastExpr *castExpr) -> EvalResult {
            auto sub        = evalExpr(castExpr->getSubExpr());
            auto targetType = deriveVarType(castExpr->getType());

            for (auto &subExpr : sub.second)
                subExpr->setValType(targetType);

            return {std::move(sub.first), std::move(sub.second)};
        })
        .Default([](const Expr *e) -> EvalResult {
            UNIMPLEMENT("Unsupported Expr type: " << e->getStmtClassName());
        });
}
string Path::dump() const
{
    ostringstream oss;

    oss << "\nPath State: " << [&]() {
        switch (currentState)
        {
        case Path::PathState::Step: return "Step";
        case Path::PathState::Continue: return "Continue";
        case Path::PathState::Break: return "Break";
        case Path::PathState::Return: return "Return";
        default: return "Unknown";
        }
    }() << "\n";
    oss << "Address Counter: " << addrCounter << "\n";

    oss << "Return Expression: ";
    if (returnExpr)
        oss << returnExpr->dump();
    else
        oss << "null";
    oss << "\n";

    oss << "Path Conditions:\n";
    for (size_t i = 0; i < pathConditions.size(); ++i)
    {
        oss << "  [" << i << "]: " << (pathConditions[i] ? pathConditions[i]->dump() : "null")
            << "\n";
    }

    unordered_set<Address, AddressHash> printedAddrs;

    oss << "Variable Address Mapping:\n";
    for (auto const &pair : varAddr)
    {
        const VarDecl *vd = pair.first;
        string name;
        if (auto opt = GlobalSM::getDeclInfo(vd))
            tie(name, ignore, ignore, ignore, ignore) = *opt;

        oss << "  @" << name << " -> " << pair.second->dump();

        auto memIt = memoryState.find(*pair.second);
        if (memIt != memoryState.end() && memIt->second)
        {
            oss << " -> " << memIt->second->dump();
            printedAddrs.insert(memIt->first);
        }
        else
        {
            oss << " -> null";
        }
        oss << "\n";
    }

    if (printedAddrs.size() < memoryState.size())
    {
        oss << "Memory State:\n";
        for (auto const &pair : memoryState)
        {
            if (printedAddrs.count(pair.first) == 0)
            {
                oss << "  " << pair.first.dump() << " -> "
                    << (pair.second ? pair.second->dump() : "null") << "\n";
            }
        }
    }

    if (StmtCtx)
    {
        if (auto opt = GlobalSM::getStmtInfo(StmtCtx))
        {
            StringRef sourceText;
            tie(sourceText, ignore, ignore, ignore) = *opt;
            if (!sourceText.empty())
            {
                oss << "Stmt Context: " << sourceText.str() << "\n";
            }
        }
    }
    return oss.str();
}

bool Path::isUnchangedState(Address addr)
{
    auto memIt = memoryState.find(addr);
    if (memIt == memoryState.end())
        return false;

    SymbolicExpr *stored = memIt->second.get();
    if (stored->getType() != SymbolicExpr::ExprType::Variable)
        return false;

    Symbolic::Variable *var = static_cast<Symbolic::Variable *>(stored);

    if (!addr.hasVarDecl())
        ERROR("Cannot resolve base VarDecl from address id");
    string baseName = addr.getBaseName();

    // Step 2: compare name
    if (!addr.isOffseted())
    {
        return var->getName() == baseName;
    }
    else
    {
        string expected = baseName + "[" + addr.getOffset()->dump() + "]";
        return var->getName() == expected;
    }
}

ProgramState::ProgramState(unique_ptr<Path> initialPath, ACSLFunction *context)
{
    paths.push_back(std::move(initialPath));
    Context = unique_ptr<ACSLFunction>(context);
}

ProgramState::ProgramState(ACSLFunction *context) { Context = unique_ptr<ACSLFunction>(context); }

void ProgramState::init()
{
    auto FD = Context->getFunctionDecl();
    paths.clear();
    paths.push_back(make_unique<Path>());

    for (const ParmVarDecl *param : FD->parameters())
    {
        QualType paramType = param->getType();
        if (!paramType->isPointerType() && !paramType->isArrayType())
        {
            Address *addr = paths[0]->allocMemory(param);

            SymbolicExpr::Type varType       = deriveVarType(paramType);
            unique_ptr<SymbolicExpr> varExpr = make_unique<Symbolic::Variable>(
                param->getNameAsString(), varType, paths[0]->getNextSymVarId());

            paths[0]->updateMemory(addr, std::move(varExpr));
        }
        else if (paramType->isPointerType())
        {
            QualType baseType = paramType->getPointeeType();
            if (baseType->isPointerType() || baseType->isArrayType())
                UNIMPLEMENT("Unsupported pointer to pointer/array");

            Address *ptrAddr     = paths[0]->allocMemory(param);
            Address *pointeeAddr = paths[0]->allocMemory();
            pointeeAddr->setVarDecl(param);

            unique_ptr<SymbolicExpr> pointerValue = pointeeAddr->clone();
            paths[0]->updateMemory(ptrAddr, std::move(pointerValue));

            // SymbolicExpr::Type varType              = deriveVarType(baseType);
            // unique_ptr<SymbolicExpr> pointeeVarExpr = make_unique<Symbolic::Variable>(
            //     "*" + param->getNameAsString(), varType, paths[0]->getNextSymVarId());
            // paths[0]->updateMemory(pointeeAddr, std::move(pointeeVarExpr));
        }
        else if (paramType->isArrayType())
        {
            TODO();
        }
    }
}

void ProgramState::step(const Stmt *stmt)
{
    if (!stmt)
        return;

    TypeSwitch<const Stmt *, void>(stmt)
        .Case<CompoundStmt>([this](const CompoundStmt *cs) {
            for (const Stmt *child : cs->children())
            {
                if (child)
                    step(child);
            }
        })
        .Case<IfStmt>([this](const IfStmt *ifStmt) {
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
            setReturnExpr(retStmt->getRetValue());
            setStates(Path::PathState::Return, NULL);
        })
        .Case<DeclStmt>([this](const DeclStmt *declStmt) {
            vector<const VarDecl *> varDecls;
            for (auto it = declStmt->decl_begin(); it != declStmt->decl_end(); ++it)
            {
                Decl *decl = *it;
                if (!dyn_cast<VarDecl>(decl))
                {
                    WARN(string("Unhandled Decl type: ") + decl->getDeclKindName());
                    continue;
                }
                varDecls.push_back(dyn_cast<VarDecl>(decl));
            }
            addNewDecls(varDecls);
        })
        .Case<BinaryOperator>([this](const BinaryOperator *binOp) {
            if (ignoreTopBinop(binOp))
                return;
            if (!isAssignOp(binOp))
                UNIMPLEMENT("BinaryOperator not implemented: " << binOp->getOpcode());
            updateVarState(binOp);
        })
        .Case<Expr>([this](const Expr *expr) { stepExpr(expr); })
        .Case<ImplicitCastExpr>(
            [](const ImplicitCastExpr *) -> unique_ptr<SymbolicExpr> { UNREACHABLE(); })
        .Case<CaseStmt>([this](const CaseStmt *caseStmt) {
            // Can only be met during step(SwitchStmt), just ignore it.
            step(caseStmt->getSubStmt());
        })
        .Case<DefaultStmt>([this](const DefaultStmt *defaultStmt) {
            // Can only be met during step(SwitchStmt), just ignore it.
            step(defaultStmt->getSubStmt());
        })
        .Case<SwitchStmt>([this](const SwitchStmt *switchStmt) {
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx    = switchStmt;

            if (switchStmt->hasInitStorage())
                UNIMPLEMENT(
                    "Unsupported Switch type, Cond has init statement: " << switchStmt->getCond());
            if (auto bodyStmt = dyn_cast_if_present<CompoundStmt>(switchStmt->getBody()))
            {
                if (isa_and_present<CaseStmt>(bodyStmt->body_front()))
                {
                    stepSimpleSwitch(switchStmt);
                }
                else
                {
                    UNIMPLEMENT("Unsupported Switch type, body's first Stmt is not CaseStmt: "
                                << switchStmt->getBody());
                }
            }
            else
            {
                WARN("A SwtichStmt without CompoundStmt body (why?) has been ignored: "
                     << switchStmt);
            }

            resetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<ForStmt>([this](const ForStmt *forStmt) {
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx    = forStmt;
            stepLoop(forStmt);
            resetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<WhileStmt>([this](const WhileStmt *whileStmt) {
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx    = whileStmt;
            stepLoop(whileStmt);
            resetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<DoStmt>([this](const DoStmt *doStmt) {
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx    = doStmt;
            stepLoop(doStmt);
            resetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<CXXForRangeStmt>([this](const CXXForRangeStmt *rangeStmt) {
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx    = rangeStmt;
            stepLoop(rangeStmt);
            resetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<BreakStmt>([this](const BreakStmt *) { setStates(Path::PathState::Break, StmtCtx); })
        .Case<ContinueStmt>([](const ContinueStmt *) { TODO(); })
        .Default(
            [](const Stmt *s) { UNIMPLEMENT("Unsupported Stmt type: " << s->getStmtClassName()); });
    return;
}

std::vector<std::unique_ptr<SymbolicExpr>> ProgramState::stepExpr(const Expr *expr)
{
    std::vector<std::unique_ptr<Path>> updatedPaths;
    std::vector<std::unique_ptr<SymbolicExpr>> evaluated;

    for (auto &path : paths)
    {
        auto [newPathGroup, exprGroup] = path->evalExpr(expr);

        updatedPaths.push_back(std::move(path));
        evaluated.push_back(std::move(exprGroup[0]));

        for (size_t i = 0; i < newPathGroup.size(); ++i)
        {
            updatedPaths.push_back(std::move(newPathGroup[i]));
            evaluated.push_back(std::move(exprGroup[i + 1]));
        }
    }

    paths = std::move(updatedPaths);
    return evaluated;
}

void ProgramState::stepBranch(
    const vector<const Expr *> &branchConds, const vector<const Stmt *> &branchStmts)
{
    assert(branchStmts.size() == branchConds.size() + 1);

    vector<unique_ptr<ProgramState>> clones;
    vector<const ProgramState *> statesForMerge;

    auto splitPair = splitActiveInactive();
    size_t n       = branchConds.size();
    for (size_t i = 0; i < n; ++i)
    {
        auto newState = splitPair.first->clone();

        vector<unique_ptr<Path>> updatedPaths;

        for (auto &path : newState->paths)
        {
            Path::EvalResult eval = path->evalExpr(branchConds[i]);

            size_t m = eval.second.size();
            for (size_t j = 0; j < m; ++j)
            {
                unique_ptr<Path> newPath =
                    (j == 0) ? std::move(path) : std::move(eval.first[j - 1]);

                newPath->insertPathCondition(std::move(eval.second[j]));
                updatedPaths.push_back(std::move(newPath));
            }
        }

        newState->paths = std::move(updatedPaths);

        newState->step(branchStmts[i]);

        statesForMerge.push_back(newState.get());
        clones.push_back(std::move(newState));
    }

    queue<pair<unique_ptr<Path>, size_t>> worklist;
    vector<unique_ptr<Path>> finalPaths;

    for (auto &path : splitPair.first->paths)
    {
        worklist.emplace(std::move(path), 0);
    }

    while (!worklist.empty())
    {
        auto [path, idx] = std::move(worklist.front());
        worklist.pop();

        if (idx == branchConds.size())
        {
            finalPaths.push_back(std::move(path));
            continue;
        }

        Path::EvalResult eval = path->evalExpr(branchConds[idx]);

        for (size_t j = 0; j < eval.second.size(); ++j)
        {
            unique_ptr<Path> newPath = (j == 0) ? std::move(path) : std::move(eval.first[j - 1]);

            newPath->insertPathCondition(createLNotExpr(std::move(eval.second[j])));
            worklist.emplace(std::move(newPath), idx + 1);
        }
    }

    splitPair.first->paths = std::move(finalPaths);
    splitPair.first->step(branchStmts.back());

    statesForMerge.push_back(splitPair.first.get());
    auto mergedActive = merge(statesForMerge);
    for (auto &path : splitPair.second->paths)
        mergedActive->paths.push_back(std::move(path));

    paths = std::move(mergedActive->paths);
}

void ProgramState::stepLoop(const Stmt *loopStmt)
{
    const Stmt *init = nullptr;
    const Expr *cond = nullptr;
    const Stmt *body = nullptr;
    const Stmt *inc  = nullptr;

    if (const auto *forStmt = dyn_cast<ForStmt>(loopStmt))
    {
        init = forStmt->getInit();
        cond = forStmt->getCond();
        body = forStmt->getBody();
        inc  = forStmt->getInc();
    }
    else if (const auto *whileStmt = dyn_cast<WhileStmt>(loopStmt))
    {
        cond = whileStmt->getCond();
        body = whileStmt->getBody();
    }
    else
    {
        UNIMPLEMENT("Loop type not supported yet: " << loopStmt->getStmtClassName());
        return;
    }

    vector<const VarDecl *> indexVars = determineIndexVars(init, cond, body, inc);
    vector<unique_ptr<ProgramState>> newStates;
    for (auto &p : paths)
    {
        unique_ptr<ProgramState> newState =
            make_unique<ProgramState>(std::move(p), Context->clone());
        newState->loopIndexes = indexVars;

        newState->step(init);

        unordered_map<Symbolic::Variable *, unique_ptr<SymbolicExpr>> loopInitMap;
        newState->paths.back()->LoopInit(loopInitMap);

        newState->step(body);
        newState->step(inc);
        INFO(newState->dump());
    }
    paths.clear();

    for (auto &state : newStates)
    {
        for (auto &p : state->paths)
        {
            paths.push_back(std::move(p));
        }
    }

    // @WindOctober: process loop post state.
    TODO();
}

vector<const VarDecl *>
ProgramState::determineIndexVars(const Stmt *, const Expr *, const Stmt *, const Stmt *)
{
    return vector<const VarDecl *>{};
}

void ProgramState::setStates(Path::PathState state, const Stmt *stmt)
{
    for (auto &pathPtr : paths)
    {
        if (pathPtr->isActive())
        {
            pathPtr->setPathState(state);
            pathPtr->StmtCtx = stmt;
        }
    }
}

void ProgramState::setReturnExpr(const Expr *expr)
{
    vector<unique_ptr<Path>> updatedPaths;

    for (auto &pathPtr : paths)
    {
        if (!pathPtr->isActive())
        {
            updatedPaths.emplace_back(std::move(pathPtr));
            continue;
        }

        Path::EvalResult eval                     = pathPtr->evalExpr(expr);
        vector<unique_ptr<Path>> &generatedPaths  = eval.first;
        vector<unique_ptr<SymbolicExpr>> &results = eval.second;

        size_t n = results.size();
        for (size_t i = 0; i < n; ++i)
        {
            unique_ptr<Path> newPath;
            if (i == 0)
            {
                newPath = std::move(pathPtr);
            }
            else
            {
                newPath = std::move(generatedPaths[i - 1]);
            }

            newPath->setReturnExpr(std::move(results[i]));
            updatedPaths.emplace_back(std::move(newPath));
        }
    }

    paths = std::move(updatedPaths);
}

void ProgramState::updateVarState(const BinaryOperator *binOp)
{
    vector<unique_ptr<Path>> updatedPaths;

    for (auto &path : paths)
    {
        auto lvalue = path->extractLValue(binOp->getLHS());
        if (!path->isActive())
        {
            updatedPaths.push_back(std::move(path));
            continue;
        }

        Path::EvalResult eval;

        if (binOp->isCompoundAssignmentOp())
        {
            BinaryOpExpr::Operator op = getCompoundAssignOp(binOp->getOpcode());

            Path::EvalResult lhs = path->evalExpr(binOp->getLHS());

            vector<unique_ptr<Path>> outPaths;
            vector<unique_ptr<SymbolicExpr>> outExprs;

            for (size_t i = 0; i < lhs.second.size(); ++i)
            {
                Path *lhsPath = (i == 0) ? path.get() : lhs.first[i - 1].get();

                Path::EvalResult rhs = lhsPath->evalExpr(binOp->getRHS());

                for (size_t j = 0; j < rhs.second.size(); ++j)
                {
                    outExprs.emplace_back(make_unique<BinaryOpExpr>(
                        lhs.second[i]->clone(), op, std::move(rhs.second[j])));

                    if (i != 0 || j != 0)
                        outPaths.emplace_back(std::move(rhs.first[j - 1]));
                }
            }
            eval = {std::move(outPaths), std::move(outExprs)};
        }
        else
        {
            eval = path->evalExpr(binOp->getRHS());
        }

        size_t n = eval.second.size();
        for (size_t i = 0; i < n; ++i)
        {
            unique_ptr<Path> newPath = (i == 0) ? std::move(path) : std::move(eval.first[i - 1]);
            if (holds_alternative<const VarDecl *>(lvalue))
            {
                auto *var = get<const VarDecl *>(lvalue);
                newPath->updateVarState(var, std::move(eval.second[i]));
            }
            else if (holds_alternative<unique_ptr<Address>>(lvalue))
            {
                auto &addrUptr = get<unique_ptr<Address>>(lvalue);
                Address *addr  = addrUptr.get();
                newPath->updateMemory(addr, std::move(eval.second[i]));
            }
            else
                UNREACHABLE();

            updatedPaths.push_back(std::move(newPath));
        }
    }

    paths = std::move(updatedPaths);
}

void ProgramState::addNewDecls(const vector<const VarDecl *> &varDecls)
{
    for (const VarDecl *varDecl : varDecls)
    {
        const Expr *initExpr = varDecl->getInit();
        vector<unique_ptr<Path>> updatedPaths;

        for (auto &path : paths)
        {
            if (!path->isActive())
            {
                updatedPaths.push_back(std::move(path));
                continue;
            }

            path->allocMemory(varDecl);

            if (!initExpr)
            {
                updatedPaths.push_back(std::move(path));
                continue;
            }

            Path::EvalResult eval = path->evalExpr(initExpr);

            size_t n = eval.second.size();
            for (size_t i = 0; i < n; ++i)
            {
                unique_ptr<Path> newPath =
                    (i == 0) ? std::move(path) : std::move(eval.first[i - 1]);

                newPath->updateVarState(varDecl, std::move(eval.second[i]));
                updatedPaths.push_back(std::move(newPath));
            }
        }

        paths = std::move(updatedPaths);
    }
}

pair<unique_ptr<ProgramState>, unique_ptr<ProgramState>> ProgramState::splitActiveInactive()
{
    auto activeState   = make_unique<ProgramState>(Context->clone());
    auto inactiveState = make_unique<ProgramState>(Context->clone());

    for (auto &path : paths)
    {
        if (path->isActive())
            activeState->paths.push_back(std::move(path));
        else
            inactiveState->paths.push_back(std::move(path));
    }
    paths.clear();
    return {std::move(activeState), std::move(inactiveState)};
}

unique_ptr<ProgramState> ProgramState::merge(const vector<const ProgramState *> &states)
{
    // TODO: Carefully process loop indexes.
    auto merged = make_unique<ProgramState>(Context->clone());

    for (const auto *state : states)
        for (const auto &path : state->paths)
            merged->paths.push_back(path->clone());
    return merged;
}

unique_ptr<ProgramState> ProgramState::clone() const
{
    auto newState = make_unique<ProgramState>(Context->clone());

    for (const auto &path : paths)
    {
        if (path)
            newState->paths.push_back(path->clone());
    }

    newState->StmtCtx     = StmtCtx;
    newState->loopIndexes = loopIndexes;
    return newState;
}

unique_ptr<ProgramState> ProgramState::cloneWithPaths(vector<unique_ptr<Path>> &newPaths) const
{
    auto clone   = this->clone();
    clone->paths = std::move(newPaths);
    return clone;
}

void ProgramState::resetState()
{
    for (auto &path : paths)
    {
        if (!path->isActive() && (path->StmtCtx && path->StmtCtx == this->StmtCtx))
            path->setPathState(Path::PathState::Step);
    }
}

static void collectCaseBlocks(
    const CompoundStmt *body, vector<vector<const Stmt *>> &blocks, vector<const Expr *> &conds)
{
    if (!body)
    {
        ERROR("dyn_cast failed for switch body.");
    }
    blocks.clear();
    conds.clear();

    vector<const Stmt *> currentBlock;
    const Expr *currentCond = nullptr;

    for (auto *stmt : body->body())
    {
        if (auto *cs = dyn_cast<CaseStmt>(stmt))
        {
            if (!currentBlock.empty())
            {
                blocks.emplace_back(std::move(currentBlock));
                conds.push_back(currentCond);
                currentBlock.clear();
            }
            currentCond = cs->getLHS();
            currentBlock.push_back(cs->getSubStmt());
        }
        else if (auto *ds = dyn_cast<DefaultStmt>(stmt))
        {
            if (!currentBlock.empty())
            {
                blocks.emplace_back(std::move(currentBlock));
                conds.push_back(currentCond);
                currentBlock.clear();
            }
            currentCond = nullptr;
            currentBlock.push_back(ds->getSubStmt());
        }
        else
        {
            currentBlock.push_back(stmt);
        }
    }
    if (!currentBlock.empty())
    {
        blocks.emplace_back(std::move(currentBlock));
        conds.push_back(currentCond);
    }
}

vector<pair<unique_ptr<ProgramState>, unique_ptr<SymbolicExpr>>>
ProgramState::splitStateBySwitchCond(const Expr *switchCond)
{
    if (!switchCond)
    {
        TODO();
    }
    vector<pair<unique_ptr<ProgramState>, unique_ptr<SymbolicExpr>>> result;

    for (auto &path : paths)
    {
        auto evalResult = path->evalExpr(switchCond);

        if (evalResult.first.size() != 0)
        {
            ERROR("evalExpr produced unexpected side paths");
        }

        vector<unique_ptr<Path>> onePath;
        onePath.push_back(std::move(path));
        auto stateClone = cloneWithPaths(onePath);

        result.emplace_back(std::move(stateClone), std::move(evalResult.second[0]));
    }

    return result;
}
void ProgramState::stepSimpleSwitch(const SwitchStmt *switchStmt)
{
    auto partitions = splitStateBySwitchCond(switchStmt->getCond());
    vector<vector<const Stmt *>> blocks;
    vector<const Expr *> conds;
    collectCaseBlocks(dyn_cast<CompoundStmt>(switchStmt->getBody()), blocks, conds);

    vector<unique_ptr<ProgramState>> finalStates;
    for (auto &pr : partitions)
    {
        auto current  = std::move(pr.first);
        auto symValue = std::move(pr.second);
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            auto &stmts    = blocks[i];
            auto *caseCond = conds[i];

            if (caseCond == nullptr)
            {
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
            for (auto &p : eqState->paths)
                p->insertPathCondition(condExprEq->clone());

            for (auto *s : stmts)
                eqState->step(s);

            auto condExprNe = make_unique<BinaryOpExpr>(
                symValue->clone(), BinaryOpExpr::Operator::NotEqual, std::move(caseSymExpr));
            for (auto &p : current->paths)
                p->insertPathCondition(condExprNe->clone());

            if (eqState->isInactive())
            {
                finalStates.push_back(std::move(eqState));
            }
            else
            {
                vector<const ProgramState *> mergeInputs;
                mergeInputs.push_back(eqState.get());
                mergeInputs.push_back(current.get());
                auto merged = eqState->merge(mergeInputs);
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
    paths       = std::move(merged->paths);
}

bool ProgramState::isInactive() const
{
    for (const auto &p : paths)
    {
        if (p->isActive())
        {
            return false;
        }
    }
    return true;
}

#include "SpecGenerator/stringTemplate.h"
#include "SpecGenerator/loopInvTemplates.h"

void ProgramState::CollectLoopACSL()
{
    NameMap map = {{"index", "i"}, {"max", "res"}, {"array", "p"}, {"i", "i"}, {"n", "n"}};
    INFO(FIND_MAX_LOOP.to_string(map));
}

void ProgramState::generateFuncACSL() {}

string ProgramState::dump() const
{
    ostringstream oss;
    for (const auto &p : paths)
    {
        oss << p->dump() << "\n";
    }
    return oss.str();
}