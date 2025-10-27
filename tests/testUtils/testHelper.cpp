// tests/testUtils/testHelper.cpp

#include "testHelper.h"

#include "Analyzer/analysis.h"
#include "SpecGenerator/specGenerator.h"
#include <clang/AST/Type.h>

using namespace std;
using namespace clang;
using namespace llvm;

namespace acslg::test::utils {
    using namespace analyzer;
    using namespace context;
    using namespace spec_generator;
    using namespace ::acslg::utils;

    optional<string> doPluginOnFirstFunc(const string &code, const string &pid) {
        static ASTExtractor e;
        static optional<ACSLGContext> context{};

        e.init(code);
        context.emplace(e.getASTContext());
        auto func     = e.findFirstDecl<FunctionDecl>();
        auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
        preState->init();
        DEBUG(preState->dump());
        auto postState = preState->clone();
        for (Stmt *stmt : func->getBody()->children()) {
            postState->step(stmt);
            DEBUG(postState->dump());
        }

        auto *pl = ACSLPluginRegistry::instance().get(pid);
        if (!pl)
            ERROR("Plugin with id " + pid + " does not exist!");
        auto *fcp = dynamic_cast<const FunctionContractPlugin *>(pl);

        auto [spec, _] = fcp->generate(*preState, *postState);
        if (spec)
            DEBUG(*spec);
        return spec;
    }

    std::string doAll(const string_view code) {
        static ASTExtractor e;
        static optional<ACSLGContext> context{};

        e.init(code);
        context.emplace(e.getASTContext());
        ACSLAnalyzer analyzer(context.value());
        analyzer.analyzeFunctions();
        for (auto &str : context.value().getInsertedStrings()) {
            DEBUG(str);
        }
        return context.value().getModifiedSource();
    }

    unique_ptr<ProgramState> execOnFirstFunc(const string &code) {
        static ASTExtractor e;
        static optional<ACSLGContext> context{};

        e.init(code);
        context.emplace(e.getASTContext());
        auto func     = e.findFirstDecl<FunctionDecl>();
        auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
        preState->init();
        DEBUG(preState->dump());
        auto postState = preState->clone();
        for (Stmt *stmt : func->getBody()->children()) {
            postState->step(stmt);
            DEBUG(postState->dump());
        }

        return postState;
    }

    not_null<unique_ptr<symbolic::SymbolicExpr>> getReturnExprOfFirstPath(
        const ProgramState &state) {
        if (state.getPaths().empty())
            ERROR("Empty paths_!");
        auto &firstPath  = state.getPaths()[0];
        auto &returnExpr = firstPath->getReturnExpr();
        if (returnExpr == nullopt)
            ERROR("There is no returnExpr!");
        return returnExpr.value()->clone();
    }

    not_null<unique_ptr<ProgramState>> getPostStateOfFirstLoop(const string_view code) {
        static ASTExtractor e;
        static optional<ACSLGContext> context{};

        e.init(code);
        context.emplace(e.getASTContext());
        auto func = e.findFirstDecl<FunctionDecl>();
        auto symbolicState =
            make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
        symbolicState->init();
        DEBUG(symbolicState->dump());
        for (Stmt *stmt : func->getBody()->children()) {
            symbolicState->step(stmt);
            if (isa<WhileStmt>(stmt) || isa<ForStmt>(stmt) || isa<DoStmt>(stmt)) {
                DEBUG(symbolicState->dump());
                break;
            }
        }
        return symbolicState;
    }

    pair<LoopInfo, bool> doPluginsOnFirstLoop(string_view code, const vector<string> pids) {
        static ASTExtractor e;
        static optional<ACSLGContext> context{};

        e.init(code);
        context.emplace(e.getASTContext());
        auto func     = e.findFirstDecl<FunctionDecl>();
        auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
        Stmt *loopStmt;
        preState->init();
        DEBUG(preState->dump());
        for (Stmt *stmt : func->getBody()->children()) {
            if (isa<WhileStmt>(stmt) || isa<ForStmt>(stmt) || isa<DoStmt>(stmt)) {
                loopStmt = stmt;
                break;
            }
            preState->step(stmt);
            DEBUG(preState->dump());
        }

        auto loopEntry = preState->clone();
        if (auto forLoop = dyn_cast<ForStmt>(loopStmt); forLoop && forLoop->getInit()) {
            loopEntry->step(forLoop->getInit());
            DEBUG(loopEntry->dump());
        }

        LoopInfo loopInfo{loopStmt};

        if (auto *pl = ACSLPluginRegistry::instance().get("setLoopEntry")) {
            auto *setLoopEntryPlugin = dynamic_cast<const LoopInfoPlugin *>(pl);

            if (!setLoopEntryPlugin->parse(*preState, *loopEntry, loopInfo))
                ERROR("Set loop entry fail.");
        } else {
            ERROR("Set loop entry fail.");
        }

        bool result;
        for (auto &pid : pids) {
            auto *pl = ACSLPluginRegistry::instance().get(pid);
            if (!pl)
                ERROR("Plugin with id " + pid + " does not exist!");
            auto *fcp = dynamic_cast<const LoopInfoPlugin *>(pl);
            result    = fcp->parse(*preState, *loopEntry, loopInfo);
        }
        return pair{std::move(loopInfo), result};
    }

    spec_generator::LoopInvariantPlugin::GenResultType doPluginOnFirstLoop(const string &code,
                                                                           const string &pid) {
        static ASTExtractor e;
        static optional<ACSLGContext> context{};

        e.init(code);
        context.emplace(e.getASTContext());
        auto func     = e.findFirstDecl<FunctionDecl>();
        auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
        Stmt *loopStmt;
        preState->init();
        DEBUG(preState->dump());
        for (Stmt *stmt : func->getBody()->children()) {
            if (isa<WhileStmt>(stmt) || isa<ForStmt>(stmt) || isa<DoStmt>(stmt)) {
                loopStmt = stmt;
                break;
            }
            preState->step(stmt);
            DEBUG(preState->dump());
        }

        auto loopEntry = preState->clone();
        if (auto forLoop = dyn_cast<ForStmt>(loopStmt); forLoop && forLoop->getInit()) {
            loopEntry->step(forLoop->getInit());
            DEBUG(loopEntry->dump());
        }

        auto [loopInfo, ok] = parseLoopInfo(*preState, *loopEntry, loopStmt);
        if (!ok) {
            // TODO(complex loop)
            UNIMPLEMENT("Loop is too complex!");
        }

        auto *pl = ACSLPluginRegistry::instance().get(pid);
        if (!pl)
            ERROR("Plugin with id " + pid + " does not exist!");
        auto *fcp = dynamic_cast<const LoopInvariantPlugin *>(pl);

        return fcp->generate(*preState, *loopEntry, loopInfo);
    }

    FixtureWithCode::FixtureWithCode()
        : e(code), defaultPoint(symbolic::SourcePoint::fromFuncDecl(e.findFirstDecl<FunctionDecl>(),
                                                                    e.getSourceManager(),
                                                                    e.getLangOptions())) {
        for (auto d : e.getASTContext().getTranslationUnitDecl()->decls()) {
            if (auto vd = dyn_cast<VarDecl>(d))
                varDecls.push_back(vd);
        }
        for (auto d : e.getASTContext().getTranslationUnitDecl()->decls()) {
            if (auto fd = dyn_cast<FunctionDecl>(d)) {
                if (fd->getNameAsString().find("func") == string::npos)
                    funcDecls.push_back(fd);
            }
        }
    }

    not_null<const VarDecl *> FixtureWithCode::getVarDecl(unsigned int id) {
        if (!varIdCountMap.contains(id)) {
            assert(var_count < varDecls.size() && "Need more varDecl? Change the for loop above!");
            varIdCountMap[id] = var_count++;
        }
        return varDecls.at(varIdCountMap.at(id));
    }

    not_null<const FunctionDecl *> FixtureWithCode::getFuncDecl(unsigned int id) {
        if (!funcIdCountMap.contains(id)) {
            assert(func_count < funcDecls.size() &&
                   "Need more FunctionDecl? Change the for loop above!");
            funcIdCountMap[id] = func_count++;
        }
        return funcDecls.at(funcIdCountMap.at(id));
    }

    symbolic::VariableAddress FixtureWithCode::makeVariableAddr(unsigned int id) {
        return symbolic::VariableAddress{getVarDecl(id)};
    }

    symbolic::SymbolAddress FixtureWithCode::makeRangeAddr(
        unsigned int id,
        unique_ptr<const symbolic::SymbolicExpr> offset,
        unique_ptr<const symbolic::SymbolicExpr> len,
        optional<symbolic::SourcePoint> fromPoint) {
        auto baseAddr = makeVariableAddr(id);
        if (len != nullptr)
            return symbolic::SymbolAddress{QualType{}, baseAddr.addressClone().into_underlying(),
                                           fromPoint.value_or(defaultPoint), std::move(offset),
                                           std::move(len)};
        return symbolic::SymbolAddress{QualType{}, baseAddr.addressClone().into_underlying(),
                                       fromPoint.value_or(defaultPoint), std::move(offset),
                                       nullopt};
    }

    unique_ptr<symbolic::SymbolValue> FixtureWithCode::makeSymbolValue(
        unsigned int id,
        optional<symbolic::SourcePoint> fromPoint) {
        return make_unique<symbolic::SymbolValue>(
            symbolic::SymbolicExpr::Type{symbolic::SymbolicExpr::ScalarKind::UInt, id},
            make_unique<symbolic::VariableAddress>(getVarDecl(id)),
            fromPoint.value_or(defaultPoint));
    }

    symbolic::SymbolAddress FixtureWithCode::makeSimpleSymbolAddr(
        unsigned int id,
        optional<symbolic::SourcePoint> fromPoint) {
        auto baseAddr = makeVariableAddr(id);
        return symbolic::SymbolAddress{QualType{}, baseAddr.addressClone().into_underlying(),
                                       fromPoint.value_or(defaultPoint), nullopt, nullopt};
    }

    symbolic::SymbolAddress FixtureWithCode::makePointAddr(unsigned int id, uint64_t off) {
        return makeRangeAddr(id, make_unique<symbolic::LiteralExpr>(static_cast<uint64_t>(off)),
                             nullptr);
    }

    ::testing::AssertionResult FixtureWithCode::ExpectReadEqAt(
        MemoryModel &mm,
        unsigned id,
        uint64_t off,
        const symbolic::SymbolicExpr &expected) {
        auto addr = makePointAddr(id, off);
        auto got  = mm.read(addr);
        if (!got) {
            return ::testing::AssertionFailure() << "read returned null at off=" << off;
        }
        if (*got.value() != expected) {
            return ::testing::AssertionFailure()
                   << "mismatch at off=" << off << "\n  got:      " << *got
                   << "\n  expected: " << expected;
        }
        return ::testing::AssertionSuccess();
    }

    ::testing::AssertionResult FixtureWithCode::ExpectReadNullAt(MemoryModel &mm,
                                                                 unsigned id,
                                                                 uint64_t off) {
        auto addr = makePointAddr(id, off);
        if (mm.read(addr) != std::nullopt) {
            return ::testing::AssertionFailure() << "expected null at off=" << off;
        }
        return ::testing::AssertionSuccess();
    }
} // namespace acslg::test::utils