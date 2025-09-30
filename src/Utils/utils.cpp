#include "utils.h"
#include "macros.h"

#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Expr.h>
#include <llvm/ADT/TypeSwitch.h>
#include <string>

using namespace clang;
using namespace llvm;
using namespace std;

bool isAssignOp(const BinaryOperator *binOp) {
    if (!binOp)
        return false;
    switch (binOp->getOpcode()) {
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

bool ignoreTopBinop(const BinaryOperator *binOp) {
    if (!binOp)
        return false;
    string opName;
    switch (binOp->getOpcode()) {
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

namespace {
    void collectFromDeclStmt(const DeclStmt *ds, std::unordered_set<const VarDecl *> &out) {
        if (!ds)
            return;
        for (Decl *d : ds->decls()) {
            if (auto *vd = llvm::dyn_cast<VarDecl>(d)) {
                out.insert(vd);
            }
        }
    }

    void collectFromCompound(const CompoundStmt *cs, std::unordered_set<const VarDecl *> &out) {
        if (!cs)
            return;
        for (Stmt *child : cs->body()) {
            if (auto *ds = llvm::dyn_cast<DeclStmt>(child)) {
                collectFromDeclStmt(ds, out);
            }
        }
    }

    void collectFromFor(const ForStmt *fs, std::unordered_set<const VarDecl *> &out) {
        if (!fs)
            return;
        if (auto *ds = llvm::dyn_cast_or_null<DeclStmt>(fs->getInit())) {
            collectFromDeclStmt(ds, out);
        }
    }

    void collectFromCall(const CallExpr *call, std::unordered_set<const VarDecl *> &out) {
        if (!call)
            return;

        if (const FunctionDecl *FD = call->getDirectCallee()) {
            for (const ParmVarDecl *P : FD->parameters()) {
                out.insert(P);
            }
            return;
        }
        if (const Expr *Callee = call->getCallee()->IgnoreParenImpCasts()) {
            if (const auto *DRE = llvm::dyn_cast<DeclRefExpr>(Callee)) {
                if (const auto *FD2 = llvm::dyn_cast<FunctionDecl>(DRE->getDecl())) {
                    for (const ParmVarDecl *P : FD2->parameters()) {
                        out.insert(P);
                    }
                }
            }
        }
    }
} // namespace

std::unordered_set<const VarDecl *> collectLocalVars(const Stmt *stmt) {
    std::unordered_set<const VarDecl *> vars;
    if (!stmt)
        return vars;

    if (auto *cs = llvm::dyn_cast<CompoundStmt>(stmt)) {
        collectFromCompound(cs, vars);
        return vars;
    }

    if (auto *fs = llvm::dyn_cast<ForStmt>(stmt)) {
        collectFromFor(fs, vars);
        return vars;
    }

    if (auto *call = llvm::dyn_cast<CallExpr>(stmt)) {
        collectFromCall(call, vars);
        return vars;
    }

    return vars;
}
