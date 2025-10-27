// tests/integration/integration_test.cpp

#include "gmock/gmock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string>
#include <llvm/Support/Casting.h>
#include "Analyzer/state.h"
#include "testHelper.h"

using namespace std;

using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::HasSubstr;
using ::testing::StartsWith;

namespace acslg::test::integration {
    using namespace utils;

    TEST(IntegrationTest, SyntaxNoDeath) {
        ASSERT_EXIT(
            {
                execOnFirstFunc(R"(
    void func(){
        return;
    }
    )");
                std::_Exit(0);
            },
            ::testing::ExitedWithCode(0), "");
        ASSERT_EXIT(
            {
                execOnFirstFunc(R"(
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
                execOnFirstFunc(R"(
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
                execOnFirstFunc(R"(
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
                execOnFirstFunc(R"(
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
                execOnFirstFunc(R"(
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
                execOnFirstFunc(R"(
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
                execOnFirstFunc(R"(
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
                execOnFirstFunc(R"(
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
                execOnFirstFunc(R"(
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
                execOnFirstFunc(code);
                std::_Exit(0);
            },
            ::testing::ExitedWithCode(0), "");
        auto postState = execOnFirstFunc(code);
        ASSERT_EQ(*getReturnExprOfFirstPath(*postState)->simplifiedExpr(),
                  *analyzer::symbolic::LiteralExpr{0}.simplifiedExpr());
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
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                addr.get().getACSLOfValue({.noStateLabelFunctionAt = true}), addrStr);
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                value.get()->simplifiedExpr()->getACSL({.noStateLabelFunctionAt = true}), valueStr);
            if (addrStr == "x") {
                EXPECT_EQ(valueStr, "n");
            } else if (addrStr == "y") {
                EXPECT_EQ(valueStr, "0");
            } else if (addrStr == "z") {
                EXPECT_THAT(valueStr, AllOf(AnyOf(StartsWith("10"), HasSubstr("+ 10")),
                                            AnyOf(StartsWith("-1 * n"), HasSubstr("- n"))));
            } else if (addrStr == "n") {
                EXPECT_EQ(valueStr, "n");
            } else {
                FAIL() << addrStr << ": " << valueStr;
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
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                addr.get().getACSLOfValue(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                addrStr);
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                value.get()->simplifiedExpr()->getACSL(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                valueStr);
            if (addrStr == "p") {
                EXPECT_EQ(valueStr, "p");
            } else if (addrStr == "n") {
                EXPECT_EQ(valueStr, "n");
            } else if (addrStr == "pt") {
                EXPECT_THAT(valueStr, AllOf(AnyOf(StartsWith("p"), HasSubstr("+ p")),
                                            AnyOf(StartsWith("n"), HasSubstr("+ n"))));
            } else if (addrStr == "p[0 .. n - 1]") {
                EXPECT_TRUE(value->isUnknown());
            } else {
                FAIL() << addrStr << ": " << valueStr;
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
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                addr.get().getACSLOfValue(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                addrStr);
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                value.get()->simplifiedExpr()->getACSL(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                valueStr);
            if (addrStr == "p") {
                EXPECT_EQ(valueStr, "p");
            } else if (addrStr == "n") {
                EXPECT_EQ(valueStr, "n");
            } else if (addrStr == "pt") {
                EXPECT_THAT(valueStr, AllOf(AnyOf(StartsWith("p"), HasSubstr("+ p")),
                                            AnyOf(StartsWith("1"), HasSubstr("+ 1")),
                                            AnyOf(StartsWith("n"), HasSubstr("+ n"))));
            } else if (addrStr == "p[1 .. n]") {
                EXPECT_TRUE(value->isUnknown());
            } else {
                FAIL() << addrStr << ": " << valueStr;
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
                DEBUG(doAll(code));
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
                DEBUG(doAll(code));
                std::_Exit(0);
            },
            ::testing::ExitedWithCode(0), "");
    }

    TEST(IntegrationTest, openHiTLS_3) {
        auto code = R"(
    #include <stdint.h>
    #define BN_UINT uint32_t

    #define SUB_ABC(borrow, r, a, b, c)         \
    do {                                    \
        BN_UINT macroTmpS = (a) - (b);            \
        BN_UINT macroTmpB = ((a) < (b)) ? 1 : 0;  \
        macroTmpB += (macroTmpS < (c)) ? 1 : 0;         \
        (r) = macroTmpS - (c);                    \
        borrow = macroTmpB;                       \
    } while (0)

BN_UINT BinSub(BN_UINT *r, const BN_UINT *a, const BN_UINT *b, uint32_t n) {
    BN_UINT borrow    = 0;
    uint32_t nn       = n;
    const BN_UINT *aa = a;
    const BN_UINT *bb = b;
    BN_UINT *rr       = r;

    while (nn >= 4) {
        SUB_ABC(borrow, rr[0], aa[0], bb[0], borrow);
        SUB_ABC(borrow, rr[1], aa[1], bb[1], borrow);
        SUB_ABC(borrow, rr[2], aa[2], bb[2], borrow);
        SUB_ABC(borrow, rr[3], aa[3], bb[3], borrow);

        rr += 4;
        aa += 4;
        bb += 4;
        nn -= 4;
    }

    uint32_t i = 0;

    for (; i < nn; i++) {
        SUB_ABC(borrow, rr[i], aa[i], bb[i], borrow);
    }
    return borrow;
}
    )";
        ASSERT_EXIT(
            {
                DEBUG(doAll(code));
                std::_Exit(0);
            },
            ::testing::ExitedWithCode(0), "");
    }

    TEST(IntegrationTest, openHiTLS_4) {
        auto code = R"(
    #include <stdint.h>
    #define BN_UINT uint32_t

    #define ADD_AB(carry, r, a, b)       \
    do {                             \
        BN_UINT macroTmpT = (a) + (b);     \
        (carry) = macroTmpT < (a) ? 1 : 0; \
        (r) = macroTmpT;                   \
    } while (0)


    BN_UINT BinInc(BN_UINT *r, const BN_UINT *a, uint32_t size, BN_UINT w)
{
    uint32_t i;
    BN_UINT carry = w;
    for (i = 0; i < size && carry != 0; i++) {
        ADD_AB(carry, r[i], a[i], carry);
    }
    if (r != a) {
        for (; i < size; i++) {
            r[i] = a[i];
        }
    }
    return carry;
}
    )";
        ASSERT_EXIT(
            {
                DEBUG(doAll(code));
                std::_Exit(0);
            },
            ::testing::ExitedWithCode(0), "");
    }

    TEST(ResultTest, WithStructure_1) {
        auto code = R"(
        struct A{
            int x;
            unsigned long y;
        };
        int func(struct A x, int n){
            return x.x - x.y + n - 100;
        }
    )";
        ASSERT_EXIT(
            {
                execOnFirstFunc(code);
                std::_Exit(0);
            },
            ::testing::ExitedWithCode(0), "");
        auto result = getReturnExprOfFirstPath(*execOnFirstFunc(code))->simplifiedExpr();
        ASSERT_OK_AND_GET_FIRST_TO_VAR(result->getACSL({.noStateLabelFunctionAt = true}),
                                       resultStr);
        EXPECT_THAT(resultStr, AllOf(AnyOf(StartsWith("x.x"), HasSubstr("+ x.x")),
                                     AnyOf(StartsWith("-1 * x.y"), HasSubstr("- x.y")),
                                     AnyOf(StartsWith("n"), HasSubstr("+ n")),
                                     AnyOf(StartsWith("-1 * 100"), HasSubstr("- 100"))));
    }

    TEST(ResultTest, WithStructure_2) {
        auto code = R"(
        struct A{
            int x;
            unsigned long y;
        };
        int func(struct A x, int n){
            int temp = x.x;
            x.x = -10;
            x.y = 100;
            x.x++;
            --x.y;
            x.x = temp + n;
            return x.x - x.y;
        }
    )";
        ASSERT_EXIT(
            {
                execOnFirstFunc(code);
                std::_Exit(0);
            },
            ::testing::ExitedWithCode(0), "");
        auto result = getReturnExprOfFirstPath(*execOnFirstFunc(code))->simplifiedExpr();
        ASSERT_OK_AND_GET_FIRST_TO_VAR(result->getACSL({.noStateLabelFunctionAt = true}),
                                       resultStr);
        EXPECT_THAT(resultStr, AllOf(AnyOf(StartsWith("x.x"), HasSubstr("+ x.x")),
                                     AnyOf(StartsWith("-1 * 99"), HasSubstr("- 99")),
                                     AnyOf(StartsWith("n"), HasSubstr("+ n"))));
    }

    TEST(ResultTest, WithStructure_3) {
        auto code = R"(
        struct A{
            int* x;
            unsigned long y;
        };
        int func(struct A x, int n){
            int* temp = x.x;
            x.y = 100;
            (*x.x)++;
            --x.y;
            *temp += n;
            return *x.x - x.y;
        }
    )";
        ASSERT_EXIT(
            {
                execOnFirstFunc(code);
                std::_Exit(0);
            },
            ::testing::ExitedWithCode(0), "");
        auto result = getReturnExprOfFirstPath(*execOnFirstFunc(code))->simplifiedExpr();
        ASSERT_OK_AND_GET_FIRST_TO_VAR(result->getACSL({.noStateLabelFunctionAt = true}),
                                       resultStr);
        EXPECT_THAT(resultStr, AllOf(AnyOf(StartsWith("*x.x"), HasSubstr("+ *x.x")),
                                     AnyOf(StartsWith("-1 * 98"), HasSubstr("- 98")),
                                     AnyOf(StartsWith("n"), HasSubstr("+ n"))));
    }

    TEST(ResultTest, WithStructure_4) {
        auto code = R"(
        struct A{
            int* x;
            unsigned long y;
        };
        int func(struct A a, struct A b){
            A temp = b;
            (*a.x)++;
            a.y = 42;
            (*temp.x)--;
            temp.y = 100;
            b.y += temp.y;
            return *a.x + a.y + *b.x + b.y;
        }
    )";
        ASSERT_EXIT(
            {
                execOnFirstFunc(code);
                std::_Exit(0);
            },
            ::testing::ExitedWithCode(0), "");
        auto result = getReturnExprOfFirstPath(*execOnFirstFunc(code))->simplifiedExpr();
        ASSERT_OK_AND_GET_FIRST_TO_VAR(result->getACSL({.noStateLabelFunctionAt = true}),
                                       resultStr);
        EXPECT_THAT(resultStr, AllOf(AnyOf(StartsWith("*a.x"), HasSubstr("+ *a.x")),
                                     AnyOf(StartsWith("*b.x"), HasSubstr("+ *b.x")),
                                     AnyOf(StartsWith("b.y"), HasSubstr("+ b.y")),
                                     AnyOf(StartsWith("142"), HasSubstr("+ 142"))));
    }

    TEST(StateTest, openHiTLS_BinSub_RightMergedAddress) {
        auto code      = R"(
    #include <stdint.h>
    #define BN_UINT uint32_t

    #define SUB_ABC(borrow, r, a, b, c)         \
    do {                                    \
        BN_UINT macroTmpS = (a) - (b);            \
        BN_UINT macroTmpB = ((a) < (b)) ? 1 : 0;  \
        macroTmpB += (macroTmpS < (c)) ? 1 : 0;         \
        (r) = macroTmpS - (c);                    \
        borrow = macroTmpB;                       \
    } while (0)

BN_UINT BinSub(BN_UINT *r, const BN_UINT *a, const BN_UINT *b, uint32_t n) {
    BN_UINT borrow    = 0;
    uint32_t nn       = n;
    const BN_UINT *aa = a;
    const BN_UINT *bb = b;
    BN_UINT *rr       = r;

    while (nn >= 4) {
        SUB_ABC(borrow, rr[0], aa[0], bb[0], borrow);
        SUB_ABC(borrow, rr[1], aa[1], bb[1], borrow);
        SUB_ABC(borrow, rr[2], aa[2], bb[2], borrow);
        SUB_ABC(borrow, rr[3], aa[3], bb[3], borrow);

        rr += 4;
        aa += 4;
        bb += 4;
        nn -= 4;
    }

    uint32_t i = 0;

    for (; i < nn; i++) {
        SUB_ABC(borrow, rr[i], aa[i], bb[i], borrow);
    }
    return borrow;
}
    )";
        auto postState = execOnFirstFunc(code);

        for (auto &path : postState->getPaths()) {
            string symbolAddrs;
            unsigned count{0};
            for (auto &&[addr, value] : path->getMemoryState().flat()) {
                auto symbolAddr = llvm::dyn_cast<analyzer::symbolic::SymbolAddress>(&addr.get());
                if (symbolAddr == nullptr)
                    continue;
                ASSERT_OK_AND_GET_FIRST_TO_VAR(
                    symbolAddr->getACSLOfValue({.noStateLabelFunctionAt = true}), rangeStr);
                if (rangeStr == "r[0 .. n - 1]")
                    ++count;
                symbolAddrs += rangeStr + "\n";
            }
            if (count != 1)
                FAIL() << "Symboladdrs: " << symbolAddrs;
        }
    }

} // namespace acslg::test::integration