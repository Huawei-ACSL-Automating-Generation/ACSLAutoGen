#include "state.h"
#include "macros.h"
#include <clang/AST/Expr.h>
#include "clang/AST/Stmt.h"
#include "llvm/ADT/TypeSwitch.h"
#include <queue>
#include <unordered_map>
#include <memory>
#include "utils/utils.h"
#include <llvm/ADT/APSInt.h>
#include <clang/AST/StmtCXX.h>
#include "context/globalSM.h"

using namespace std;
using namespace clang;
using namespace llvm;

void Path::LoopInit(unordered_map<Variable *, unique_ptr<SymbolicExpr>> &initMap)
{
    for (auto &entry : varAddr)
    {
        const VarDecl *varDecl = entry.first;
        QualType varType = varDecl->getType();
        Address *addr = entry.second.get();
        auto memIt = memoryState.find(*addr);
        if (memIt == memoryState.end())
            ERROR("No memory state entry for allocated address");

        if (!varType->isPointerType() && !varType->isArrayType())
        {
            unique_ptr<SymbolicExpr> origExpr = std::move(memIt->second);

            Variable::VarType derived = deriveVarType(varType);
            unique_ptr<SymbolicExpr> newExpr =
                make_unique<Variable>(varDecl->getNameAsString(), derived);

            memIt->second = std::move(newExpr);

            Variable *varPtr = dynamic_cast<Variable *>(memIt->second.get());
            if (varPtr)
                initMap[varPtr] = std::move(origExpr);
        }
        else if (varType->isPointerType())
        {
            unique_ptr<SymbolicExpr> ptrOrigExpr = std::move(memIt->second);
            Address *pointeeAddr = dynamic_cast<Address *>(ptrOrigExpr.get());
            if (!pointeeAddr)
                ERROR("Pointer stored value is not an Address");

            auto memItPointee = memoryState.find(*pointeeAddr);
            if (memItPointee == memoryState.end())
                ERROR("No memory state entry for pointee");
            unique_ptr<SymbolicExpr> pointeeOrigExpr = std::move(memItPointee->second);

            QualType baseType = varType->getPointeeType();
            Variable::VarType baseDerived = deriveVarType(baseType);
            unique_ptr<SymbolicExpr> newPointeeExpr =
                make_unique<Variable>("*" + varDecl->getNameAsString(), baseDerived);
            memItPointee->second = std::move(newPointeeExpr);
            Variable *varPtr = dynamic_cast<Variable *>(memItPointee->second.get());
            if (varPtr)
                initMap[varPtr] = std::move(pointeeOrigExpr);
            // Restore the pointer's memoryState entry.
            memIt->second = std::move(ptrOrigExpr);
        }
    }
}

unique_ptr<SymbolicExpr> Path::getVarState(const VarDecl *var)
{
    auto canonicalVar = var->getCanonicalDecl();
    auto varIt = varAddr.find(canonicalVar);
    if (varIt == varAddr.end())
        ERROR("Variable '" + canonicalVar->getNameAsString() + "' has no allocated address");
    auto addr = varIt->second.get();
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
    auto newAddr = make_unique<Address>(addrCounter++);
    Address *rawPtr = newAddr.get();
    varAddr.emplace(canonicalVar, std::move(newAddr));
    return rawPtr;
}

Address *Path::allocMemory() { return new Address(addrCounter++); }

void Path::insertVarState(Address *addr, unique_ptr<SymbolicExpr> expr)
{
    if (!expr)
        expr = make_unique<NullExpr>();
    memoryState[*addr] = std::move(expr);
}

void Path::insertVarState(const VarDecl *var, unique_ptr<SymbolicExpr> expr)
{
    unique_ptr<SymbolicExpr> exprPtr;
    if (expr)
        exprPtr = std::move(expr);
    else
        exprPtr = make_unique<NullExpr>();

    auto canonicalVar = var->getCanonicalDecl();
    auto addrIt = varAddr.find(canonicalVar);
    if (addrIt == varAddr.end())
        ERROR("Variable has no allocated address");

    auto &addr = addrIt->second;
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
    auto cloned = make_unique<Path>();
    cloned->currentState = currentState;
    for (const auto &entry : varAddr)
        cloned->varAddr.emplace(entry.first,
            unique_ptr<Address>(static_cast<Address *>(entry.second->clone().release())));
    for (const auto &entry : memoryState)
        cloned->memoryState.emplace(entry.first, entry.second->clone());
    for (const auto &cond : pathConditions)
        cloned->pathConditions.push_back(cond->clone());
    cloned->returnExpr = returnExpr->clone();
    cloned->addrCounter = addrCounter;
    return cloned;
}

Path::EvalResult Path::evalExpr(const Expr *expr)
{
    if (!expr)
        ERROR("Fail to convert an empty Expr");

    expr = expr->IgnoreParenImpCasts();
    return TypeSwitch<const Expr *, EvalResult>(expr)
        .Case<IntegerLiteral>([](const IntegerLiteral *lit) -> EvalResult {
            APInt ap = lit->getValue();
            QualType litType = lit->getType();
            unique_ptr<SymbolicExpr> result;

            if (litType->isBooleanType())
                result = make_unique<LiteralExpr>(static_cast<bool>(ap.getZExtValue()));
            else if (litType->isUnsignedIntegerType())
            {
                if (ap.getBitWidth() <= 16)
                    result =
                        make_unique<LiteralExpr>(static_cast<unsigned short>(ap.getZExtValue()));
                else if (ap.getBitWidth() <= 32)
                    result =
                        make_unique<LiteralExpr>(static_cast<unsigned short>(ap.getZExtValue()));
                else
                {
                    UNIMPLEMENT("Unsupported unsigned integer literal with bit width > 32: "
                                << ap.getBitWidth());
                    vector<unique_ptr<Path>> paths;
                    vector<unique_ptr<SymbolicExpr>> exprs;
                    return EvalResult(std::move(paths), std::move(exprs));
                }
            }
            else
            {
                if (ap.getBitWidth() <= 16)
                    result =
                        make_unique<LiteralExpr>(static_cast<unsigned short>(ap.getZExtValue()));
                else if (ap.getBitWidth() <= 32)
                    result =
                        make_unique<LiteralExpr>(static_cast<unsigned short>(ap.getZExtValue()));
                else
                {
                    UNIMPLEMENT("Unsupported signed integer literal with bit width > 32: "
                                << ap.getBitWidth());
                    vector<unique_ptr<Path>> paths;
                    vector<unique_ptr<SymbolicExpr>> exprs;
                    return EvalResult(std::move(paths), std::move(exprs));
                }
            }

            vector<std::unique_ptr<Path>> paths;
            vector<std::unique_ptr<SymbolicExpr>> exprs;
            exprs.reserve(1);
            exprs.push_back(std::move(result));

            return {std::move(paths), std::move(exprs)};
        })
        .Case<BinaryOperator>([this](const BinaryOperator *binOp) -> EvalResult {
            // TODO: maybe pack the logic in BO, ArraySub into a function?
            EvalResult lhs = evalExpr(binOp->getLHS());
            BinaryOpExpr::Operator op = getBinaryOp(binOp->getOpcode());

            vector<unique_ptr<Path>> outPaths;
            vector<unique_ptr<SymbolicExpr>> outExprs;

            size_t lhsCount = lhs.second.size();
            for (size_t i = 0; i < lhsCount; ++i)
            {
                Path *path = (i == 0) ? this : lhs.first[i - 1].get();
                unique_ptr<SymbolicExpr> lhsExpr = std::move(lhs.second[i]);

                EvalResult rhs = path->evalExpr(binOp->getRHS());
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
            const VarDecl *varDecl = dyn_cast<VarDecl>(declRef->getDecl());
            if (!varDecl)
            {
                UNIMPLEMENT("Unsupported Decl type: " << declRef->getDecl()->getDeclKindName());
                vector<unique_ptr<Path>> paths;
                vector<unique_ptr<SymbolicExpr>> exprs;
                return EvalResult(std::move(paths), std::move(exprs));
            }
            auto varExpr = getVarState(varDecl);
            vector<unique_ptr<Path>> paths;
            vector<unique_ptr<SymbolicExpr>> exprs;
            exprs.push_back(std::move(varExpr));
            return {std::move(paths), std::move(exprs)};
        })
        .Case<ArraySubscriptExpr>([this](const ArraySubscriptExpr *arrSub) -> EvalResult {
            EvalResult base = evalExpr(arrSub->getBase());

            vector<unique_ptr<Path>> outPaths;
            vector<unique_ptr<SymbolicExpr>> outExprs;

            for (size_t i = 0; i < base.second.size(); ++i)
            {
                Path *path = (i == 0) ? this : base.first[i - 1].get();
                auto baseExpr = std::move(base.second[i]);

                EvalResult idx = path->evalExpr(arrSub->getIdx());

                for (size_t j = 0; j < idx.second.size(); ++j)
                {
                    auto idxExpr = std::move(idx.second[j]);

                    outExprs.emplace_back(
                        make_unique<ArrayExpr>(baseExpr->clone(), std::move(idxExpr)));

                    if (i == 0 && j == 0)
                        continue;

                    outPaths.emplace_back(
                        j == 0 ? std::move(base.first[i - 1]) : std::move(idx.first[j - 1]));
                }
            }
            return {std::move(outPaths), std::move(outExprs)};
        })
        .Case<CallExpr>([](const CallExpr *) -> EvalResult {
            TODO();
            vector<unique_ptr<Path>> paths;
            vector<unique_ptr<SymbolicExpr>> exprs;
            return EvalResult(std::move(paths), std::move(exprs));
        })
        .Default([](const Expr *e) -> EvalResult {
            UNIMPLEMENT("Unsupported Expr type: " << e->getStmtClassName());
            vector<unique_ptr<Path>> paths;
            vector<unique_ptr<SymbolicExpr>> exprs;
            return EvalResult(std::move(paths), std::move(exprs));
        });
}
string Path::dump() const
{
    std::ostringstream oss;

    oss << "Path State: " << [&]() {
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
        const clang::VarDecl *vd = pair.first;
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
            llvm::StringRef sourceText;
            std::tie(sourceText, std::ignore, std::ignore, std::ignore) = *opt;
            if (!sourceText.empty())
            {
                oss << "Stmt Context: " << sourceText.str() << "\n";
            }
        }
    }
    return oss.str();
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

            Variable::VarType varType = deriveVarType(paramType);
            unique_ptr<SymbolicExpr> varExpr =
                make_unique<Variable>(param->getNameAsString(), varType);

            paths[0]->insertVarState(addr, std::move(varExpr));
        }
        else if (paramType->isPointerType())
        {
            QualType baseType = paramType->getPointeeType();
            if (baseType->isPointerType() || baseType->isArrayType())
                UNIMPLEMENT("Unsupported pointer to pointer/array");

            Address *ptrAddr = paths[0]->allocMemory(param);
            Address *pointeeAddr = paths[0]->allocMemory();

            unique_ptr<SymbolicExpr> pointerValue = pointeeAddr->clone();
            paths[0]->insertVarState(ptrAddr, std::move(pointerValue));

            Variable::VarType varType = deriveVarType(baseType);
            unique_ptr<SymbolicExpr> pointeeVarExpr =
                make_unique<Variable>("*" + param->getNameAsString(), varType);
            paths[0]->insertVarState(pointeeAddr, std::move(pointeeVarExpr));
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
            this->StmtCtx = switchStmt;

            if (switchStmt->hasInitStorage())
                UNIMPLEMENT(
                    "Unsupported Switch type, Cond has init statement: " << switchStmt->getCond());
            if (auto bodyStmt = dyn_cast_if_present<CompoundStmt>(switchStmt->getBody()))
            {
                if (isa_and_present<CaseStmt>(bodyStmt->body_front()))
                {
                    // simple SwitchStmt
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

            ResetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<ForStmt>([this](const ForStmt *forStmt) {
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx = forStmt;
            stepLoop(forStmt);
            ResetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<WhileStmt>([this](const WhileStmt *whileStmt) {
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx = whileStmt;
            stepLoop(whileStmt);
            ResetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<DoStmt>([this](const DoStmt *doStmt) {
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx = doStmt;
            stepLoop(doStmt);
            ResetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<CXXForRangeStmt>([this](const CXXForRangeStmt *rangeStmt) {
            auto prevStmtCtx = this->StmtCtx;
            this->StmtCtx = rangeStmt;
            stepLoop(rangeStmt);
            ResetState();
            this->StmtCtx = prevStmtCtx;
        })
        .Case<BreakStmt>([](const BreakStmt *) { TODO(); })
        .Case<ContinueStmt>([](const ContinueStmt *) { TODO(); })
        .Default(
            [](const Stmt *s) { UNIMPLEMENT("Unsupported Stmt type: " << s->getStmtClassName()); });
    return;
}

void ProgramState::stepBranch(
    const vector<const Expr *> &branchConds, const vector<const Stmt *> &branchStmts)
{
    assert(branchStmts.size() == branchConds.size() + 1);

    vector<unique_ptr<ProgramState>> clones;
    vector<const ProgramState *> statesForMerge;

    auto splitPair = splitActiveInactive();
    size_t n = branchConds.size();
    for (size_t i = 0; i < n; ++i)
    {
        auto newState = splitPair.first->clone();

        vector<std::unique_ptr<Path>> updatedPaths;

        for (auto &path : newState->paths)
        {
            Path::EvalResult eval = path->evalExpr(branchConds[i]);

            size_t m = eval.second.size();
            for (size_t j = 0; j < m; ++j)
            {
                std::unique_ptr<Path> newPath =
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
    auto mergedActive = Merge(statesForMerge);
    for (auto &path : splitPair.second->paths)
        mergedActive->paths.push_back(std::move(path));

    paths = std::move(mergedActive->paths);
}

void ProgramState::stepLoop(const Stmt *loopStmt)

{
    const Stmt *init = nullptr;
    const Expr *cond = nullptr;
    const Stmt *body = nullptr;
    const Stmt *inc = nullptr;

    if (const auto *forStmt = dyn_cast<ForStmt>(loopStmt))
    {
        init = forStmt->getInit();
        cond = forStmt->getCond();
        body = forStmt->getBody();
        inc = forStmt->getInc();
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

        unordered_map<Variable *, unique_ptr<SymbolicExpr>> loopInitMap;
        newState->paths.back()->LoopInit(loopInitMap);

        newState->step(body);
        newState->step(inc);

        newState->CollectLoopACSL();
    }
    paths.clear();
    // TODO: process loop post state.
    // for (auto &state : newStates)
    // {
    //     for (auto &p : state->paths)
    //     {
    //         paths.push_back(std::move(p));
    //     }
    // }
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

        Path::EvalResult eval = pathPtr->evalExpr(expr);
        vector<unique_ptr<Path>> &generatedPaths = eval.first;
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
    const VarDecl *var = [&]() -> const VarDecl * {
        if (auto *declRef = dyn_cast<DeclRefExpr>(binOp->getLHS()))
            return dyn_cast<VarDecl>(declRef->getDecl());
        return nullptr;
    }();

    if (!var || var->getType()->isPointerType() || var->getType()->isArrayType())
        TODO();

    vector<unique_ptr<Path>> updatedPaths;

    for (auto &path : paths)
    {
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
                auto lhsExpr = std::move(lhs.second[i]);

                Path::EvalResult rhs = lhsPath->evalExpr(binOp->getRHS());

                for (size_t j = 0; j < rhs.second.size(); ++j)
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

            newPath->insertVarState(var, std::move(eval.second[i]));
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

                newPath->insertVarState(varDecl, std::move(eval.second[i]));
                updatedPaths.push_back(std::move(newPath));
            }
        }

        paths = std::move(updatedPaths);
    }
}

pair<unique_ptr<ProgramState>, unique_ptr<ProgramState>> ProgramState::splitActiveInactive()
{
    auto activeState = make_unique<ProgramState>(Context->clone());
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

unique_ptr<ProgramState> ProgramState::Merge(const vector<const ProgramState *> &states)
{
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
        newState->paths.push_back(path->clone());
    }

    newState->loopIndexes = loopIndexes;
    return newState;
}

void ProgramState::ResetState()
{
    for (auto &path : paths)
    {
        if (!path->isActive() && (path->StmtCtx && path->StmtCtx == this->StmtCtx))
            path->setPathState(Path::PathState::Step);
    }
}

void ProgramState::stepSimpleSwitch(const SwitchStmt *)
{
    TODO();
    // auto switchCond = switchStmt->getCond();
    // unique_ptr<ProgramState> activePS,
    //     inactivePS; // PS = ProgramState
    // tie(activePS, inactivePS) = splitActiveInactive();
    // vector<ProgramState *> statesForMerge; // Every case we meet will emit a new
    //                                        // ProgramState to statesForMerge.
    // vector<Expr *> caseConds; // Collect case conds to construct the default branch's cond.

    // // Caller ensures the *Simple* switch has non-empty body typed CompoundStmt.
    // for (auto stmt : dyn_cast<CompoundStmt>(switchStmt->getBody())->body())
    // {
    //     if (auto caseStmt = dyn_cast<CaseStmt>(stmt))
    //     {
    //         // Split a new state step into the case.
    //         auto newState = activePS->clone();
    //         for (auto &path : newState->paths)
    //         {
    //             auto caseCond = caseStmt->getLHS();
    //             caseConds.push_back(caseCond);
    //             path->insertPathCondition(switchCond, BinaryOpExpr::Operator::Equal, caseCond);
    //         }
    //         statesForMerge.push_back(newState.release());
    //     }
    //     else if (isa<DefaultStmt>(stmt))
    //     {
    //         // Use activePS as the state into default branch.
    //         for (auto &path : activePS->paths)
    //         {
    //             for (auto &cond : caseConds)
    //             {
    //                 path->insertPathCondition(switchCond, BinaryOpExpr::Operator::NotEqual, cond);
    //             }
    //         }
    //         statesForMerge.push_back(activePS.release()); // activePS released
    //     }
    //     // Every state existed steps through the whole switch's body together.
    //     for (auto state : statesForMerge)
    //     {
    //         state->step(stmt);
    //     }
    // }
    // if (activePS)
    // {
    //     // Switch has no default:, so activePS has not been released.
    //     statesForMerge.push_back(activePS.release()); // activePS released
    // }
    // // Merge all paths in different ProgramStates into one.
    // auto mergedActive =
    //     Merge(vector<const ProgramState *>(make_move_iterator(statesForMerge.begin()),
    //         make_move_iterator(statesForMerge.end()))); // statesForMerge moved

    // // Merge inactive paths in.
    // mergedActive->paths.insert(mergedActive->paths.end(),
    //     make_move_iterator(inactivePS->paths.begin()),
    //     make_move_iterator(inactivePS->paths.end())); // inactivePS->paths moved

    // paths = std::move(mergedActive->paths); // mergedActive->paths moved
}

#include "specGenerator/stringTemplate.h"
#include "specGenerator/loopInvTemplates.h"

void ProgramState::CollectLoopACSL()
{
    NameMap map = {{"index", "i"}, {"max", "res"}, {"array", "p"}, {"i", "i"}, {"n", "n"}};
    INFO(FIND_MAX_LOOP(map));
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