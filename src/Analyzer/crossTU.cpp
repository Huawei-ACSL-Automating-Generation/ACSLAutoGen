#include "crossTU.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTImporter.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Error.h"

#include "Context/context.h"

namespace acslg::analyzer::ctu {
    namespace {
        const clang::tooling::CompilationDatabase *gDB = nullptr;
        bool gEnableFramacCompat                       = false;

        std::mutex gMutex;
        std::unordered_map<std::string, std::unique_ptr<clang::ASTUnit>> gASTCacheByFile;
        std::unordered_map<std::string, const clang::FunctionDecl *> gImportedByKey;

        std::vector<std::string> normalizeArgs(const clang::tooling::CompileCommand &cmd) {
            std::vector<std::string> args;
            args.emplace_back("-xc");
            args.emplace_back("-std=c11");
            if (gEnableFramacCompat)
                args.emplace_back("-D__FRAMAC__");

            for (size_t i = 1; i < cmd.CommandLine.size(); ++i) {
                const auto &a = cmd.CommandLine[i];
                if (a == "-c") {
                    ++i; // skip input
                    continue;
                }
                if (a == "-o") {
                    ++i; // skip output
                    continue;
                }
                if (a == "-Werror")
                    continue;
                if (a == "-Wno-stringop-overread")
                    continue;
                args.push_back(a);
            }

            args.emplace_back("-Wno-unknown-warning-option");
            args.emplace_back("-Wno-error=unknown-warning-option");
            return args;
        }

        const clang::FunctionDecl *findDefinitionInAST(const clang::ASTContext &fromCtx,
                                                       llvm::StringRef name,
                                                       unsigned paramCount) {
            const auto *TU = fromCtx.getTranslationUnitDecl();
            for (const auto *decl : TU->decls()) {
                auto *FD = llvm::dyn_cast<clang::FunctionDecl>(decl);
                if (!FD)
                    continue;
                if (!FD->hasBody())
                    continue;
                if (FD->getName() != name)
                    continue;
                if (FD->getNumParams() != paramCount)
                    continue;
                return FD;
            }
            return nullptr;
        }

        clang::ASTUnit *getOrBuildASTForFile(const std::string &filePath) {
            if (!gDB)
                return nullptr;

            auto it = gASTCacheByFile.find(filePath);
            if (it != gASTCacheByFile.end())
                return it->second.get();

            auto cmds = gDB->getCompileCommands(filePath);
            if (cmds.empty())
                return nullptr;

            auto bufOrErr = llvm::MemoryBuffer::getFile(filePath);
            if (!bufOrErr)
                return nullptr;

            auto args = normalizeArgs(cmds.front());
            auto ast  = clang::tooling::buildASTFromCodeWithArgs(bufOrErr.get()->getBuffer(), args,
                                                                 filePath);
            if (!ast)
                return nullptr;

            auto *raw = ast.get();
            gASTCacheByFile.emplace(filePath, std::move(ast));
            return raw;
        }

        const clang::FunctionDecl *importDecl(const clang::FunctionDecl *fromFD,
                                              context::ACSLGContext &to,
                                              clang::ASTUnit &fromUnit) {
            auto &toCtx   = to.getASTContext();
            auto &toFM    = to.getSourceManager().getFileManager();
            auto &fromCtx = fromUnit.getASTContext();
            auto &fromFM  = fromUnit.getFileManager();

            clang::ASTImporter importer(toCtx, toFM, fromCtx, fromFM, /*MinimalImport*/ false);
            auto importedOrErr = importer.Import(const_cast<clang::FunctionDecl *>(fromFD));
            if (!importedOrErr) {
                llvm::consumeError(importedOrErr.takeError());
                return nullptr;
            }
            return llvm::dyn_cast_or_null<clang::FunctionDecl>(*importedOrErr);
        }

        const clang::FunctionDecl *findAndImportDefinition(const clang::FunctionDecl *callee,
                                                           context::ACSLGContext &ctx) {
            if (!gDB)
                return nullptr;

            const std::string name    = callee->getNameAsString();
            const std::string needle1 = name + "(";
            const std::string needle2 = name + " (";
            const unsigned paramCount = callee->getNumParams();

            for (const auto &file : gDB->getAllFiles()) {
                auto bufOrErr = llvm::MemoryBuffer::getFile(file);
                if (!bufOrErr)
                    continue;
                llvm::StringRef text = bufOrErr.get()->getBuffer();
                if (!text.contains(needle1) && !text.contains(needle2))
                    continue;

                auto *ast = getOrBuildASTForFile(file);
                if (!ast)
                    continue;

                auto *def = findDefinitionInAST(ast->getASTContext(), name, paramCount);
                if (!def)
                    continue;

                auto *imported = importDecl(def, ctx, *ast);
                if (imported && imported->hasBody())
                    return imported;
            }
            return nullptr;
        }
    } // namespace

    void init(const clang::tooling::CompilationDatabase &db, bool enableFramacCompat) {
        std::scoped_lock lock(gMutex);
        gDB                 = &db;
        gEnableFramacCompat = enableFramacCompat;
    }

    const clang::FunctionDecl *importDefinitionIfAvailable(const clang::FunctionDecl *callee,
                                                           context::ACSLGContext &ctx) {
        if (!callee || callee->hasBody())
            return callee;
        if (!gDB)
            return nullptr;

        const std::string name = callee->getNameAsString();
        if (name.empty())
            return nullptr;

        const std::string key = name + "/" + std::to_string(callee->getNumParams());

        std::scoped_lock lock(gMutex);

        if (auto it = gImportedByKey.find(key); it != gImportedByKey.end())
            return it->second;

        auto *imported = findAndImportDefinition(callee, ctx);
        if (!imported)
            return nullptr;

        gImportedByKey.emplace(key, imported);
        return imported;
    }
} // namespace acslg::analyzer::ctu
