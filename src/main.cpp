#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include <memory>
#include <filesystem>
#include <string>
#include <vector>
#include "Context/context.h"
#include "Analyzer/analysis.h"
#include "Analyzer/crossTU.h"
#include "macros.h"

namespace acslg {

    namespace fs = std::filesystem;

    static llvm::cl::OptionCategory ACSLGCategory("ACSLG options");

    static llvm::cl::opt<bool> ASTOnly(
        "ast-only",
        llvm::cl::desc("Output only the entire AST (Decls) using Clang's pretty print"),
        llvm::cl::cat(ACSLGCategory));

    static llvm::cl::list<std::string> TargetFunctions(
        "func",
        llvm::cl::desc("Functions to analyze (analyze all when omitted)"),
        llvm::cl::CommaSeparated,
        llvm::cl::ZeroOrMore,
        llvm::cl::cat(ACSLGCategory));

    class TUASTConsumer : public clang::ASTConsumer {
      public:
        void HandleTranslationUnit(clang::ASTContext &context) override {
            if (ASTOnly) {
                clang::TranslationUnitDecl *TUDecl = context.getTranslationUnitDecl();
                TUDecl->dump();
            } else {
                auto acslContext = context::ACSLGContext{context};
                std::vector<std::string> targetFuncs(TargetFunctions.begin(),
                                                     TargetFunctions.end());
                auto analyzer = analyzer::ACSLAnalyzer{acslContext, std::move(targetFuncs)};
                analyzer.analyzeFunctions();
                auto &SM       = acslContext.getSourceManager();
                auto &rewriter = acslContext.getRewriter();
                std::error_code EC;

                // TODO: replace "with_acsl.c" with user-defined relative path.
                auto path =
                    fs::path{SM.getFilename(SM.getLocForStartOfFile(SM.getMainFileID())).str()};
                // Keep original extension and insert "_with_acsl" before it: foo.c -> foo_with_acsl.c
                auto stem       = path.stem().string();
                auto extension  = path.extension();
                auto parentPath = path.parent_path();
                auto outName    = stem + "_with_acsl" + extension.string();
                path            = parentPath / outName;
                llvm::raw_fd_ostream Out(path.string(), EC, llvm::sys::fs::OF_None);
                if (EC)
                    ERROR("Error opening file " + path.string() + ": " + EC.message());

                rewriter.getEditBuffer(SM.getMainFileID()).write(Out);
                if (Out.has_error())
                    ERROR("Error writing to " + path.string() + ": " + Out.error().message());
            }
        }
    };

    class ACSLCommentHandler : public clang::CommentHandler {
        clang::Rewriter rewriter_;

        static bool isACSL(llvm::StringRef s) {
            llvm::StringRef t = s.ltrim();
            return t.starts_with("/*@") || t.starts_with("//@") || t.contains("\n//@");
        }

        static llvm::StringRef firstTokenAfterMarker(llvm::StringRef s) {
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
                if (pos != llvm::StringRef::npos)
                    s = s.substr(pos + 3);
            }
            s        = s.ltrim();
            size_t i = 0;
            while (i < s.size() && (isalnum(s[i]) || s[i] == '_' || s[i] == '-'))
                ++i;
            return s.substr(0, i);
        }

        static bool isTopLevelRequires(llvm::StringRef raw) {
            auto tok = firstTokenAfterMarker(raw).lower();
            return tok == "requires";
        }

      public:
        ACSLCommentHandler(const clang::Rewriter &rewriter) : rewriter_(rewriter) {}

        bool HandleComment(clang::Preprocessor &PP, clang::SourceRange comment) override {
            const clang::SourceManager &SM = PP.getSourceManager();
            const clang::LangOptions &LO   = PP.getLangOpts();
            clang::SourceLocation begin    = SM.getFileLoc(comment.getBegin());
            clang::SourceLocation end      = SM.getFileLoc(comment.getEnd());

            if (!begin.isValid() || !end.isValid())
                return false;

            end = clang::Lexer::getLocForEndOfToken(end, 0, SM, LO);

            if (!SM.isWrittenInMainFile(begin))
                return false;
            if (SM.getFileID(begin) != SM.getFileID(end))
                return false;
            if (!rewriter_.isRewritable(begin) || !rewriter_.isRewritable(end))
                return false;

            llvm::StringRef raw = clang::Lexer::getSourceText(
                clang::CharSourceRange::getCharRange(begin, end), SM, LO);

            if (!isACSL(raw))
                return false;

            if (!isTopLevelRequires(raw)) {
                clang::CharSourceRange charRange = clang::CharSourceRange::getCharRange(begin, end);
                rewriter_.ReplaceText(charRange, "");
            }

            return false;
        }
    };

    class TUFrontendAction : public clang::ASTFrontendAction {
      public:
        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &CI,
                                                              llvm::StringRef) override {
            auto &SM = CI.getSourceManager();
            auto &LO = CI.getLangOpts();
            clang::Rewriter rewriter;
            rewriter.setSourceMgr(SM, LO);
            commentHandler_ = std::make_unique<ACSLCommentHandler>(rewriter);
            CI.getPreprocessor().addCommentHandler(commentHandler_.get());
            return std::make_unique<TUASTConsumer>();
        }

      private:
        std::unique_ptr<ACSLCommentHandler> commentHandler_;
    };
} // namespace acslg

int main(int argc, const char **argv) {
    bool enableFramacCompat = false;
    for (int i = 0; i < argc; ++i) {
        if (argv[i] && std::string_view{argv[i]}.find("__FRAMAC__") != std::string_view::npos) {
            enableFramacCompat = true;
            break;
        }
    }

    auto expectedParser =
        clang::tooling::CommonOptionsParser::create(argc, argv, acslg::ACSLGCategory);
    if (!expectedParser) {
        llvm::errs() << "Error while parsing options.\n";
        return 1;
    }
    clang::tooling::CommonOptionsParser &optionsParser = expectedParser.get();

    acslg::analyzer::ctu::init(optionsParser.getCompilations(), enableFramacCompat);

    clang::tooling::ClangTool tool(optionsParser.getCompilations(),
                                   optionsParser.getSourcePathList());
    tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
        "-xc", clang::tooling::ArgumentInsertPosition::BEGIN));
    tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-std=c11", clang::tooling::ArgumentInsertPosition::END));
    return tool.run(clang::tooling::newFrontendActionFactory<acslg::TUFrontendAction>().get());
}
