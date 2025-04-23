#include "utils.h"
#include "macros.h"

#include "clang/AST/PrettyPrinter.h"
#include <clang/AST/Expr.h>
#include <llvm/ADT/TypeSwitch.h>
#include <string>

using namespace clang;
using namespace llvm;
using namespace std;
// bool isLoopOrSwitchStmt(const Stmt *stmt)
// {
//     return isa<ForStmt>(stmt) || isa<WhileStmt>(stmt) || isa<DoStmt>(stmt) ||
//            isa<CXXForRangeStmt>(stmt) || isa<SwitchStmt>(stmt);
// }

unique_ptr<SymbolicExpr> createLNotExpr(unique_ptr<SymbolicExpr> expr)
{
    return make_unique<UnaryOpExpr>(UnaryOpExpr::Operator::LogicalNot, std::move(expr));
}

BinaryOpExpr::Operator getCompoundAssignOp(clang::BinaryOperatorKind compoundAssignOp)
{
    switch (compoundAssignOp)
    {
    case clang::BO_MulAssign: return BinaryOpExpr::Operator::Multiply;
    case clang::BO_DivAssign: return BinaryOpExpr::Operator::Divide;
    case clang::BO_RemAssign: return BinaryOpExpr::Operator::Remainder;
    case clang::BO_AddAssign: return BinaryOpExpr::Operator::Add;
    case clang::BO_SubAssign: return BinaryOpExpr::Operator::Subtract;
    case clang::BO_ShlAssign: return BinaryOpExpr::Operator::ShiftLeft;
    case clang::BO_ShrAssign: return BinaryOpExpr::Operator::ShiftRight;
    case clang::BO_AndAssign: return BinaryOpExpr::Operator::BitAnd;
    case clang::BO_XorAssign: return BinaryOpExpr::Operator::BitXor;
    case clang::BO_OrAssign: return BinaryOpExpr::Operator::BitOr;
    default: UNREACHABLE();
    }
}

// No AssignOp Here.
BinaryOpExpr::Operator getBinaryOp(BinaryOperatorKind op)
{
    switch (op)
    {
    case BO_Mul: return BinaryOpExpr::Operator::Multiply;
    case BO_Div: return BinaryOpExpr::Operator::Divide;
    case BO_Rem: return BinaryOpExpr::Operator::Remainder;
    case BO_Add: return BinaryOpExpr::Operator::Add;
    case BO_Sub: return BinaryOpExpr::Operator::Subtract;
    case BO_Shl: return BinaryOpExpr::Operator::ShiftLeft;
    case BO_Shr: return BinaryOpExpr::Operator::ShiftRight;
    case BO_LT: return BinaryOpExpr::Operator::LessThan;
    case BO_GT: return BinaryOpExpr::Operator::GreaterThan;
    case BO_LE: return BinaryOpExpr::Operator::LessEqual;
    case BO_GE: return BinaryOpExpr::Operator::GreaterEqual;
    case BO_EQ: return BinaryOpExpr::Operator::Equal;
    case BO_NE: return BinaryOpExpr::Operator::NotEqual;
    case BO_And: return BinaryOpExpr::Operator::BitAnd;
    case BO_Xor: return BinaryOpExpr::Operator::BitXor;
    case BO_Or: return BinaryOpExpr::Operator::BitOr;
    case BO_LAnd: return BinaryOpExpr::Operator::LogicalAnd;
    case BO_LOr: return BinaryOpExpr::Operator::LogicalOr;
    case BO_Assign:
    case BO_AddAssign:
    case BO_SubAssign:
    case BO_MulAssign:
    case BO_DivAssign:
    case BO_RemAssign:
    case BO_ShlAssign:
    case BO_ShrAssign:
    case BO_AndAssign:
    case BO_XorAssign:
    case BO_OrAssign: UNREACHABLE();
    default: UNIMPLEMENT("Unsupported binary operator: " << op); return BinaryOpExpr::Operator::Add;
    }
}

bool isAssignOp(const BinaryOperator *binOp)
{
    if (!binOp)
        return false;
    switch (binOp->getOpcode())
    {
    case BO_Assign:
    case BO_MulAssign:
    case BO_DivAssign:
    case BO_RemAssign:
    case BO_AddAssign:
    case BO_SubAssign:
    case BO_ShlAssign:
    case BO_ShrAssign:
    case BO_AndAssign:
    case BO_OrAssign:
    case BO_XorAssign: return true;
    default: return false;
    }
}

SymbolicExpr::Type deriveVarType(QualType type)
{
    return llvm::TypeSwitch<QualType, SymbolicExpr::Type>(type.getCanonicalType())
        .Case([](const BuiltinType *BT) -> SymbolicExpr::Type {
            using Kind = SymbolicExpr::ScalarKind;

            switch (BT->getKind())
            {
            case BuiltinType::Bool: return {Kind::Bool, 1};
            case BuiltinType::Char_S:
            case BuiltinType::SChar: return {Kind::Int, 8};
            case BuiltinType::Char_U:
            case BuiltinType::UChar: return {Kind::UInt, 8};

            case BuiltinType::Short: return {Kind::Int, 16};
            case BuiltinType::UShort: return {Kind::UInt, 16};

            case BuiltinType::Int: return {Kind::Int, 32};
            case BuiltinType::UInt: return {Kind::UInt, 32};

            case BuiltinType::Long: return {Kind::Int, 64};
            case BuiltinType::ULong: return {Kind::UInt, 64};

            case BuiltinType::LongLong: return {Kind::Int, 64};
            case BuiltinType::ULongLong: return {Kind::UInt, 64};

            default:
                LangOptions langOpts;
                PrintingPolicy pp(langOpts);
                UNIMPLEMENT("Unsupported builtin type: " << BT->getName(pp).str());
            }
        })
        .Default([&](QualType QT) -> SymbolicExpr::Type {
            UNIMPLEMENT("Unsupported non-builtin type: " << QT.getAsString());
        });
}

bool ignoreTopBinop(const BinaryOperator *binOp)
{
    if (!binOp)
        return false;
    string opName;
    switch (binOp->getOpcode())
    {
    case BO_Mul: opName = "Multiply"; break;
    case BO_Div: opName = "Divide"; break;
    case BO_Rem: opName = "Remainder"; break;
    case BO_Add: opName = "Add"; break;
    case BO_Sub: opName = "Subtract"; break;
    case BO_Shl: opName = "ShiftLeft"; break;
    case BO_Shr: opName = "ShiftRight"; break;
    case BO_LT: opName = "LessThan"; break;
    case BO_GT: opName = "GreaterThan"; break;
    case BO_LE: opName = "LessEqual"; break;
    case BO_GE: opName = "GreaterEqual"; break;
    case BO_EQ: opName = "Equal"; break;
    case BO_NE: opName = "NotEqual"; break;
    case BO_And: opName = "BitAnd"; break;
    case BO_Xor: opName = "BitXor"; break;
    case BO_Or: opName = "BitOr"; break;
    case BO_LAnd: opName = "LogicalAnd"; break;
    case BO_LOr: opName = "LogicalOr"; break;
    default: return false;
    }
    INFO("ignore op: " + opName);
    return true;
}