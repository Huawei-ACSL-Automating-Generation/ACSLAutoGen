#include "state.h"
#include "macros.h"
#include <clang/AST/Expr.h>
#include "clang/AST/Stmt.h"
#include "llvm/ADT/TypeSwitch.h"
#include <memory>
#include "utils/utils.h"
#include <llvm/ADT/APSInt.h>
#include <clang/AST/StmtCXX.h>

using namespace std;
using namespace clang;
using namespace llvm;
SymbolicExpr *Path::getVarState(const VarDecl *var)
{
    auto canonicalVar = var->getCanonicalDecl();
    auto it = stateMap.find(canonicalVar);
    if (it == stateMap.end())
        ERROR("Variable state not found");

    return it->second.get();
}

const vector<unique_ptr<SymbolicExpr>> &Path::getPathConditions() const { return pathConditions; }

void Path::insertVarState(const VarDecl *var, const Expr *expr)
{
    auto canonicalVar = var->getCanonicalDecl();
    unique_ptr<SymbolicExpr> convertedExpr;
    if (expr)
        convertedExpr = convertExpr(expr);
    else
        convertedExpr = make_unique<NullExpr>();
    auto it = stateMap.find(canonicalVar);
    if (it != stateMap.end())
        it->second = std::move(convertedExpr);
    else
        stateMap.insert({canonicalVar, std::move(convertedExpr)});
}

void Path::insertPathCondition(const Expr *cond)
{
    if (!cond)
        return;
    auto convertedCond = convertExpr(cond);
    pathConditions.push_back(std::move(convertedCond));
}

void Path::insertDefaultPathConds(const vector<const Expr *> &conds)
{
    for (auto cond : conds)
    {
        if (!cond)
            continue;
        auto converted = convertExpr(cond);
        auto notExpr = createLNotExpr(std::move(converted));
        pathConditions.push_back(std::move(notExpr));
    }
}

std::unique_ptr<Path> Path::clone() const
{
    auto cloned = std::make_unique<Path>();
    cloned->currentState = currentState;
    for (const auto &entry : stateMap)
        cloned->stateMap.insert({entry.first, entry.second->clone()});
    for (const auto &cond : pathConditions)
        cloned->pathConditions.push_back(cond->clone());
    cloned->returnExpr = returnExpr->clone();
    return cloned;
}

unique_ptr<SymbolicExpr> Path::convertExpr(const Expr *expr)
{
    if (!expr)
        ERROR("Fail to convert an empty Expr");

    expr = expr->IgnoreParenImpCasts();
    return TypeSwitch<const Expr *, unique_ptr<SymbolicExpr>>(expr)
        .Case<IntegerLiteral>([this](const IntegerLiteral *lit) -> unique_ptr<SymbolicExpr> {
            APInt ap = lit->getValue();
            QualType litType = lit->getType();
            if (litType->isBooleanType())
                return make_unique<LiteralExpr>(static_cast<bool>(ap.getZExtValue()));
            else if (litType->isUnsignedIntegerType())
            {
                if (ap.getBitWidth() <= 16)
                    return make_unique<LiteralExpr>(static_cast<unsigned short>(ap.getZExtValue()));
                else if (ap.getBitWidth() <= 32)
                    return make_unique<LiteralExpr>(static_cast<unsigned int>(ap.getZExtValue()));
                else
                {
                    UNIMPLEMENT("Unsupported unsigned integer literal with bit width > 32: "
                                << ap.getBitWidth());
                    return nullptr;
                }
            }
            else
            {
                if (ap.getBitWidth() <= 16)
                    return make_unique<LiteralExpr>(static_cast<short>(ap.getSExtValue()));
                else if (ap.getBitWidth() <= 32)
                    return make_unique<LiteralExpr>(static_cast<int>(ap.getSExtValue()));
                else
                {
                    UNIMPLEMENT("Unsupported signed integer literal with bit width > 32: "
                                << ap.getBitWidth());
                    return nullptr;
                }
            }
        })
        .Case<BinaryOperator>([this](const BinaryOperator *binOp) -> unique_ptr<SymbolicExpr> {
            unique_ptr<SymbolicExpr> lhs = convertExpr(binOp->getLHS());
            unique_ptr<SymbolicExpr> rhs = convertExpr(binOp->getRHS());
            BinaryOpExpr::Operator op;
            switch (binOp->getOpcode())
            {
            case BO_Mul: op = BinaryOpExpr::Operator::Multiply; break;
            case BO_Div: op = BinaryOpExpr::Operator::Divide; break;
            case BO_Rem: op = BinaryOpExpr::Operator::Remainder; break;
            case BO_Add: op = BinaryOpExpr::Operator::Add; break;
            case BO_Sub: op = BinaryOpExpr::Operator::Subtract; break;
            case BO_Shl: op = BinaryOpExpr::Operator::ShiftLeft; break;
            case BO_Shr: op = BinaryOpExpr::Operator::ShiftRight; break;
            case BO_LT: op = BinaryOpExpr::Operator::LessThan; break;
            case BO_GT: op = BinaryOpExpr::Operator::GreaterThan; break;
            case BO_LE: op = BinaryOpExpr::Operator::LessEqual; break;
            case BO_GE: op = BinaryOpExpr::Operator::GreaterEqual; break;
            case BO_EQ: op = BinaryOpExpr::Operator::Equal; break;
            case BO_NE: op = BinaryOpExpr::Operator::NotEqual; break;
            case BO_And: op = BinaryOpExpr::Operator::BitAnd; break;
            case BO_Xor: op = BinaryOpExpr::Operator::BitXor; break;
            case BO_Or: op = BinaryOpExpr::Operator::BitOr; break;
            case BO_LAnd: op = BinaryOpExpr::Operator::LogicalAnd; break;
            case BO_LOr: op = BinaryOpExpr::Operator::LogicalOr; break;
            case BO_Assign: op = BinaryOpExpr::Operator::Assign; break;
            case BO_MulAssign: op = BinaryOpExpr::Operator::MultiplyAssign; break;
            case BO_DivAssign: op = BinaryOpExpr::Operator::DivideAssign; break;
            case BO_RemAssign: op = BinaryOpExpr::Operator::RemainderAssign; break;
            case BO_AddAssign: op = BinaryOpExpr::Operator::AddAssign; break;
            case BO_SubAssign: op = BinaryOpExpr::Operator::SubtractAssign; break;
            case BO_ShlAssign: op = BinaryOpExpr::Operator::ShiftLeftAssign; break;
            case BO_ShrAssign: op = BinaryOpExpr::Operator::ShiftRightAssign; break;
            case BO_AndAssign: op = BinaryOpExpr::Operator::AndAssign; break;
            case BO_XorAssign: op = BinaryOpExpr::Operator::XorAssign; break;
            case BO_OrAssign: op = BinaryOpExpr::Operator::OrAssign; break;
            default:
                UNIMPLEMENT("Unsupported binary operator: " << binOp->getOpcode());
                return nullptr;
            }
            return make_unique<BinaryOpExpr>(std::move(lhs), op, std::move(rhs));
        })
        .Case<UnaryOperator>([this](const UnaryOperator *unOp) -> unique_ptr<SymbolicExpr> {
            unique_ptr<SymbolicExpr> subExpr = convertExpr(unOp->getSubExpr());
            UnaryOpExpr::Operator op;
            switch (unOp->getOpcode())
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
                UNIMPLEMENT("Unsupported unary operator: " << unOp->getOpcode());
                return nullptr;
            }
            return make_unique<UnaryOpExpr>(op, std::move(subExpr));
        })
        .Case<ParenExpr>([this](const ParenExpr *paren) -> unique_ptr<SymbolicExpr> {
            return convertExpr(paren->getSubExpr());
        })
        .Case<DeclRefExpr>([this](const DeclRefExpr *declRef) -> unique_ptr<SymbolicExpr> {
            const VarDecl *varDecl = dyn_cast<VarDecl>(declRef->getDecl());
            if (!varDecl)
            {
                UNIMPLEMENT("Unsupported Decl type: " << declRef->getDecl()->getDeclKindName());
                return nullptr;
            }
            SymbolicExpr *state = getVarState(varDecl);
            return unique_ptr<SymbolicExpr>(state);
        })
        .Case<ArraySubscriptExpr>(
            [this](const ArraySubscriptExpr *arrSub) -> unique_ptr<SymbolicExpr> {
                unique_ptr<SymbolicExpr> arrayExpr = convertExpr(arrSub->getBase());
                unique_ptr<SymbolicExpr> indexExpr = convertExpr(arrSub->getIdx());
                return make_unique<ArrayExpr>(std::move(arrayExpr), std::move(indexExpr));
            })
        .Case<CallExpr>([this](const CallExpr *callExpr) -> unique_ptr<SymbolicExpr> {
            TODO();
            return nullptr;
        })
        .Default([this](const Expr *e) -> unique_ptr<SymbolicExpr> {
            UNIMPLEMENT("Unsupported Expr type: " << e->getStmtClassName());
            return nullptr;
        });
}

ProgramState::ProgramState() { paths.push_back(make_unique<Path>()); }

void ProgramState::init(const FunctionDecl *FD) {}

void ProgramState::step(const Stmt *stmt)
{
    if (!stmt)
        return;
    TODO(); // Stack State for break, continue context.
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
            setStates(Path::PathState::Return);
            setReturnExpr(retStmt->getRetValue());
        })
        .Case<DeclStmt>([this](const DeclStmt *declStmt) {
            vector<const VarDecl *> varDecls;
            for (auto it = declStmt->decl_begin(); it != declStmt->decl_end(); ++it)
            {
                Decl *decl = *it;
                if (!dyn_cast<VarDecl>(decl))
                {
                    WARNING(string("Unhandled Decl type: ") + decl->getDeclKindName());
                    continue;
                }
                varDecls.push_back(dyn_cast<VarDecl>(decl));
            }
            addNewDecls(varDecls);
        })
        .Case<BinaryOperator>([this](const BinaryOperator *binOp) {
            // Do nothing
        })
        .Case<ImplicitCastExpr>([this](const ImplicitCastExpr *ice) -> unique_ptr<SymbolicExpr> {
            UNIMPLEMENT("Unexpected top-level ImplicitCastExpr: " << ice->getStmtClassName());
        })
        .Case<SwitchStmt>([this](const SwitchStmt *switchStmt) { TODO(); })
        .Case<CaseStmt>([this](const CaseStmt *caseStmt) { step(caseStmt->getSubStmt()); })
        .Case<DefaultStmt>(
            [this](const DefaultStmt *defaultStmt) { step(defaultStmt->getSubStmt()); })
        .Case<ForStmt>([this](const ForStmt *forStmt) { TODO(); })
        .Case<WhileStmt>([this](const WhileStmt *whileStmt) { TODO(); })
        .Case<DoStmt>([this](const DoStmt *doStmt) { TODO(); })
        .Case<CXXForRangeStmt>([this](const CXXForRangeStmt *rangeStmt) { TODO(); })
        .Case<BreakStmt>([this](const BreakStmt *breakStmt) { TODO(); })
        .Case<ContinueStmt>([this](const ContinueStmt *continueStmt) { TODO(); })
        .Default([this](const Stmt *s) {
            UNIMPLEMENT("Unsupported Stmt type: " << s->getStmtClassName());
        });
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
        for (auto &path : newState->paths)
            path->insertPathCondition(branchConds[i]);
        newState->step(branchStmts[i]);

        statesForMerge.push_back(newState.get());
        clones.push_back(std::move(newState));
    }

    for (auto &path : splitPair.first->paths)
        path->insertDefaultPathConds(branchConds);
    splitPair.first->step(branchStmts.back());

    statesForMerge.push_back(splitPair.first.get());
    auto mergedActive = Merge(statesForMerge);
    for (auto &path : splitPair.second->paths)
        mergedActive->paths.push_back(std::move(path));

    paths = std::move(mergedActive->paths);
}

void ProgramState::setStates(Path::PathState state)
{
    for (auto &pathPtr : paths)
    {
        if (pathPtr->isActive())
            pathPtr->setPathState(state);
    }
}

void ProgramState::setReturnExpr(const Expr *expr)
{
    for (auto &pathPtr : paths)
    {
        if (pathPtr->isActive())
            pathPtr->setReturnExpr(expr);
    }
}

void ProgramState::addNewDecls(const vector<const VarDecl *> &varDecls)
{
    for (auto *varDecl : varDecls)
    {
        auto *initExpr = varDecl->getInit();
        for (auto &path : paths)
            path->insertVarState(varDecl, initExpr);
    }
}

pair<unique_ptr<ProgramState>, unique_ptr<ProgramState>> ProgramState::splitActiveInactive() const
{
    auto activeState = make_unique<ProgramState>();
    auto inactiveState = make_unique<ProgramState>();
    for (const auto &path : paths)
    {
        if (path->isActive())
            activeState->paths.push_back(path->clone());
        else
            inactiveState->paths.push_back(path->clone());
    }
    return {std::move(activeState), std::move(inactiveState)};
}

unique_ptr<ProgramState> ProgramState::Merge(const vector<const ProgramState *> &states)
{
    auto merged = make_unique<ProgramState>();
    for (const auto *state : states)
        for (const auto &path : state->paths)
            merged->paths.push_back(path->clone());
    return merged;
}

unique_ptr<ProgramState> ProgramState::clone() const
{
    auto newState = make_unique<ProgramState>();
    newState->paths.clear();
    for (const auto &path : paths)
    {
        newState->paths.push_back(path->clone());
    }
    return newState;
}