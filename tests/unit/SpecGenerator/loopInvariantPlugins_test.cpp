// tests/unit/SpecGenerator/loopInvariantPlugins_test.cpp

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string>
#include <llvm/Support/Casting.h>
#include "testHelper.h"

using namespace std;
using namespace llvm;

using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::ContainsRegex;
using ::testing::HasSubstr;
using ::testing::StartsWith;

namespace acslg::test::unit::spec_generator {
    using namespace acslg::spec_generator;
    using namespace acslg::analyzer::symbolic;
    using namespace utils;

    using ::testing::HasSubstr;

    TEST(LoopAssignsPluginTest, Simple_0) {
        auto pluginId                                      = "loopAssigns";
        auto code                                          = R"(
        void func(int *p, int n){
            int mx = 0;
            for(int i = 0; i < n; i++){
                if(mx < p[i])
                    mx = p[i];
            } 
        }
    )";
        auto [spec, _, normalPostInfo, interruptPostInfos] = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_NE(spec, nullopt);
        EXPECT_THAT(*spec, HasSubstr("mx"));
        ASSERT_EQ(normalPostInfo.memoryMap.size(), 1);
        EXPECT_TRUE(normalPostInfo.memoryMap.begin()->second->isUnknown());
        ASSERT_TRUE(interruptPostInfos.empty());
    }

    TEST(LoopAssignsPluginTest, Simple_1) {
        auto pluginId                                      = "loopAssigns";
        auto code                                          = R"(
        void func(int *p, int n){
            int cnt = 0;
            for(int i = 0; i < n; i++){
                p[i]++;
                cnt -= 1;
            } 
        }
    )";
        auto [spec, _, normalPostInfo, interruptPostInfos] = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_NE(spec, nullopt);
        EXPECT_THAT(*spec, HasSubstr("cnt"));
        // EXPECT_THAT(*spec, ContainsRegex(R"(\\at\(p, [^)]+\)\[0 \.\. \\at\(n, [^)]+\) - 1\])"));
        EXPECT_EQ(normalPostInfo.memoryMap.size(), 2);
        ASSERT_TRUE(interruptPostInfos.empty());
        for (auto &[addr, value] : normalPostInfo.memoryMap) {
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                addr.get().getACSLOfValue(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                addrStr);
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                value.get()->simplifiedExpr()->getACSL(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                valueStr);
            if (addrStr == "p[i .. n - 1]") { // Too lazy to write the matching logic.
                EXPECT_TRUE(value->isUnknown()) << valueStr;
            } else if (addrStr == "cnt") {
                EXPECT_THAT(valueStr, AllOf(AnyOf(StartsWith("cnt"), HasSubstr("+ cnt")),
                                            AnyOf(StartsWith("-1 * n"), HasSubstr("- n")),
                                            AnyOf(StartsWith("i"), HasSubstr("+ i"))));
            } else {
                FAIL() << addrStr << valueStr;
            }
        }
    }

    TEST(LoopAssignsPluginTest, Simple_2) {
        auto pluginId                                      = "loopAssigns";
        auto code                                          = R"(
        void func(int *p, int n){
            int cnt = 0;
            int i = 0;
            while(i < n){
                p[i]++;
                cnt -= 1;
                i++;
            } 
        }
    )";
        auto [spec, _, normalPostInfo, interruptPostInfos] = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_NE(spec, nullopt);
        EXPECT_THAT(*spec, HasSubstr("i"));
        EXPECT_THAT(*spec, HasSubstr("cnt"));
        // EXPECT_THAT(*spec, ContainsRegex(R"(\\at\(p, [^)]+\)\[0 \.\. \\at\(n, [^)]+\) - 1\])"));
        EXPECT_EQ(normalPostInfo.memoryMap.size(), 3);
        ASSERT_TRUE(interruptPostInfos.empty());
        for (auto &[addr, value] : normalPostInfo.memoryMap) {
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                addr.get().getACSLOfValue(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                addrStr);
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                value.get()->simplifiedExpr()->getACSL(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                valueStr);
            if (addrStr == "p[i .. n - 1]") {
                EXPECT_TRUE(value->isUnknown()) << valueStr;
            } else if (addrStr == "cnt") {
                EXPECT_THAT(valueStr, AllOf(AnyOf(StartsWith("cnt"), HasSubstr("+ cnt")),
                                            AnyOf(StartsWith("-1 * n"), HasSubstr("- n")),
                                            AnyOf(StartsWith("i"), HasSubstr("+ i"))));
            } else if (addrStr == "i") {
                EXPECT_EQ(valueStr, "n");
            } else {
                FAIL() << addrStr << valueStr;
            }
        }
    }

    TEST(LoopAssignsPluginTest, Simple_3) {
        auto pluginId                                      = "loopAssigns";
        auto code                                          = R"(
        void func(int *p, int n){
            int cnt = 0;
            int i = 0;
            while(i < n){
                *p += 1;
                cnt -= 1;
                i++;
                p++;
            } 
        }
    )";
        auto [spec, _, normalPostInfo, interruptPostInfos] = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_NE(spec, nullopt);
        EXPECT_THAT(*spec, HasSubstr("i"));
        EXPECT_THAT(*spec, HasSubstr("cnt"));
        // EXPECT_THAT(*spec, ContainsRegex(R"(\\at\(p, [^)]+\)\[0 \.\. \\at\(n, [^)]+\) - 1\])"));
        EXPECT_EQ(normalPostInfo.memoryMap.size(), 4);
        ASSERT_TRUE(interruptPostInfos.empty());
        for (auto &[addr, value] : normalPostInfo.memoryMap) {
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                addr.get().getACSLOfValue(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                addrStr);
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                value.get()->simplifiedExpr()->getACSL(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                valueStr);
            if (addrStr == "p") {
                EXPECT_THAT(valueStr, AllOf(AnyOf(StartsWith("p"), HasSubstr("+ p")),
                                            AnyOf(StartsWith("n"), HasSubstr("+ n"))));
            } else if (addrStr ==
                       "p[0 .. -1 * i + n - 1]") { // Too lazy to write the matching logic.
                EXPECT_TRUE(value->isUnknown()) << valueStr;
            } else if (addrStr == "cnt") {
                EXPECT_THAT(valueStr, AllOf(AnyOf(StartsWith("cnt"), HasSubstr("+ cnt")),
                                            AnyOf(StartsWith("-1 * n"), HasSubstr("- n")),
                                            AnyOf(StartsWith("i"), HasSubstr("+ i"))));
            } else if (addrStr == "i") {
                EXPECT_EQ(valueStr, "n");
            } else {
                FAIL() << addrStr << valueStr;
            }
        }
    }

    TEST(LoopAssignsPluginTest, openHiTLS_1) {
        auto pluginId                                      = "loopAssigns";
        auto code                                          = R"(
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
        auto [spec, _, normalPostInfo, interruptPostInfos] = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_NE(spec, nullopt);
        EXPECT_THAT(*spec, HasSubstr("aa"));
        EXPECT_THAT(*spec, HasSubstr("bb"));
        EXPECT_THAT(*spec, HasSubstr("rr"));
        EXPECT_THAT(*spec, HasSubstr("nn"));
        // EXPECT_THAT(*spec, ContainsRegex(R"(\\at\(r, [^)]+\)\[0 \.\. \\at\(n, [^)]+\) - 1\])"));

        EXPECT_EQ(normalPostInfo.memoryMap.size(), 6);
        ASSERT_TRUE(interruptPostInfos.empty());
        for (auto &[addr, value] : normalPostInfo.memoryMap) {
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                addr.get().getACSLOfValue(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                addrStr);
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                value.get()->simplifiedExpr()->getACSL(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                valueStr);
            if (addrStr == "aa") {
                EXPECT_THAT(valueStr, AllOf(AnyOf(StartsWith("a"), HasSubstr("+ a")),
                                            AnyOf(StartsWith("n"), HasSubstr("+ n"))));
            } else if (addrStr == "bb") {
                EXPECT_THAT(valueStr, AllOf(AnyOf(StartsWith("b"), HasSubstr("+ b")),
                                            AnyOf(StartsWith("n"), HasSubstr("+ n"))));
            } else if (addrStr == "rr") {
                EXPECT_THAT(valueStr, AllOf(AnyOf(StartsWith("r"), HasSubstr("+ r")),
                                            AnyOf(StartsWith("n"), HasSubstr("+ n"))));
            } else if (addrStr == "nn") {
                EXPECT_EQ(valueStr, "0");
            } else if (addrStr == "rr[0 .. nn - 1]") {
                EXPECT_TRUE(value->isUnknown());
            } else if (addrStr == "carry") {
                EXPECT_TRUE(value->isUnknown());
            } else {
                FAIL() << addrStr << valueStr;
            }
        }
    }

    TEST(ComplexLoopAssignsPluginTest, SimpleScalar) {
        auto pluginId                                      = "complexLoopAssigns";
        auto code                                          = R"(
        void func(int *p, int n){
            int mx = 0;
            for(int i = 0; i < n; i++){
                if(mx < p[i])
                    mx = p[i];
            } 
        }
    )";
        auto [spec, _, normalPostInfo, interruptPostInfos] = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_NE(spec, nullopt);
        EXPECT_THAT(*spec, HasSubstr("mx"));
        ASSERT_EQ(normalPostInfo.memoryMap.size(), 1);
        ASSERT_TRUE(interruptPostInfos.empty());
        EXPECT_TRUE(normalPostInfo.memoryMap.begin()->second->isUnknown());
    }

    // TEST(ComplexLoopAssignsPluginTest, ArrayAndScalar) {
    //     auto pluginId            = "complexLoopAssigns";
    //     auto code                = R"(
    //     void func(int *p, int n){
    //         int cnt = 0;
    //         for(int i = 0; i < n; i++){
    //             p[i]++;
    //             cnt -= 1;
    //         }
    //     }
    // )";
    //     auto [spec, _, postInfo] = doPIPluginOnFirstLoop(code, pluginId);
    //     EXPECT_NE(spec, nullopt);
    //     EXPECT_THAT(*spec, HasSubstr("cnt"));
    //     EXPECT_THAT(*spec, ContainsRegex(R"(\\at\(p, [^)]+\)\[0 \.\. \\at\(n, [^)]+\) - 1\])"));
    //     EXPECT_EQ(postInfo.memoryMap.size(), 2);
    //     for (auto &[addr, value] : postInfo.memoryMap) {
    //         ASSERT_OK_AND_GET_FIRST_TO_VAR(
    //             addr.get().getACSLOfValue(
    //                 {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
    //             addrStr);
    //         if (addrStr == "p[i .. n - 1]") {
    //             EXPECT_TRUE(value->isUnknown());
    //         } else if (addrStr == "cnt") {
    //             EXPECT_TRUE(value->isUnknown());
    //         } else {
    //             FAIL() << addrStr;
    //         }
    //     }
    // }

    TEST(ComplexLoopAssignsPluginTest, InactivePathWrites) {
        auto pluginId                                      = "complexLoopAssigns";
        auto code                                          = R"(
        int find_first_zero(int *p, int n){
            int found = -1;
            int i = 0;
            while(i < n){
                if(p[i] == 0){
                    found = i;
                    break;
                }
                ++i;
            }
            return found;
        }
    )";
        auto [spec, _, normalPostInfo, interruptPostInfos] = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_NE(spec, nullopt);
        EXPECT_THAT(*spec, HasSubstr("i"));
        ASSERT_EQ(normalPostInfo.memoryMap.size(), 1);
        EXPECT_TRUE(normalPostInfo.memoryMap.size() == 1);
        for (auto &[addr, value] : normalPostInfo.memoryMap) {
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                addr.get().getACSLOfValue(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                addrStr);
            if (addrStr == "i") {
                EXPECT_TRUE(value->isUnknown());
            } else {
                FAIL() << addrStr;
            }
        }
        ASSERT_EQ(interruptPostInfos.size(), 1);
        EXPECT_TRUE(interruptPostInfos.front().memoryMap.size() == 2);
        for (auto &[addr, value] : interruptPostInfos.front().memoryMap) {
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                addr.get().getACSLOfValue(
                    {.noStateLabelFunctionAt = true, .UnknownExprAsError = false}),
                addrStr);
            if (addrStr == "i") {
                EXPECT_TRUE(value->isUnknown());
            } else if (addrStr == "found") {
                EXPECT_TRUE(value->isUnknown());
            } else {
                FAIL() << addrStr;
            }
        }
    }

    TEST(ParadigmMaxMinPluginTest, Simple_0) {
        auto pluginId = "paradigmMaxMin";
        auto code     = R"(
        void func(int *p, int n){
            int mx = 0;
            for(int i = 0; i < n; i++){
                if(mx < p[i])
                    mx = p[i];
            } 
        }
    )";
        auto res      = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_NE(res.acsl, nullopt);
    }

    TEST(ParadigmMaxMinPluginTest, Simple_1) {
        auto pluginId = "paradigmMaxMin";
        auto code     = R"(
        void func(int *p, int n){
            int ms = 0;
            for(int i = 0; i < n; i++){
                if(ms > p[i])
                    ms = p[i];
            } 
        }
    )";
        auto res      = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_NE(res.acsl, nullopt);
    }

    TEST(ParadigmMaxMinPluginTest, Simple_2) {
        auto pluginId = "paradigmMaxMin";
        auto code     = R"(
        void func(int *p, int n){
            int mx = 0;
            for(int i = 0; i < n; i++){
                if(p[i] >= mx)
                    mx = p[i];
            } 
        }
    )";
        auto res      = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_NE(res.acsl, nullopt);
    }

    TEST(ParadigmMaxMinPluginTest, Simple_3) {
        auto pluginId = "paradigmMaxMin";
        auto code     = R"(
        void func(int *p, int n){
            int mx = 0;
            int cnt = 0;
            for(int i = 0; i < n; i++){
                if(p[i] >= mx) {
                    if(p[i] > mx)
                        cnt = 1;
                    else
                        cnt++;
                    mx = p[i];
                }   
            } 
        }
    )";
        auto res      = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_NE(res.acsl, nullopt);
    }

    TEST(ParadigmMaxMinPluginTest, Simple_4) {
        auto pluginId = "paradigmMaxMin";
        auto code     = R"(
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
        auto res      = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_NE(res.acsl, nullopt);
    }

    TEST(ParadigmMaxMinPluginTest, Simple_5) {
        auto pluginId = "paradigmMaxMin";
        auto code     = R"(
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
        auto res      = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_EQ(res.acsl, nullopt);
    }

    TEST(ParadigmMaxMinPluginTest, Simple_6) {
        auto pluginId = "paradigmMaxMin";
        auto code     = R"(
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
        auto res      = doPIPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        EXPECT_NE(res.acsl, nullopt);
    }

    TEST(LinearInvariantPluginTest, Simple_1) {
        auto pluginId = "StInGXPlugin";
        auto code     = R"(
        void func(int n){
            int x = 0, y = n, z = 10;
            for(int i = 0; i < n; i++){
                x++;
                y--;
                z--;
            } 
        }
    )";
        auto res      = doPSPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        ASSERT_TRUE(res);
        auto &[spec, _, normalPostInfos, interruptPostInfos] = res.value();
        EXPECT_NE(spec, nullopt);

        ASSERT_EQ(normalPostInfos.size(), 1);
        ASSERT_TRUE(interruptPostInfos.empty());
        auto &postInfo = normalPostInfos.at(0);
        for (auto &[addr, value] : postInfo.memoryMap) {
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                addr.get().getACSLOfValue({.noStateLabelFunctionAt = true}), addrStr);
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                value.get()->simplifiedExpr()->getACSL({.noStateLabelFunctionAt = true}), valueStr);
            if (addrStr == "x") {
                EXPECT_THAT(valueStr, "n");
            } else if (addrStr == "y") {
                EXPECT_THAT(valueStr, AllOf(AnyOf(StartsWith("y"), HasSubstr("+ y")),
                                            AnyOf(StartsWith("-1 * n"), HasSubstr("- n"))));
            } else if (addrStr == "z") {
                EXPECT_THAT(valueStr, AllOf(AnyOf(StartsWith("10"), HasSubstr("+ 10")),
                                            AnyOf(StartsWith("-1 * n"), HasSubstr("- n"))));
            } else if (addrStr != "i" && addrStr != "n") {
                FAIL() << addrStr;
            }
        }
    }

    TEST(LinearInvariantPluginTest, Simple_2) {
        auto pluginId = "StInGXPlugin";
        auto code     = R"(
        void func(int n){
            int x = 0, sum = 0;
            for(int i = 0; i < n; i++){
                x++;
                sum += x;
            } 
        }
    )";
        auto res      = doPSPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        ASSERT_TRUE(res);
        auto &[spec, _, normalPostInfos, interruptPostInfos] = res.value();
        EXPECT_NE(spec, nullopt);

        ASSERT_EQ(normalPostInfos.size(), 1);
        ASSERT_TRUE(interruptPostInfos.empty());
        auto &postInfo = normalPostInfos.at(0);
        for (auto &[addr, value] : postInfo.memoryMap) {
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                addr.get().getACSLOfValue({.noStateLabelFunctionAt = true}), addrStr);
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                value.get()->simplifiedExpr()->getACSL({.noStateLabelFunctionAt = true}), valueStr);
            if (addrStr == "x") {
                EXPECT_THAT(valueStr, "n");
            } else if (addrStr != "i" && addrStr != "n") {
                FAIL() << addrStr;
            }
        }
    }

    TEST(LinearInvariantPluginTest, Simple_3) {
        auto pluginId = "StInGXPlugin";
        auto code     = R"(
    int func() {
        int i, j;
        i = 1;
        j = 10;
        while (j >= -10) {
            i = i + 2;
            j = -1 + j;
        }
        return 0;
    }
    )";
        auto res      = doPSPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        ASSERT_TRUE(res);
        auto &[spec, _, normalPostInfos, interruptPostInfos] = res.value();
        EXPECT_NE(spec, nullopt);
        ASSERT_EQ(normalPostInfos.size(), 1);
        ASSERT_TRUE(interruptPostInfos.empty());
        auto &postInfo = normalPostInfos.at(0);
        for (auto &[addr, value] : postInfo.memoryMap) {
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                addr.get().getACSLOfValue({.noStateLabelFunctionAt = true}), addrStr);
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                value.get()->simplifiedExpr()->getACSL({.noStateLabelFunctionAt = true}), valueStr);
            if (addrStr == "i") {
                EXPECT_THAT(valueStr, "43");
            } else if (addrStr == "j") {
                EXPECT_THAT(valueStr, "-11");
            } else {
                FAIL() << addrStr;
            }
        }
    }

    TEST(LinearInvariantPluginTest, Simple_4) {
        auto pluginId = "StInGXPlugin";
        auto code     = R"(
    void func(int *p, int n) {
        int *pt = p;
        for(int i = 0; i < n; ++i){
            *pt = 0;
            ++pt;
        }
    }
    )";
        auto res      = doPSPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        ASSERT_TRUE(res);
        auto &[spec, _, normalPostInfos, interruptPostInfos] = res.value();
        EXPECT_NE(spec, nullopt);
        ASSERT_EQ(normalPostInfos.size(), 1);
        ASSERT_TRUE(interruptPostInfos.empty());
        auto &postInfo = normalPostInfos.at(0);
        for (auto &[addr, value] : postInfo.memoryMap) {
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                addr.get().getACSLOfValue({.noStateLabelFunctionAt = true}), addrStr);
            ASSERT_OK_AND_GET_FIRST_TO_VAR(
                value.get()->simplifiedExpr()->getACSL({.noStateLabelFunctionAt = true}), valueStr);
            if (addrStr == "pt") {
                EXPECT_THAT(valueStr, AllOf(AnyOf(StartsWith("n"), HasSubstr("+ n")),
                                            AnyOf(StartsWith("p"), HasSubstr("+ p"))));
            } else if (addrStr != "p" && addrStr != "n" && addrStr != "i") {
                FAIL() << addrStr;
            }
        }
    }

    TEST(LinearInvariantPluginTest, Simple_5) {
        auto pluginId = "StInGXPlugin";
        auto code     = R"(
    void func(int x, int y) {
        x = 0;
        y = 50;
        while(x < 100){
            x = x + 1;
            if(x > 50)
                y = y + 1;
        }
    }
    )";
        auto res      = doPSPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        ASSERT_TRUE(res);
        auto &[spec, _, normalPostInfos, interruptPostInfos] = res.value();
        EXPECT_NE(spec, nullopt);
        ASSERT_EQ(normalPostInfos.size(), 2);
        ASSERT_TRUE(interruptPostInfos.empty());
        DEBUG(spec.value());
    }

    TEST(LinearInvariantPluginTest, X509_parser_bufs_differ) {
        auto pluginId = "StInGXPlugin";
        auto code     = R"(
#include <stdint.h>

typedef uint8_t	  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

int bufs_differ(const u8 *b1, const u8 *b2, u32 n)
{
	int ret = 0;
	u32 i = 0;

	for (i = 0; i < n; i++) {
		if(b1[i] != b2[i]) {
			ret = 1;
			break;
		}
	}

	return ret;
}
    )";
        auto res      = doPSPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        ASSERT_TRUE(res);
        auto &[spec, _, normalPostInfos, interruptPostInfos] = res.value();
        EXPECT_NE(spec, nullopt);
        DEBUG(spec.value());
        DEBUG(normalPostInfos.size());
        ASSERT_FALSE(interruptPostInfos.empty());
        bool sawPostCondition = false;
        auto &factory         = getLastExprFactory();
        for (auto &postInfo : normalPostInfos) {
            DEBUG("");
            for (auto &[addr, value] : postInfo.memoryMap) {
                DEBUG(addr.get().dump());
                DEBUG(value->dump());
            }
            for (auto &pathCond : postInfo.pathConds) {
                sawPostCondition = true;
                auto canonical   = factory.importExpr(*pathCond);
                EXPECT_EQ(canonical.get(), pathCond.get());
                auto expected = pathCond->simplifiedExpr()->getACSL({});
                assert(expected);
                DEBUG(expected.value().first);
            }
        }
        EXPECT_TRUE(sawPostCondition);
    }

    // TEST(LinearInvariantPluginTest, Simple_6) {
    //     auto pluginId = "StInGXPlugin";
    //     auto code     = R"(
    // int unknown();

    // void func(int x, int y) {
    //     while(unknown()){
    //         x = x + 1;
    //         y = y + 2;
    //     }
    // }
    // )";
    //     auto res      = doPSPluginOnFirstLoop(code, pluginId);
    //     ASSERT_TRUE(res);
    //     auto &[spec, _, postInfos] = res.value();
    //     EXPECT_NE(spec, nullopt);
    //     DEBUG(spec.value());
    // }

    TEST(ParadigmSearchPluginTest, openHiTLS_1) {
        auto pluginId = "paradigmSearch";
        auto code     = R"(
    #include <stdint.h>
    #define BN_UINT uint32_t

    uint32_t BinFixSize(const BN_UINT *data, uint32_t size)
{
    uint32_t fix = size;
    uint32_t i = size;
    for (; i > 0; i--) {
        if (data[i - 1] != 0) {
            return fix;
        };
        fix--;
    }
    return fix;
}
    )";
        auto res      = doPSPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        ASSERT_TRUE(res);
        auto &[spec, _, normalPostInfos, interruptPostInfos] = res.value();
        EXPECT_NE(spec, nullopt);
        DEBUG(spec.value());
        ASSERT_EQ(normalPostInfos.size(), 1);
        ASSERT_EQ(interruptPostInfos.size(), 1);
        ASSERT_EQ(interruptPostInfos.front().size(), 1);
        auto &normalPath      = normalPostInfos.at(0);
        auto &interruptedPath = interruptPostInfos.front().at(0);
        for (auto &pathCond : normalPath.pathConds) {
            auto expected = pathCond->simplifiedExpr()->getACSL({});
            assert(expected);
            DEBUG(expected.value().first);
        }
        for (auto &pathCond : interruptedPath.pathConds) {
            auto expected = pathCond->simplifiedExpr()->getACSL({});
            assert(expected);
            DEBUG(expected.value().first);
        }
    }

    TEST(ParadigmSearchPluginTest, Simple_0) {
        auto pluginId = "paradigmSearch";
        auto code     = R"(
int arraySearch(int *a, int x, int n) {
        int p = 0;

        while (p < n) {
            if (a[p] == x) {
                return 1;
            }
            p++;
        }
        return 0;
    }
    )";
        auto res      = doPSPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        ASSERT_TRUE(res);
        auto &[spec, _, normalPostInfos, interruptPostInfos] = res.value();
        EXPECT_NE(spec, nullopt);
        DEBUG(spec.value());
        ASSERT_EQ(normalPostInfos.size(), 1);
        ASSERT_EQ(interruptPostInfos.size(), 1);
        ASSERT_EQ(interruptPostInfos.front().size(), 1);
        auto &normalPath      = normalPostInfos.at(0);
        auto &interruptedPath = interruptPostInfos.front().at(0);
        for (auto &pathCond : normalPath.pathConds) {
            auto expected = pathCond->simplifiedExpr()->getACSL({});
            assert(expected);
            DEBUG(expected.value().first);
        }
        EXPECT_TRUE(interruptedPath.pathState == analyzer::Path::PathState::Return);
        EXPECT_TRUE(interruptedPath.returnExpr);
        DEBUG(interruptedPath.returnExpr.value()->dump());
        for (auto &pathCond : interruptedPath.pathConds) {
            auto expected = pathCond->simplifiedExpr()->getACSL({});
            assert(expected);
            DEBUG(expected.value().first);
        }
    }

    TEST(ParadigmSearchPluginTest, X509_parser_bufs_differ) {
        auto pluginId = "paradigmSearch";
        auto code     = R"(
#include <stdint.h>

typedef uint8_t	  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

int bufs_differ(const u8 *b1, const u8 *b2, u32 n)
{
	int ret = 0;
	u32 i = 0;

	for (i = 0; i < n; i++) {
		if(b1[i] != b2[i]) {
			ret = 1;
			break;
		}
	}

	return ret;
}
    )";
        auto res      = doPSPluginOnFirstLoop(code, pluginId);
        ExprFactoryScope scope(getLastExprFactory());
        ASSERT_TRUE(res);
        auto &[spec, _, normalPostInfos, interruptPostInfos] = res.value();
        EXPECT_NE(spec, nullopt);
        DEBUG(spec.value());
        ASSERT_EQ(normalPostInfos.size(), 1);
        ASSERT_EQ(interruptPostInfos.size(), 1);
        ASSERT_EQ(interruptPostInfos.front().size(), 1);
        auto &normalPath      = normalPostInfos.at(0);
        auto &interruptedPath = interruptPostInfos.front().at(0);
        for (auto &pathCond : normalPath.pathConds) {
            auto expected = pathCond->simplifiedExpr()->getACSL({});
            assert(expected);
            DEBUG(expected.value().first);
        }
        EXPECT_TRUE(interruptedPath.pathState == analyzer::Path::PathState::Break);
        for (auto &pathCond : interruptedPath.pathConds) {
            auto expected = pathCond->simplifiedExpr()->getACSL({});
            assert(expected);
            DEBUG(expected.value().first);
        }
    }
} // namespace acslg::test::unit::spec_generator
