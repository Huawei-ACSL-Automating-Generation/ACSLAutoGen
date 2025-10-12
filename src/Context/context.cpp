#include "context.h"
#include "clang/Lex/Lexer.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/SourceManager.h"

using namespace std;
using namespace clang;
using namespace llvm;

namespace acslg::context {
    vector<const FunctionDecl *> ACSLContext::getFunctions() const {
        vector<const FunctionDecl *> funcs;
        for (const auto *decl : TU_->decls()) {
            if (const auto *funcDecl = dyn_cast<FunctionDecl>(decl))
                funcs.push_back(funcDecl);
        }
        return funcs;
    }

    optional<tuple<string, StringRef, StringRef, unsigned, unsigned>> ACSLContext::getDeclInfo(
        const Decl *decl) {
        tuple<string, StringRef, StringRef, unsigned, unsigned> result;
        if (!decl)
            return nullopt;
        if (auto namedDecl = dyn_cast<NamedDecl>(decl); namedDecl)
            get<0>(result) = namedDecl->getNameAsString();

        SourceRange range         = decl->getSourceRange();
        CharSourceRange charRange = CharSourceRange::getTokenRange(range);
        get<1>(result)            = Lexer::getSourceText(charRange, SM_, LO_);

        SourceLocation loc = decl->getBeginLoc();
        get<2>(result)     = SM_.getFilename(loc);
        get<3>(result)     = SM_.getSpellingLineNumber(loc);
        get<4>(result)     = SM_.getSpellingColumnNumber(loc);
        return result;
    }

    optional<tuple<StringRef, StringRef, unsigned, unsigned>> ACSLContext::getStmtInfo(
        const Stmt *stmt) {
        tuple<StringRef, StringRef, unsigned, unsigned> result;
        if (!stmt)
            return nullopt;

        SourceRange range         = stmt->getSourceRange();
        CharSourceRange charRange = CharSourceRange::getTokenRange(range);
        get<0>(result)            = Lexer::getSourceText(charRange, SM_, LO_);

        SourceLocation loc = stmt->getBeginLoc();
        get<1>(result)     = SM_.getFilename(loc);
        get<2>(result)     = SM_.getSpellingLineNumber(loc);
        get<3>(result)     = SM_.getSpellingColumnNumber(loc);
        return result;
    }
} // namespace acslg::context