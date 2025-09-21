// tests/integration/integration_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <unordered_map>
#include <string>
#include <llvm/Support/Casting.h>
#include "ASTExtractor.h"
#include "SpecGenerator/specGenerator.h"
#include "Analyzer/function.h"
#include "Analyzer/state.h"
#include "Analyzer/analysis.h"
#include "Context/context.h"

using namespace std;

using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::StartsWith;
using ::testing::StrEq;

namespace {
    ASTExtractor e;
    // This code performs minimal safety checks, so please ensure the validity of the input.
    not_null<unique_ptr<ProgramState>> symbolicExecutionOnFirstFunc(const string_view code) {
        e.init(code);
        static optional<ACSLContext> context{};
        context.emplace(e.getASTContext());
        auto func = e.findFirstDecl<clang::FunctionDecl>();
        auto symbolicState =
            make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
        symbolicState->init();
        DEBUG(symbolicState->dump());
        for (clang::Stmt *stmt : func->getBody()->children()) {
            symbolicState->step(stmt);
            DEBUG(symbolicState->dump());
        }
        return symbolicState;
    }

    void doAll(const string_view code) {
        e.init(code);
        static optional<ACSLContext> context{};
        context.emplace(e.getASTContext());
        ACSLAnalyzer analyzer(context.value());
        analyzer.analyzeFunctions();
        for (auto &str : context.value().getInsertedStrings()) {
            DEBUG(str);
        }
    }

    not_null<unique_ptr<SymbolicExpr>> getReturnExprOfFirstPath(const ProgramState &state) {
        if (state.getPaths().empty())
            ERROR("Empty paths_!");
        auto &firstPath  = state.getPaths()[0];
        auto &returnExpr = firstPath->getReturnExpr();
        if (returnExpr == nullopt)
            ERROR("There is no returnExpr!");
        return returnExpr.value()->clone();
    }

    not_null<unique_ptr<ProgramState>> getPostStateOfFirstLoop(const string_view code) {
        e.init(code);
        static optional<ACSLContext> context{};
        context.emplace(e.getASTContext());
        auto func = e.findFirstDecl<clang::FunctionDecl>();
        auto symbolicState =
            make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
        symbolicState->init();
        DEBUG(symbolicState->dump());
        for (clang::Stmt *stmt : func->getBody()->children()) {
            symbolicState->step(stmt);
            if (isa<clang::WhileStmt>(stmt) || isa<clang::ForStmt>(stmt) ||
                isa<clang::DoStmt>(stmt)) {
                DEBUG(symbolicState->dump());
                break;
            }
        }
        return symbolicState;
    }
} // namespace

TEST(IntegrationTest, SyntaxNoDeath) {
    ASSERT_EXIT(
        {
            symbolicExecutionOnFirstFunc(R"(
    void func(){
        return;
    }
    )");
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    ASSERT_EXIT(
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
    ASSERT_EXIT(
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
    ASSERT_EXIT(
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
    ASSERT_EXIT(
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
    ASSERT_EXIT(
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

    ASSERT_EXIT(
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
    ASSERT_EXIT(
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
    ASSERT_EXIT(
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
    ASSERT_EXIT(
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

TEST(IntegrationTest, CorrectPostStateOfLoop_1) {
    auto code = R"(
        void func(int n){
            int x = 0, y = n, z = 10;
            for(int i = 0; i < n; i++){
                x++;
                y--;
                z--;
            } 
        }
    )";
    ASSERT_EXIT(
        {
            getPostStateOfFirstLoop(code);
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    auto postState = getPostStateOfFirstLoop(code);
    auto &paths    = postState->getPaths();
    ASSERT_EQ(paths.size(), 1);
    for (auto &&[addr, value] : paths.at(0)->getMemoryState().flat()) {
        auto var  = addr.get().regularFormOfValue();
        auto expr = value->simplifiedExpr()->regularForm();
        ASSERT_NE(var, nullopt);
        ASSERT_NE(expr, nullopt);
        if (var.value() == "x") {
            EXPECT_EQ(expr.value(), "n");
        } else if (var.value() == "y") {
            EXPECT_EQ(expr.value(), "0");
        } else if (var.value() == "z") {
            EXPECT_THAT(expr.value(), AllOf(AnyOf(StartsWith("10"), HasSubstr("+ 10")),
                                            AnyOf(StartsWith("-1 * n"), HasSubstr("- n"))));
        } else if (var.value() == "n") {
            EXPECT_EQ(expr.value(), "n");
        } else {
            FAIL() << var.value() << ": " << expr.value();
        }
    }
}

TEST(IntegrationTest, CorrectPostStateOfLoop_2) {
    auto code = R"(
    void func(int *p, int n) {
        int *pt = p;
        for(int i = 0; i < n; ++i){
            *pt = 0;
            ++pt;
        }
    }
    )";
    ASSERT_EXIT(
        {
            getPostStateOfFirstLoop(code);
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    auto postState = getPostStateOfFirstLoop(code);
    auto &paths    = postState->getPaths();
    ASSERT_EQ(paths.size(), 1);
    for (auto &&[addr, value] : paths.at(0)->getMemoryState().flat()) {
        auto var  = addr.get().regularFormOfValue();
        auto expr = value->simplifiedExpr()->regularForm();
        ASSERT_NE(var, nullopt);
        ASSERT_NE(expr, nullopt);
        if (var.value() == "p") {
            EXPECT_EQ(expr.value(), "p");
        } else if (var.value() == "n") {
            EXPECT_EQ(expr.value(), "n");
        } else if (var.value() == "pt") {
            EXPECT_THAT(expr.value(), AllOf(AnyOf(StartsWith("p"), HasSubstr("+ p")),
                                            AnyOf(StartsWith("n"), HasSubstr("+ n"))));
        } else if (var.value() == "p[0..n - 1]") {
            EXPECT_TRUE(value->isUnknown());
        } else {
            FAIL() << var.value() << ": " << expr.value();
        }
    }
}

TEST(IntegrationTest, CorrectPostStateOfLoop_3) {
    auto code = R"(
    void func(int *p, int n) {
        int *pt = p + 1;
        for(int i = 0; i < n; ++i){
            *pt = 0;
            ++pt;
        }
    }
    )";
    ASSERT_EXIT(
        {
            getPostStateOfFirstLoop(code);
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    auto postState = getPostStateOfFirstLoop(code);
    auto &paths    = postState->getPaths();
    ASSERT_EQ(paths.size(), 1);
    for (auto &&[addr, value] : paths.at(0)->getMemoryState().flat()) {
        auto var  = addr.get().regularFormOfValue();
        auto expr = value->simplifiedExpr()->regularForm();
        ASSERT_NE(var, nullopt);
        ASSERT_NE(expr, nullopt);
        if (var.value() == "p") {
            EXPECT_EQ(expr.value(), "p");
        } else if (var.value() == "n") {
            EXPECT_EQ(expr.value(), "n");
        } else if (var.value() == "pt") {
            EXPECT_THAT(expr.value(), AllOf(AnyOf(StartsWith("(p+1)"), HasSubstr("+ (p+1)")),
                                            AnyOf(StartsWith("n"), HasSubstr("+ n"))));
        } else if (var.value() == "p[1..n]") {
            EXPECT_TRUE(value->isUnknown());
        } else {
            FAIL() << var.value() << ": " << expr.value();
        }
    }
}

TEST(IntegrationTest, openHiTLS_1) {
    auto code = R"(
    #include <stdint.h>
    #define BN_UINT uint32_t

    #define ADD_ABC(carry, r, a, b, c)      \
    do {                                \
        BN_UINT macroTmpS = (b) + (c);        \
        carry = (macroTmpS < (c)) ? 1 : 0;    \
        (r) = macroTmpS + (a);                \
        carry += ((r) < macroTmpS) ? 1 : 0;   \
    } while (0)

    BN_UINT BinAdd(BN_UINT *r, const BN_UINT *a, const BN_UINT *b, uint32_t n)
{
    BN_UINT carry = 0;
    uint32_t nn = n;
    const BN_UINT *aa = a;
    const BN_UINT *bb = b;
    BN_UINT *rr = r;
    while (nn) {
        ADD_ABC(carry, rr[0], aa[0], bb[0], carry);

        rr += 1;
        aa += 1;
        bb += 1;
        nn -= 1;
    }
    return carry;
}
    )";
    ASSERT_EXIT(
        {
            doAll(code);
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST(IntegrationTest, openHiTLS_2) {
    auto code = R"(
    #include <stdint.h>
    #define BN_UINT uint32_t

    #define ADD_ABC(carry, r, a, b, c)      \
    do {                                \
        BN_UINT macroTmpS = (b) + (c);        \
        carry = (macroTmpS < (c)) ? 1 : 0;    \
        (r) = macroTmpS + (a);                \
        carry += ((r) < macroTmpS) ? 1 : 0;   \
    } while (0)

    BN_UINT BinAdd(BN_UINT *r, const BN_UINT *a, const BN_UINT *b, uint32_t n)
{
    BN_UINT carry = 0;
    uint32_t nn = n;
    const BN_UINT *aa = a;
    const BN_UINT *bb = b;
    BN_UINT *rr = r;
    while (nn >= 4) {
        ADD_ABC(carry, rr[0], aa[0], bb[0], carry);
        ADD_ABC(carry, rr[1], aa[1], bb[1], carry);
        ADD_ABC(carry, rr[2], aa[2], bb[2], carry);
        ADD_ABC(carry, rr[3], aa[3], bb[3], carry);

        rr += 4;
        aa += 4;
        bb += 4;
        nn -= 4;
    }
    uint32_t i = 0;
    for (; i < nn; i++) {
        ADD_ABC(carry, rr[i], aa[i], bb[i], carry);
    }
    return carry;
}
    )";
    ASSERT_EXIT(
        {
            doAll(code);
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}