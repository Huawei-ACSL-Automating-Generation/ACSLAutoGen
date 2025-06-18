// tests/SpecGenerator/loopInvariantPlugins_test.cpp

#include <gtest/gtest.h>
#include <unordered_map>
#include <string>
#include <llvm/Support/Casting.h>
#include "ASTExtractor.h"
#include "SpecGenerator/specGenerator.h"
#include "Analyzer/function.h"
#include "Analyzer/state.h"

using namespace std;
using namespace llvm;

namespace {
    // This code performs minimal safety checks, so please ensure the validity of the input.
    tuple<optional<string>, bool> doPluginOnFirstLoop(const string &code, const string &pid) {
        ASTExtractor e(code);
        auto func      = e.findFirstDecl<clang::FunctionDecl>();
        auto loopEntry = make_unique<ProgramState>(make_unique<ACSLFunction>(func));
        clang::Stmt *loopStmt;
        loopEntry->init();
        for (clang::Stmt *stmt : func->getBody()->children()) {
            if (isa<clang::WhileStmt>(stmt) || isa<clang::ForStmt>(stmt) ||
                isa<clang::DoStmt>(stmt)) {
                loopStmt = stmt;
                break;
            }
            loopEntry->step(stmt);
        }

        if (auto forLoop = dyn_cast<clang::ForStmt>(loopStmt); forLoop && forLoop->getInit()) {
            loopEntry->step(forLoop->getInit());
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

        auto loopInfo = parseLoopInfo(*loopEntry, cond, inc, body);
        if (!loopInfo) {
            // TODO(complex loop)
            UNIMPLEMENT("Loop is too complex!");
        }

        auto *pl = ACSLPluginRegistry::instance().get(pid);
        if (!pl)
            ERROR("Plugin with id " + pid + " does not exist!");
        auto *fcp = dynamic_cast<const LoopInvariantPlugin *>(pl);

        return fcp->generate(*loopEntry, cond, inc, body, *loopInfo);
    }
} // namespace

TEST(ParadigmMaxMinPluginTest, simple_0) {
    auto pluginId             = "paradigmMaxMin";
    auto code                 = R"(
        void func(int *p, int n){
            int mx = 0;
            for(int i = 0; i < n; i++){
                if(mx < p[i])
                    mx = p[i];
            } 
        }
    )";
    auto [spec, continueFlag] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}

TEST(ParadigmMaxMinPluginTest, simple_1) {
    auto pluginId             = "paradigmMaxMin";
    auto code                 = R"(
        void func(int *p, int n){
            int ms = 0;
            for(int i = 0; i < n; i++){
                if(ms > p[i])
                    ms = p[i];
            } 
        }
    )";
    auto [spec, continueFlag] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}

TEST(ParadigmMaxMinPluginTest, simple_2) {
    auto pluginId             = "paradigmMaxMin";
    auto code                 = R"(
        void func(int *p, int n){
            int mx = 0;
            for(int i = 0; i < n; i++){
                if(p[i] >= mx)
                    mx = p[i];
            } 
        }
    )";
    auto [spec, continueFlag] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}

TEST(ParadigmMaxMinPluginTest, simple_3) {
    auto pluginId             = "paradigmMaxMin";
    auto code                 = R"(
        void func(int *p, int n){
            int mx = 0;
            int cnt = 0;
            for(int i = 0; i < n; i++){
                if(p[i] >= mx) {
                    if(p[i] > mx)
                        count = 1;
                    else
                        count++;
                    mx = p[i];
                }   
            } 
        }
    )";
    auto [spec, continueFlag] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}

TEST(ParadigmMaxMinPluginTest, simple_4) {
    auto pluginId             = "paradigmMaxMin";
    auto code                 = R"(
        void func(int *p, int n){
            int mx = 0;
            int i = 0;
            while(i < n){
                if(mx <= p[i])
                    mx = p[i];
                i++;
            }
        }
    )";
    auto [spec, continueFlag] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}

TEST(ParadigmMaxMinPluginTest, simple_5) {
    auto pluginId             = "paradigmMaxMin";
    auto code                 = R"(
        void func(int *p, int n){
            int bound = 100;
            int count = 0;
            int i = 0;
            while(i < n){
                if(p[i] >= bound)
                    count++;
                i++;
            }
        }
    )";
    auto [spec, continueFlag] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_EQ(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}

TEST(ParadigmMaxMinPluginTest, simple_6) {
    auto pluginId             = "paradigmMaxMin";
    auto code                 = R"(
        void func(int *p, int n){
            int mx = 0;
            int i = 0;
            int count = 0;
            while(i < n){
                if(mx <= p[i])
                    mx = p[i];
                else
                    count++;
                i++;
            }
        }
    )";
    auto [spec, continueFlag] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}