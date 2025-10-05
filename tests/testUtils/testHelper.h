// tests/testUtils/testHelper.h

#ifndef TEST_HELPER_H
#define TEST_HELPER_H

#include "ASTExtractor.h"
#include "SpecGenerator/specGenerator.h"
#include "Analyzer/function.h"
#include "Analyzer/state.h"
#include "Analyzer/analysis.h"

using namespace std;
using namespace clang;

// These codes performs minimal safety checks, so please ensure the validity of the input.

inline auto doPluginOnFirstFunc(const string &code, const string &pid) {
    static ASTExtractor e;
    static optional<ACSLContext> context{};

    e.init(code);
    context.emplace(e.getASTContext());
    auto func     = e.findFirstDecl<clang::FunctionDecl>();
    auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
    preState->init();
    DEBUG(preState->dump());
    auto postState = preState->clone();
    for (clang::Stmt *stmt : func->getBody()->children()) {
        postState->step(stmt);
        DEBUG(postState->dump());
    }

    auto *pl = ACSLPluginRegistry::instance().get(pid);
    if (!pl)
        ERROR("Plugin with id " + pid + " does not exist!");
    auto *fcp = dynamic_cast<const FunctionContractPlugin *>(pl);

    auto spec = fcp->generate(*preState, *postState);
    if (spec)
        DEBUG(*spec);
    return spec;
}

inline void doAll(const string_view code) {
    static ASTExtractor e;
    static optional<ACSLContext> context{};

    e.init(code);
    context.emplace(e.getASTContext());
    ACSLAnalyzer analyzer(context.value());
    analyzer.analyzeFunctions();
    for (auto &str : context.value().getInsertedStrings()) {
        DEBUG(str);
    }
}

inline auto execOnFirstFunc(const string &code) {
    static ASTExtractor e;
    static optional<ACSLContext> context{};

    e.init(code);
    context.emplace(e.getASTContext());
    auto func     = e.findFirstDecl<clang::FunctionDecl>();
    auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
    preState->init();
    DEBUG(preState->dump());
    auto postState = preState->clone();
    for (clang::Stmt *stmt : func->getBody()->children()) {
        postState->step(stmt);
        DEBUG(postState->dump());
    }

    return postState;
}

inline not_null<unique_ptr<SymbolicExpr>> getReturnExprOfFirstPath(const ProgramState &state) {
    if (state.getPaths().empty())
        ERROR("Empty paths_!");
    auto &firstPath  = state.getPaths()[0];
    auto &returnExpr = firstPath->getReturnExpr();
    if (returnExpr == nullopt)
        ERROR("There is no returnExpr!");
    return returnExpr.value()->clone();
}

inline not_null<unique_ptr<ProgramState>> getPostStateOfFirstLoop(const string_view code) {
    static ASTExtractor e;
    static optional<ACSLContext> context{};

    e.init(code);
    context.emplace(e.getASTContext());
    auto func = e.findFirstDecl<clang::FunctionDecl>();
    auto symbolicState =
        make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
    symbolicState->init();
    DEBUG(symbolicState->dump());
    for (clang::Stmt *stmt : func->getBody()->children()) {
        symbolicState->step(stmt);
        if (isa<clang::WhileStmt>(stmt) || isa<clang::ForStmt>(stmt) || isa<clang::DoStmt>(stmt)) {
            DEBUG(symbolicState->dump());
            break;
        }
    }
    return symbolicState;
}

inline auto doPluginsOnFirstLoop(string_view code, const vector<string> pids) {
    static ASTExtractor e;
    static optional<ACSLContext> context{};

    e.init(code);
    context.emplace(e.getASTContext());
    auto func     = e.findFirstDecl<clang::FunctionDecl>();
    auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
    clang::Stmt *loopStmt;
    preState->init();
    DEBUG(preState->dump());
    for (clang::Stmt *stmt : func->getBody()->children()) {
        if (isa<clang::WhileStmt>(stmt) || isa<clang::ForStmt>(stmt) || isa<clang::DoStmt>(stmt)) {
            loopStmt = stmt;
            break;
        }
        preState->step(stmt);
        DEBUG(preState->dump());
    }

    auto loopEntry = preState->clone();
    if (auto forLoop = dyn_cast<clang::ForStmt>(loopStmt); forLoop && forLoop->getInit()) {
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

inline std::tuple<std::optional<std::string>, bool, vector<PostInfo>> doPluginOnFirstLoop(
    const string &code,
    const string &pid) {
    ASTExtractor e;
    static optional<ACSLContext> context{};

    e.init(code);
    context.emplace(e.getASTContext());
    auto func     = e.findFirstDecl<clang::FunctionDecl>();
    auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
    clang::Stmt *loopStmt;
    preState->init();
    DEBUG(preState->dump());
    for (clang::Stmt *stmt : func->getBody()->children()) {
        if (isa<clang::WhileStmt>(stmt) || isa<clang::ForStmt>(stmt) || isa<clang::DoStmt>(stmt)) {
            loopStmt = stmt;
            break;
        }
        preState->step(stmt);
        DEBUG(preState->dump());
    }

    auto loopEntry = preState->clone();
    if (auto forLoop = dyn_cast<clang::ForStmt>(loopStmt); forLoop && forLoop->getInit()) {
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

#endif