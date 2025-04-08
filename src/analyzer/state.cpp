#include "state.h"
#include "macros.h"
#include <clang/AST/Expr.h>
#include "llvm/ADT/TypeSwitch.h"
#include <memory>
#include <llvm/ADT/APSInt.h>

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

void Path::insertVarState(const VarDecl *var, unique_ptr<SymbolicExpr> expr)
{
    auto canonicalVar = var->getCanonicalDecl();
    auto it = stateMap.find(canonicalVar);
    if (it != stateMap.end())
        it->second = std::move(expr);
    else
        stateMap.insert({canonicalVar, std::move(expr)});
}

void Path::insertPathCondition(unique_ptr<SymbolicExpr> cond)
{
    pathConditions.push_back(std::move(cond));
}

ProgramState::ProgramState() { paths.push_back(std::make_unique<Path>()); }

unique_ptr<SymbolicExpr> Path::convertExpr(const Expr *expr)
{
    if (!expr)
        ERROR("Fail to convert an empty Expr");

    expr = expr->IgnoreParenImpCasts();
    return llvm::TypeSwitch<const Expr *, unique_ptr<SymbolicExpr>>(expr)
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
