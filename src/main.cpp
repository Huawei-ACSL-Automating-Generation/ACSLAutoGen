#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include <memory>
#include <filesystem>
#include "Context/context.h"
#include "Analyzer/analysis.h"
#include "macros.h"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;
using namespace acslg;

namespace fs = std::filesystem;

static cl::OptionCategory ACSLGCategory("ACSLG options");

static cl::opt<bool> ASTOnly(
    "ast-only",
    cl::desc("Output only the entire AST (Decls) using Clang's pretty print"),
    cl::cat(ACSLGCategory));

class TUASTConsumer : public ASTConsumer {
  public:
    void HandleTranslationUnit(ASTContext &context) override {
        if (ASTOnly) {
            TranslationUnitDecl *TUDecl = context.getTranslationUnitDecl();
            TUDecl->dump();
        } else {
            auto acslContext = context::ACSLContext{context};
            auto analyzer    = analyzer::ACSLAnalyzer{acslContext};
            analyzer.analyzeFunctions();
            auto &SM       = acslContext.getSourceManager();
            auto &rewriter = acslContext.getRewriter();
            std::error_code EC;

            // TODO: replace "with_acsl.c" with user-defined relative path.
            auto path = fs::path{SM.getFilename(SM.getLocForStartOfFile(SM.getMainFileID())).str()};
            path.replace_filename(
                path.stem().concat("_with_acsl").concat(path.extension().string()));
            llvm::raw_fd_ostream Out(path.string(), EC, llvm::sys::fs::OF_None);
            if (EC)
                ERROR("Error opening file " + path.string() + ": " + EC.message());

            rewriter.getEditBuffer(SM.getMainFileID()).write(Out);
            if (Out.has_error())
                ERROR("Error writing to " + path.string() + ": " + Out.error().message());
        }
    }
};

class ACSLCommentHandler : public CommentHandler {
    Rewriter rewriter_;

    static bool isACSL(StringRef s) {
        StringRef t = s.ltrim();
        return t.starts_with("/*@") || t.starts_with("//@") || t.contains("\n//@");
    }

    static StringRef firstTokenAfterMarker(StringRef s) {
        s = s.ltrim();
        if (s.starts_with("/*@")) {
            s = s.drop_front(3);
            if (s.ends_with("@*/"))
                s = s.drop_back(3);
            if (s.ends_with("*/"))
                s = s.drop_back(2);
        } else if (s.starts_with("//@")) {
            s = s.drop_front(3);
        } else {
            size_t pos = s.rfind("//@");
            if (pos != StringRef::npos)
                s = s.substr(pos + 3);
        }
        s        = s.ltrim();
        size_t i = 0;
        while (i < s.size() && (isalnum(s[i]) || s[i] == '_' || s[i] == '-'))
            ++i;
        return s.substr(0, i);
    }

    static bool isTopLevelRequires(StringRef raw) {
        auto tok = firstTokenAfterMarker(raw).lower();
        return tok == "requires";
    }

  public:
    ACSLCommentHandler(const Rewriter &rewriter) : rewriter_(rewriter) {}

    bool HandleComment(Preprocessor &PP, SourceRange comment) override {
        const SourceManager &SM     = PP.getSourceManager();
        const LangOptions &LO       = PP.getLangOpts();
        clang::SourceLocation begin = SM.getFileLoc(comment.getBegin());
        clang::SourceLocation end   = SM.getFileLoc(comment.getEnd());

        if (!begin.isValid() || !end.isValid())
            return false;

        end = clang::Lexer::getLocForEndOfToken(end, 0, SM, LO);

        if (!SM.isWrittenInMainFile(begin))
            return false;
        if (SM.getFileID(begin) != SM.getFileID(end))
            return false;
        if (!rewriter_.isRewritable(begin) || !rewriter_.isRewritable(end))
            return false;

        llvm::StringRef raw =
            clang::Lexer::getSourceText(clang::CharSourceRange::getCharRange(begin, end), SM, LO);

        if (!isACSL(raw))
            return false;

        if (!isTopLevelRequires(raw)) {
            clang::CharSourceRange charRange = clang::CharSourceRange::getCharRange(begin, end);
            rewriter_.ReplaceText(charRange, "");
        }

        return false;
    }
};

class TUFrontendAction : public ASTFrontendAction {
  public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef) override {
        auto &SM = CI.getSourceManager();
        auto &LO = CI.getLangOpts();
        Rewriter rewriter;
        rewriter.setSourceMgr(SM, LO);
        commentHandler_ = std::make_unique<ACSLCommentHandler>(rewriter);
        CI.getPreprocessor().addCommentHandler(commentHandler_.get());
        return std::make_unique<TUASTConsumer>();
    }

  private:
    std::unique_ptr<ACSLCommentHandler> commentHandler_;
};

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ACSLGCategory);
    if (!ExpectedParser) {
        llvm::errs() << "Error while parsing options.\n";
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();

    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    Tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-xc", ArgumentInsertPosition::BEGIN));
    Tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-std=c11", ArgumentInsertPosition::END));
    return Tool.run(newFrontendActionFactory<TUFrontendAction>().get());
}
