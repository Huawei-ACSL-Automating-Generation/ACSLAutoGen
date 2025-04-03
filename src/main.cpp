#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include <memory>
#include "Context/context.h"
#include "Analyzer/analysis.h"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory ACSLGCategory("ACSLG options");

class TUASTConsumer : public ASTConsumer
{
  public:
    void HandleTranslationUnit(ASTContext &Context) override
    {
        ACSLContext acslContext(Context);
        ACSLAnalyzer analyzer(acslContext);
        analyzer.analysis_funcs();
    }
};

class TUFrontendAction : public ASTFrontendAction
{
  public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, StringRef) override
    {
        return std::make_unique<TUASTConsumer>();
    }
};

int main(int argc, const char **argv)
{
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ACSLGCategory);
    if(!ExpectedParser)
    {
        llvm::errs() << "Error while parsing options.\n";
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();

    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

    return Tool.run(newFrontendActionFactory<TUFrontendAction>().get());
}
