// emit_subclasses.cpp
// -----------------------------------------------------------
// A Clang LibTooling tool that scans C++ source files and
// generates a list of all **direct subclasses** of a given base class.
// Usage example:
//
//   emit_subclasses --base acslg::analyzer::symbolic::Symbol \
//                   -o subclasses.inc \
//                   -p build \
//                   -- src/file1.cpp src/file2.cpp
//
// The tool writes an `.inc` file with lines like:
//
//   SUBCLASS(acslg::analyzer::symbolic::OverRangeExpr)
//   SUBCLASS(acslg::analyzer::symbolic::SymbolValue)
// -----------------------------------------------------------

#include <clang/AST/AST.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <unordered_set>
#include <string>

using namespace clang;
using namespace clang::tooling;

namespace acslg::tool {

    // ---------------------- CLI options ----------------------
    // These command-line options let users specify the base class name,
    // output file, and optional verbose debug mode.

    static llvm::cl::OptionCategory Cat("emit-subclasses options");

    static llvm::cl::opt<std::string> BaseName(
        "base",
        llvm::cl::Required,
        llvm::cl::desc("Qualified base class name, e.g. acslg::analyzer::symbolic::Symbol"),
        llvm::cl::value_desc("qualified_name"),
        llvm::cl::cat(Cat));

    static llvm::cl::opt<std::string> OutPath("o",
                                              llvm::cl::Required,
                                              llvm::cl::desc("Output .inc file path"),
                                              llvm::cl::value_desc("path"),
                                              llvm::cl::cat(Cat));

    static llvm::cl::opt<bool> Verbose("v",
                                       llvm::cl::init(false),
                                       llvm::cl::desc("Verbose logging"),
                                       llvm::cl::cat(Cat));

    // ---------------------- Shared data collector ----------------------
    // Stores the base class name and all discovered direct subclasses.

    struct Collector {
        std::string_view BaseQName; // The fully-qualified base class name from CLI
        std::string_view BaseScope; // its scope (namespace chain)
        std::unordered_set<std::string> DirectDerived; // All direct subclass names (unqualified)
    };

    // ---------------------- Utility: get base class decl ----------------------
    // Safely extract the CXXRecordDecl from a base type.
    // This supports normal classes, template specializations, and canonical types.
    // Careful null checks prevent segmentation faults.

    static const CXXRecordDecl *getBaseAsCXXRecord(const QualType &QT) {
        const Type *T = QT.getTypePtrOrNull();
        if (!T)
            return nullptr;

        // Case 1: Normal record type
        if (auto *RT = T->getAs<RecordType>())
            return llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());

        // Case 2: Template specialization
        if (auto *TST = T->getAs<TemplateSpecializationType>())
            return TST->getAsCXXRecordDecl();

        // Case 3: Retry on canonical type (handles typedefs, using aliases, etc.)
        QualType Can   = QT.getCanonicalType();
        const Type *CT = Can.getTypePtrOrNull();
        if (!CT)
            return nullptr;

        if (auto *RT = CT->getAs<RecordType>())
            return llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());

        if (auto *TST = CT->getAs<TemplateSpecializationType>())
            return TST->getAsCXXRecordDecl();

        return nullptr;
    }

    // ---------------------- helpers ----------------------
    // Return the "scope" part (namespace or enclosing scope) of a qualified name.
    // For "ns1::ns2::Type" -> "ns1::ns2"; for "Type" -> "" (global scope).
    static std::string_view scopeOfQualified(std::string_view QN) {
        size_t pos = QN.rfind("::");
        if (pos == std::string::npos)
            return std::string_view(); // global scope
        return QN.substr(0, pos);
    }

    // ---------------------- AST Visitor ----------------------
    // Walks all CXXRecordDecl nodes in the AST.
    // For each class definition, checks if it directly inherits the specified base class.
    // If so, it records it in the Collector.

    class Visitor : public RecursiveASTVisitor<Visitor> {
      public:
        Visitor(Collector &C) : C(C) {}

        bool VisitCXXRecordDecl(CXXRecordDecl *RD) {
            if (!RD)
                return true;

            // Only visit complete definitions (skip forward declarations)
            if (!RD->isCompleteDefinition())
                return true;

            // Skip the base class itself
            const std::string QN = RD->getQualifiedNameAsString();
            if (QN == C.BaseQName)
                return true;

            auto ClassScope = scopeOfQualified(QN);
            if (ClassScope != C.BaseScope) {
                if (Verbose) {
                    llvm::errs() << "[Skip] Different scope: "
                                 << "class=" << QN << " (scope=" << ClassScope << ") "
                                 << "baseScope=" << C.BaseScope << "\n";
                }
                return true; // skip base traversal entirely
            }

            if (Verbose) {
                llvm::errs() << "[Visit] Class in same scope: " << QN << "\n";
            }

            // Iterate over direct bases of this class
            for (const CXXBaseSpecifier &BS : RD->bases()) {
                const QualType BT       = BS.getType();
                const CXXRecordDecl *BR = getBaseAsCXXRecord(BT);
                if (!BR) {
                    if (Verbose)
                        llvm::errs() << "   - base: <unresolved>\n";
                    continue;
                }

                const std::string BName = BR->getQualifiedNameAsString();
                if (Verbose)
                    llvm::errs() << "   - base: " << BName << "\n";

                // If one of the direct bases matches our target base, this is a direct subclass
                if (BName == C.BaseQName) {
                    if (Verbose)
                        llvm::errs() << "   -> DIRECT SUBCLASS FOUND: " << QN << "\n";
                    C.DirectDerived.insert(QN);
                    break; // no need to check other bases
                }
            }

            return true;
        }

      private:
        Collector &C;
    };

    // ---------------------- ASTConsumer ----------------------
    // Creates the AST visitor and starts traversal for each translation unit (TU).

    class Consumer : public ASTConsumer {
      public:
        Consumer(Collector &C) : C(C) {}

        void HandleTranslationUnit(ASTContext &Context) override {
            if (Verbose)
                llvm::errs() << "[TU] Start traversing AST\n";
            Visitor V(C);
            V.TraverseDecl(Context.getTranslationUnitDecl());
            if (Verbose)
                llvm::errs() << "[TU] Done traversing AST\n";
        }

      private:
        Collector &C;
    };

    // ---------------------- FrontendAction ----------------------
    // The frontend action that runs our consumer for each source file.

    class Action : public ASTFrontendAction {
      public:
        explicit Action(Collector &C) : C(C) {}

        std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                       llvm::StringRef) override {
            CI.getDiagnostics().setIgnoreAllWarnings(true);
            return std::make_unique<Consumer>(C);
        }

      private:
        Collector &C;
    };

    // ---------------------- Custom ActionFactory ----------------------
    // Allows passing a reference to our shared Collector into each FrontendAction.

    class ActionFactory : public FrontendActionFactory {
      public:
        explicit ActionFactory(Collector &C) : C(C) {}
        std::unique_ptr<FrontendAction> create() override { return std::make_unique<Action>(C); }

      private:
        Collector &C;
    };
} // namespace acslg::tool

// ---------------------- main() ----------------------
// Entry point: parse arguments, run ClangTool, collect subclasses, write output.

int main(int argc, const char **argv) {
    using namespace acslg::tool;
    // Parse CLI arguments
    auto ExpParser = CommonOptionsParser::create(argc, argv, Cat);
    if (!ExpParser) {
        llvm::errs() << llvm::toString(ExpParser.takeError()) << "\n";
        return 1;
    }
    auto &Parser = *ExpParser;

    // Sanity checks
    if (BaseName.empty()) {
        llvm::errs() << "error: --base is required\n";
        return 1;
    }
    if (OutPath.empty()) {
        llvm::errs() << "error: -o is required\n";
        return 1;
    }
    if (Parser.getSourcePathList().empty()) {
        llvm::errs() << "error: no source files provided\n";
        return 1;
    }

    if (Verbose) {
        llvm::errs() << "[Args] base = " << BaseName << "\n";
        llvm::errs() << "[Args] out  = " << OutPath << "\n";
        llvm::errs() << "[Args] files:\n";
        for (auto &F : Parser.getSourcePathList())
            llvm::errs() << "  - " << F << "\n";
    }

    // Shared collector for all translation units
    Collector C{.BaseQName = BaseName, .BaseScope = scopeOfQualified(BaseName), .DirectDerived{}};

    // Run the ClangTool over all source files
    ClangTool Tool(Parser.getCompilations(), Parser.getSourcePathList());
    ActionFactory Factory(C);
    int rc = Tool.run(&Factory);
    if (rc != 0) {
        llvm::errs() << "[Error] ClangTool run failed with code " << rc << "\n";
        return rc;
    }

    // Write results to output file
    std::error_code EC;
    llvm::raw_fd_ostream OS(OutPath, EC, llvm::sys::fs::OF_Text);
    if (EC) {
        llvm::errs() << "[Error] Cannot open output file " << OutPath << " : " << EC.message()
                     << "\n";
        return 1;
    }

    OS << R"(#ifndef SUBCLASS
#define SUBCLASS(NAME)
#endif

)";

    for (const auto &Q : C.DirectDerived) {
        OS << "SUBCLASS(" << Q << ")\n";
    }

    if (Verbose) {
        llvm::errs() << "[OK] Wrote " << C.DirectDerived.size() << " entries to " << OutPath
                     << "\n";
    }

    return 0;
}