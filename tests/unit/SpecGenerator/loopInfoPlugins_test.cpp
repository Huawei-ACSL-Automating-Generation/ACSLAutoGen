// tests/unit/SpecGenerator/loopInfoPlugins_test.cpp

#include <gtest/gtest.h>
#include <unordered_map>
#include <string>
#include <llvm/Support/Casting.h>
#include "ASTExtractor.h"
#include "SpecGenerator/specGenerator.h"
#include "Analyzer/function.h"
#include "Analyzer/state.h"
#include "globalSM.h"

using namespace std;
using namespace llvm;

namespace {
    // This code performs minimal safety checks, so please ensure the validity of the input.
    auto doPluginOnFirstLoop(const string &code, const string &pid) {
        ASTExtractor e(code);
        GlobalSM::getInstance().initialize(e.getSourceManager(), e.getLangOptions());
        auto func     = e.findFirstDecl<clang::FunctionDecl>();
        auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func));
        clang::Stmt *loopStmt;
        preState->init();
        DEBUG(preState->dump());
        for (clang::Stmt *stmt : func->getBody()->children()) {
            if (isa<clang::WhileStmt>(stmt) || isa<clang::ForStmt>(stmt) ||
                isa<clang::DoStmt>(stmt)) {
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

        const clang::Expr *cond = nullptr;
        const clang::Stmt *inc  = nullptr;
        const clang::Stmt *body = nullptr;

        if (const auto *forStmt = dyn_cast<clang::ForStmt>(loopStmt)) {
            cond = forStmt->getCond();
            inc  = forStmt->getInc();
            body = forStmt->getBody();
        } else if (const auto *whileStmt = dyn_cast<clang::WhileStmt>(loopStmt)) {
            cond = whileStmt->getCond();
            body = whileStmt->getBody();
        } else {
            UNIMPLEMENT("Loop type not supported yet: " << loopStmt->getStmtClassName());
        }

        LoopInfo loopInfo;

        if (auto *pl = ACSLPluginRegistry::instance().get("setLoopEntry")) {
            auto *setLoopEntryPlugin = dynamic_cast<const LoopInfoPlugin *>(pl);

            if (!setLoopEntryPlugin->parse(*preState, *loopEntry, cond, inc, body, loopInfo))
                ERROR("Set loop entry fail.");
        } else {
            ERROR("Set loop entry fail.");
        }

        auto *pl = ACSLPluginRegistry::instance().get(pid);
        if (!pl)
            ERROR("Plugin with id " + pid + " does not exist!");
        auto *fcp   = dynamic_cast<const LoopInfoPlugin *>(pl);
        auto result = fcp->parse(*preState, *loopEntry, cond, inc, body, loopInfo);
        return pair{std::move(loopInfo), result};
    }
} // namespace

TEST(SetPatternsPluginTest, SimpleLoop_1) {
    auto pluginId                 = "setPatterns";
    auto code                     = R"(
        int func(int x, int n){
            for(int i = 0; i < n; i++){
                x++;
            } 
            return x;
        }
    )";
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_EQ(continueFlag, true);
    EXPECT_EQ(loopInfo.patternsMap_.size(), 2);
    for (auto &[addr, pattern] : loopInfo.patternsMap_) {
        DEBUG(addr.dump());
        if (pattern != nullopt) {
            DEBUG(pattern.value().initialValue_->dump() +
                  ", step: " + to_string(pattern.value().step_));
            EXPECT_EQ(pattern.value().step_, 1);
        } else {
            DEBUG("too complex");
            FAIL();
        }
    }
}

TEST(SetPatternsPluginTest, SimpleLoop_2) {
    auto pluginId                 = "setPatterns";
    auto code                     = R"(
        int* func(int *x, int n){
            for(int i = 0; i < n; i++){
                x++;
            } 
            return x;
        }
    )";
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_EQ(continueFlag, true);
    EXPECT_EQ(loopInfo.patternsMap_.size(), 2);
    for (auto &[addr, pattern] : loopInfo.patternsMap_) {
        DEBUG(addr.dump());
        if (pattern != nullopt) {
            DEBUG(pattern.value().initialValue_->dump() +
                  ", step: " + to_string(pattern.value().step_));
            EXPECT_EQ(pattern.value().step_, 1);
        } else {
            DEBUG("too complex");
            FAIL();
        }
    }
}

TEST(SetPatternsPluginTest, SimpleLoop_3) {
    auto pluginId                 = "setPatterns";
    auto code                     = R"(
        int* func(int *x, int n){
            for(int i = 0; i < n; i++){
                x++;
                *x = 1;
            } 
            return x;
        }
    )";
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_EQ(continueFlag, true);
    EXPECT_EQ(loopInfo.patternsMap_.size(), 3);
    for (auto &[addr, pattern] : loopInfo.patternsMap_) {
        DEBUG(addr.dump());
        if (pattern != nullopt) {
            DEBUG(pattern.value().initialValue_->dump() +
                  ", step: " + to_string(pattern.value().step_));
            EXPECT_EQ(pattern.value().step_, 1);
        } else {
            DEBUG("too complex");
            EXPECT_EQ(addr.isOffseted(), true);
        }
    }
}

TEST(SetPatternsPluginTest, SimpleLoop_4) {
    auto pluginId                 = "setPatterns";
    auto code                     = R"(
        int* func(int *x, int n){
            for(int i = 0; i < n; i++){
                x[i]--;
            } 
            return x;
        }
    )";
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_EQ(continueFlag, true);
    EXPECT_EQ(loopInfo.patternsMap_.size(), 2);
    for (auto &[addr, pattern] : loopInfo.patternsMap_) {
        DEBUG(addr.dump());
        DEBUG(pattern.value().initialValue_->dump() +
              ", step: " + to_string(pattern.value().step_));
        if (addr.isOffseted())
            EXPECT_EQ(pattern.value().step_, -1);
        else
            EXPECT_EQ(pattern.value().step_, 1);
    }
}