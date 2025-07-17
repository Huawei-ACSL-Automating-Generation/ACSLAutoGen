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
#include "Context/globalSM.h"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;
namespace fs = std::filesystem;

static cl::OptionCategory ACSLGCategory("ACSLG options");

static cl::opt<bool> ASTOnly(
    "ast-only",
    cl::desc("Output only the entire AST (Decls) using Clang's pretty print"),
    cl::cat(ACSLGCategory));

class TUASTConsumer : public ASTConsumer {
  public:
    void HandleTranslationUnit(ASTContext &Context) override {
        if (ASTOnly) {
            TranslationUnitDecl *TUDecl = Context.getTranslationUnitDecl();
            TUDecl->dump();
        } else {
            ACSLContext acslContext(Context);
            ACSLAnalyzer analyzer(acslContext);
            analyzer.analyzeFunctions();
            auto &SM       = GlobalSM::getSM();
            auto &rewriter = GlobalSM::getRewriter();
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
    Rewriter &TheRewriter;

  public:
    ACSLCommentHandler(Rewriter &R) : TheRewriter(R) {}

    bool HandleComment(Preprocessor &PP, SourceRange CommentRange) override {
        SourceManager &SM = PP.getSourceManager();
        if (!SM.isInMainFile(CommentRange.getBegin()))
            return false;

        // Grab the raw text of the comment
        StringRef text =
            Lexer::getSourceText(CharSourceRange::getCharRange(CommentRange), SM, PP.getLangOpts());

        // Only care about ACSL-style comments: /*@ ... */ or //@ ...
        if (text.starts_with("/*@") || text.starts_with("//@")) {
            //-------------------------------
            // remove all ACSL now
            //-------------------------------
            // TODO(requires): impl this dealing with 'requires'.
            TheRewriter.RemoveText(CharSourceRange::getCharRange(CommentRange));
            return false;
            // If it contains a 'requires', extract only those lines
            if (text.contains("requires")) {
                SmallVector<StringRef, 8> lines;
                text.split(lines, '\n');

                std::string newComment;
                // Reconstruct as a /*@ ... */ block
                if (text.starts_with("/*@"))
                    newComment = "/*@\n";
                else
                    newComment = "//@\n";

                for (auto &line : lines) {
                    if (auto pos = line.find("requires"); pos != StringRef::npos) {
                        newComment += line.substr(pos);
                        newComment += "\n";
                    }
                }

                if (text.starts_with("/*@"))
                    newComment += "*/";

                // TODO: store 'requires'.
                TheRewriter.ReplaceText(CharSourceRange::getCharRange(CommentRange), newComment);
            } else {
                // Remove all ACSL
                TheRewriter.RemoveText(CharSourceRange::getCharRange(CommentRange));
            }
            // TODO: return true causes segfault, why?
            return false;
        }

        return false; // leave other comments alone
    }
};

class TUFrontendAction : public ASTFrontendAction {
  public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef) override {
        GlobalSM::getInstance().initialize(CI.getSourceManager(), CI.getLangOpts());
        commentHandler_ = std::make_unique<ACSLCommentHandler>(GlobalSM::getRewriter());
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

    return Tool.run(newFrontendActionFactory<TUFrontendAction>().get());
}
