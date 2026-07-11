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

    namespace {
        symbolic::ExprFactory *lastExprFactory = nullptr;
        std::unique_ptr<symbolic::ExprFactoryScope> lastExprFactoryScope;

        void rememberExprFactory(ACSLGContext &context) {
            lastExprFactoryScope.reset();
            lastExprFactory = &context.getExprFactory();
            lastExprFactoryScope =
                std::make_unique<symbolic::ExprFactoryScope>(*lastExprFactory);
        }
    } // namespace

    symbolic::ExprFactory &getLastExprFactory() {
        if (lastExprFactory == nullptr)
            ERROR("No test ExprFactory has been initialized.");
        return *lastExprFactory;
    }

    optional<string> doPluginOnFirstFunc(const string &code, const string &pid) {
        static ASTExtractor e;
        static optional<ACSLGContext> context{};

        e.init(code);
        context.emplace(e.getASTContext());
        rememberExprFactory(context.value());
        symbolic::ExprFactoryScope exprScope(context->getExprFactory());
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
        rememberExprFactory(context.value());
        symbolic::ExprFactoryScope exprScope(context->getExprFactory());
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
        rememberExprFactory(context.value());
        symbolic::ExprFactoryScope exprScope(context->getExprFactory());
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
        auto &factory = state.getExprFactory();
        return factory.cloneExpr(factory.importExpr(*returnExpr.value()));
    }

    not_null<unique_ptr<ProgramState>> getPostStateOfFirstLoop(const string_view code) {
        static ASTExtractor e;
        static optional<ACSLGContext> context{};

        e.init(code);
        context.emplace(e.getASTContext());
        rememberExprFactory(context.value());
        symbolic::ExprFactoryScope exprScope(context->getExprFactory());
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
        rememberExprFactory(context.value());
        symbolic::ExprFactoryScope exprScope(context->getExprFactory());
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

        if (auto *pl = ACSLPluginRegistry::instance().get("SetEntryAndCurrent")) {
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

    spec_generator::PathInsensitiveLoopInvPlugin::GenResultType doPIPluginOnFirstLoop(
        const string &code,
        const string &pid) {
        static ASTExtractor e;
        static optional<ACSLGContext> context{};

        e.init(code);
        context.emplace(e.getASTContext());
        rememberExprFactory(context.value());
        symbolic::ExprFactoryScope exprScope(context->getExprFactory());
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
        auto *fcp = dynamic_cast<const PathInsensitiveLoopInvPlugin *>(pl);

        return fcp->generate(*preState, *loopEntry, loopInfo);
    }

    std::optional<spec_generator::PathSensitiveLoopInvPlugin::GenResultType> doPSPluginOnFirstLoop(
        const string &code,
        const string &pid) {
        static ASTExtractor e;
        static optional<ACSLGContext> context{};

        e.init(code);
        context.emplace(e.getASTContext());
        rememberExprFactory(context.value());
        symbolic::ExprFactoryScope exprScope(context->getExprFactory());
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
        auto *fcp = dynamic_cast<const PathSensitiveLoopInvPlugin *>(pl);

        return fcp->tryGenerate(*preState, *loopEntry, loopInfo);
    }

    FixtureWithCode::FixtureWithCode()
        : e(code), defaultPoint(symbolic::SourcePoint::fromFuncDecl(e.findFirstDecl<FunctionDecl>(),
                                                                    e.getSourceManager(),
                                                                    e.getLangOptions())),
          exprScope_(exprFactory_) {
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
        return exprFactory_.variableAddress(getVarDecl(id)).cast<symbolic::VariableAddress>();
    }

    not_null<unique_ptr<symbolic::SymbolicExpr>> FixtureWithCode::makeLiteralExpr(uint64_t value) {
        return exprFactory_.cloneExpr(exprFactory_.literal(value));
    }

    not_null<unique_ptr<symbolic::SymbolicExpr>> FixtureWithCode::makeRangeIndexExpr(
        string_view name) {
        return exprFactory_.cloneExpr(exprFactory_.rangeIndex(name));
    }

    not_null<unique_ptr<symbolic::SymbolicExpr>> FixtureWithCode::cloneExpr(
        const symbolic::SymbolicExpr &expr) {
        return exprFactory_.cloneExpr(exprFactory_.importExpr(expr));
    }

    symbolic::SymbolAddress FixtureWithCode::makeRangeAddr(
        unsigned int id,
        unique_ptr<const symbolic::SymbolicExpr> offset,
        unique_ptr<const symbolic::SymbolicExpr> len,
        optional<symbolic::SourcePoint> fromPoint) {
        auto baseHandle = exprFactory_.variableAddress(getVarDecl(id));
        optional<symbolic::ExprHandle> offsetHandle;
        if (offset != nullptr)
            offsetHandle = exprFactory_.importExpr(*offset);

        optional<symbolic::ExprHandle> lenHandle;
        if (len != nullptr)
            lenHandle = exprFactory_.importExpr(*len);

        return exprFactory_
            .symbolAddress(QualType{}, baseHandle, fromPoint.value_or(defaultPoint), offsetHandle,
                           lenHandle)
            .cast<symbolic::SymbolAddress>();
    }

    unique_ptr<symbolic::SymbolValue> FixtureWithCode::makeSymbolValue(
        unsigned int id,
        optional<symbolic::SourcePoint> fromPoint) {
        auto value = exprFactory_.symbolValue(
            symbolic::SymbolicExpr::Type{symbolic::SymbolicExpr::ScalarKind::UInt, id},
            exprFactory_.variableAddress(getVarDecl(id)), fromPoint.value_or(defaultPoint));
        return std::make_unique<symbolic::SymbolValue>(value.cast<symbolic::SymbolValue>());
    }

    symbolic::SymbolAddress FixtureWithCode::makeSimpleSymbolAddr(
        unsigned int id,
        optional<symbolic::SourcePoint> fromPoint) {
        return exprFactory_
            .symbolAddress(QualType{}, exprFactory_.variableAddress(getVarDecl(id)),
                           fromPoint.value_or(defaultPoint))
            .cast<symbolic::SymbolAddress>();
    }

    symbolic::SymbolAddress FixtureWithCode::makePointAddr(unsigned int id, uint64_t off) {
        return makeRangeAddr(id, makeLiteralExpr(off).into_underlying(), nullptr);
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
                   << "mismatch at off=" << off << "\n  got:      " << *got.value()
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
