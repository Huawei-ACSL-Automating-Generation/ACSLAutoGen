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
    unique_ptr<ProgramState> symbolicExecutionOnFirstFunc(const string_view code) {
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
        return symbolicState;
    }

    unique_ptr<SymbolicExpr> getReturnExprOfFirstPath(const ProgramState &state) {
        if (state.getPaths().empty())
            ERROR("Empty paths_!");
        auto &firstPath = state.getPaths()[0];
        if (firstPath == nullptr)
            ERROR("First path is nullptr!");
        auto &returnExpr = firstPath->getReturnExpr();
        if (returnExpr == nullopt)
            ERROR("There is no returnExpr!");
        return returnExpr.value()->clone();
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

TEST(IntegrationTest, CorrectStateWithPointerArithmetic) {
    auto code = R"(
    int func(int *pt){
        *pt = 0;
        (*pt)++;
        ++pt;
        pt -= 1;
        (*pt)--;
        return *pt;
    }
    )"s;
    ASSERT_EXIT(
        {
            symbolicExecutionOnFirstFunc(code);
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    auto postState = symbolicExecutionOnFirstFunc(code);
    ASSERT_EQ(*getReturnExprOfFirstPath(*postState)->simplifiedExpr(),
              *LiteralExpr{0}.simplifiedExpr());
}