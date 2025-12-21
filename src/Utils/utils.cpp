#include "utils.h"
#include "macros.h"

#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Expr.h>
#include <llvm/ADT/TypeSwitch.h>
#include <string>

namespace acslg::utils {
    bool isAssignOp(const clang::BinaryOperator *binOp) {
        if (!binOp)
            return false;
        switch (binOp->getOpcode()) {
            using enum clang::BinaryOperatorKind;
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

    bool ignoreTopBinop(const clang::BinaryOperator *binOp) {
        if (!binOp)
            return false;
        std::string opName;
        switch (binOp->getOpcode()) {
            using enum clang::BinaryOperatorKind;
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
        void collectFromDeclStmt(const clang::DeclStmt *ds,
                                 std::unordered_set<const clang::VarDecl *> &out) {
            if (!ds)
                return;
            for (clang::Decl *d : ds->decls()) {
                if (auto *vd = dyn_cast<clang::VarDecl>(d)) {
                    out.insert(vd->getCanonicalDecl());
                }
            }
        }

        void collectFromCompound(const clang::CompoundStmt *cs,
                                 std::unordered_set<const clang::VarDecl *> &out) {
            if (!cs)
                return;
            for (clang::Stmt *child : cs->body()) {
                if (auto *ds = dyn_cast<clang::DeclStmt>(child)) {
                    collectFromDeclStmt(ds, out);
                }
            }
        }

        void collectFromFor(const clang::ForStmt *fs,
                            std::unordered_set<const clang::VarDecl *> &out) {
            if (!fs)
                return;
            if (auto *ds = dyn_cast_or_null<clang::DeclStmt>(fs->getInit())) {
                collectFromDeclStmt(ds, out);
            }
        }

        void collectFromCall(const clang::CallExpr *call,
                             std::unordered_set<const clang::VarDecl *> &out) {
            if (!call)
                return;

            if (const clang::FunctionDecl *FD = call->getDirectCallee()) {
                for (const clang::ParmVarDecl *P : FD->parameters()) {
                    out.insert(P->getCanonicalDecl());
                }
                return;
            }
            if (const clang::Expr *Callee = call->getCallee()->IgnoreParenImpCasts()) {
                if (const auto *DRE = dyn_cast<clang::DeclRefExpr>(Callee)) {
                    if (const auto *FD2 = dyn_cast<clang::FunctionDecl>(DRE->getDecl())) {
                        for (const clang::ParmVarDecl *P : FD2->parameters()) {
                            out.insert(P->getCanonicalDecl());
                        }
                    }
                }
            }
        }
    } // namespace

    std::unordered_set<const clang::VarDecl *> collectLocalVars(const clang::Stmt *stmt) {
        std::unordered_set<const clang::VarDecl *> vars;
        if (!stmt)
            return vars;

        if (auto *cs = dyn_cast<clang::CompoundStmt>(stmt)) {
            collectFromCompound(cs, vars);
            return vars;
        }

        if (auto *fs = dyn_cast<clang::ForStmt>(stmt)) {
            collectFromFor(fs, vars);
            return vars;
        }

        if (auto *call = dyn_cast<clang::CallExpr>(stmt)) {
            collectFromCall(call, vars);
            return vars;
        }

        return vars;
    }

    namespace {
        inline bool enabled() {
            const char *no = std::getenv("NO_COLOR");
            return !no || *no == '\0';
        }

        inline const char *reset() {
            static const char *s = "\033[0m";
            return enabled() ? s : "";
        }
        inline const char *bold() {
            static const char *s = "\033[1m";
            return enabled() ? s : "";
        }
        inline const char *dim() {
            static const char *s = "\033[90m";
            return enabled() ? s : "";
        }
        inline const char *bright_red() {
            static const char *s = "\033[91m";
            return enabled() ? s : "";
        }
        inline const char *bright_green() {
            static const char *s = "\033[92m";
            return enabled() ? s : "";
        }
        inline const char *bright_yellow() {
            static const char *s = "\033[93m";
            return enabled() ? s : "";
        }
        inline const char *bright_blue() {
            static const char *s = "\033[94m";
            return enabled() ? s : "";
        }
        inline const char *bright_magenta() {
            static const char *s = "\033[95m";
            return enabled() ? s : "";
        }
        inline const char *bright_cyan() {
            static const char *s = "\033[96m";
            return enabled() ? s : "";
        }

        inline std::string wrap(const char *color, std::string_view s) {
            if (!enabled())
                return std::string(s);
            std::ostringstream oss;
            oss << color << s << reset();
            return oss.str();
        }
    } // namespace

    namespace dump_fmt {
        std::string type(std::string_view s) { return wrap(bright_red(), s); }
        std::string key(std::string_view s) { return wrap(bright_yellow(), s); }
        std::string op(std::string_view s) { return wrap(bright_magenta(), s); }
        std::string lit(std::string_view s) { return wrap(bright_green(), s); }
        std::string path(std::string_view s) { return wrap(bright_blue(), s); }
        std::string accent(std::string_view s) { return wrap(bright_cyan(), s); }
        std::string hint(std::string_view s) { return wrap(dim(), s); }
    } // namespace dump_fmt

    std::optional<clang::QualType> findSizeofQualType(const clang::Expr *E) {
        using namespace clang;
        if (!E)
            return std::nullopt;

        // Normalize trivial wrappers to reduce noise.
        const Expr *Cur = E->IgnoreParenImpCasts();

        // Check the current node.
        if (const auto *U = llvm::dyn_cast<UnaryExprOrTypeTraitExpr>(Cur)) {
            if (U->getKind() == UETT_SizeOf) {
                if (U->isArgumentType())
                    return U->getArgumentType();
                if (const Expr *Arg = U->getArgumentExpr())
                    return Arg->getType();
            }
        }

        // Recurse into children; Stmt::children() covers all sub-expressions.
        for (const Stmt *S : Cur->children()) {
            if (!S)
                continue;
            if (const auto *CE = llvm::dyn_cast<Expr>(S)) {
                if (auto QT = findSizeofQualType(CE))
                    return QT;
            }
        }
        return std::nullopt;
    }

    // Count how many `sizeof(...)` occurrences exist within an expression tree.
    size_t countSizeofInExpr(const clang::Expr *E) {
        using namespace clang;
        if (!E)
            return 0;
        const Expr *Cur = E->IgnoreParenImpCasts();
        size_t cnt      = 0;

        if (const auto *U = llvm::dyn_cast<UnaryExprOrTypeTraitExpr>(Cur)) {
            if (U->getKind() == UETT_SizeOf)
                ++cnt;
        }
        for (const Stmt *S : Cur->children()) {
            if (!S)
                continue;
            if (const auto *CE = llvm::dyn_cast<Expr>(S))
                cnt += countSizeofInExpr(CE);
        }
        return cnt;
    }

    // Count `sizeof(...)` occurrences over all call arguments.
    size_t countSizeofInCall(const clang::CallExpr *call) {
        size_t total = 0;
        for (unsigned i = 0; i < call->getNumArgs(); ++i)
            total += countSizeofInExpr(call->getArg(i));
        return total;
    }

    // Decide whether a QualType denotes a builtin scalar (e.g., uint64_t via typedef).
    bool isBuiltinScalar(const clang::QualType QT) {
        auto CT              = QT.getCanonicalType();
        const clang::Type *T = CT.getTypePtrOrNull();
        if (!T)
            return false;
        // Accept integers, bool, and character types; extend as needed.
        return T->isIntegerType() || T->isBooleanType() || T->isAnyCharacterType();
    }

} // namespace acslg::utils
