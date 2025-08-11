// tests/integration/integration_test.cpp

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

namespace {
    // This code performs minimal safety checks, so please ensure the validity of the input.
    void symbolicExecutionOnFirstFunc(const string_view code) {
        ASTExtractor e(code);
        GlobalSM::getInstance().initialize(e.getSourceManager(), e.getLangOptions());
        auto func          = e.findFirstDecl<clang::FunctionDecl>();
        auto symbolicState = make_unique<ProgramState>(make_unique<ACSLFunction>(func));
        symbolicState->init();
        DEBUG(symbolicState->dump());
        for (clang::Stmt *stmt : func->getBody()->children()) {
            symbolicState->step(stmt);
            DEBUG(symbolicState->dump());
        }
    }
} // namespace

TEST(IntegrationTest, SyntaxNoDeath) {
    EXPECT_EXIT(
        {
            symbolicExecutionOnFirstFunc(R"(
    void func(){
        return;
    }
    )");
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    EXPECT_EXIT(
        {
            symbolicExecutionOnFirstFunc(R"(
    void func(int x){
        x++;
        int y = x + 1;
        return;
    }
    )");
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    EXPECT_NO_THROW(symbolicExecutionOnFirstFunc(R"(
    void func(int x, int *pt){
        x++;
        ++(*pt);
        int y = *pt + x;
        return;
    }
    )"));
    EXPECT_EXIT(
        {
            symbolicExecutionOnFirstFunc(R"(
    void func(int x, int *pt){
        x++;
        ++(*pt);
        int y = *pt + x;
        return;
    }
    )");
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    EXPECT_EXIT(
        {
            symbolicExecutionOnFirstFunc(R"(
    int func(int x, int n){
        for(int i = 0; i < n; i++){
            x = x - 1;
        }
        return x;
    }
    )");
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    EXPECT_EXIT(
        {
            symbolicExecutionOnFirstFunc(R"(
    struct A{
        int x;
        unsigned long y;
    };
    int func(int x){
        struct A a = {-1, 10};
        int z = a.x + x - a.y;
        return z;
    }
    )");
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");

    EXPECT_EXIT(
        {
            symbolicExecutionOnFirstFunc(R"(
    struct A{
        int x;
        unsigned long y;
    };
    int func(int x){
        struct A a = {-1, 10}, b = {0, 0};
        struct A c = b;
        b = a;
        return c.x + b.y;
    }
    )");
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    EXPECT_EXIT(
        {
            symbolicExecutionOnFirstFunc(R"(
    void func(int x, int *pt){
        x++;
        ++*(pt+1);
        int y = *pt + x;
        return;
    }
    )");
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    EXPECT_EXIT(
        {
            symbolicExecutionOnFirstFunc(R"(
    void func(int x, int *pt){
        x++;
        ++pt[2];
        int y = *pt + x;
        return;
    }
    )");
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    EXPECT_EXIT(
        {
            symbolicExecutionOnFirstFunc(R"(
    void func(int x, int *pt){
        x++;
        pt[1]++;
        --*(pt+1);
        int y = ++*(pt+1) + x;
        return;
    }
    )");
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}