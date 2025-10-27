// tests/unit/SpecGenerator/loopInfoPlugins_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string>
#include <llvm/Support/Casting.h>
#include "SpecGenerator/specGenerator.h"
#include "expr.h"
#include "testHelper.h"

using namespace std;
using namespace llvm;

using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::HasSubstr;
using ::testing::StartsWith;

namespace acslg::test::unit::spec_generator {
    using namespace acslg::spec_generator;
    using namespace acslg::analyzer::symbolic;
    using namespace utils;

    TEST(SetPatternsPluginTest, SimpleLoop_1) {
        auto pluginIds                = vector{"setPatterns"s};
        auto code                     = R"(
        int func(int x, int n){
            for(int i = 0; i < n; i++){
                x++;
            } 
            return x;
        }
    )";
        auto [loopInfo, continueFlag] = doPluginsOnFirstLoop(code, pluginIds);
        EXPECT_EQ(continueFlag, true);
        ASSERT_NE(loopInfo.patternInfo_, nullopt);
        auto &patternInfo = loopInfo.patternInfo_.value();
        EXPECT_EQ(patternInfo.patternsMap_.size(), 2);
        for (auto &[addr, pattern] : patternInfo.patternsMap_) {
            DEBUG(addr.get().dump());
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
        auto pluginId                 = vector{"setPatterns"s};
        auto code                     = R"(
        int* func(int *x, int n){
            for(int i = 0; i < n; i++){
                x++;
            } 
            return x;
        }
    )";
        auto [loopInfo, continueFlag] = doPluginsOnFirstLoop(code, pluginId);
        EXPECT_EQ(continueFlag, true);
        ASSERT_NE(loopInfo.patternInfo_, nullopt);
        auto &patternInfo = loopInfo.patternInfo_.value();
        EXPECT_EQ(patternInfo.patternsMap_.size(), 2);
        for (auto &[addr, pattern] : patternInfo.patternsMap_) {
            DEBUG(addr.get().dump());
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
        auto pluginId                 = vector{"setPatterns"s};
        auto code                     = R"(
        int* func(int *x, int n){
            for(int i = 0; i < n; i++){
                x++;
                *x = 1;
            } 
            return x;
        }
    )";
        auto [loopInfo, continueFlag] = doPluginsOnFirstLoop(code, pluginId);
        EXPECT_EQ(continueFlag, true);
        ASSERT_NE(loopInfo.patternInfo_, nullopt);
        auto &patternInfo = loopInfo.patternInfo_.value();
        EXPECT_EQ(patternInfo.patternsMap_.size(), 3);
        for (auto &[addr, pattern] : patternInfo.patternsMap_) {
            DEBUG(addr.get().dump());
            if (pattern != nullopt) {
                DEBUG(pattern.value().initialValue_->dump() +
                      ", step: " + to_string(pattern.value().step_));
                EXPECT_EQ(pattern.value().step_, 1);
            } else {
                DEBUG("too complex");
                if (!llvm::isa<SymbolAddress>(addr.get()))
                    FAIL();
            }
        }
    }

    TEST(SetPatternsPluginTest, SimpleLoop_4) {
        auto pluginId                 = vector{"setPatterns"s};
        auto code                     = R"(
        int* func(int *x, int n){
            for(int i = 0; i < n; i++){
                x[i]--;
            } 
            return x;
        }
    )";
        auto [loopInfo, continueFlag] = doPluginsOnFirstLoop(code, pluginId);
        EXPECT_EQ(continueFlag, true);
        ASSERT_NE(loopInfo.patternInfo_, nullopt);
        auto &patternInfo = loopInfo.patternInfo_.value();
        EXPECT_EQ(patternInfo.patternsMap_.size(), 2);
        for (auto &[addr, pattern] : patternInfo.patternsMap_) {
            DEBUG(addr.get().dump());
            ASSERT_NE(pattern, nullopt);
            DEBUG(pattern.value().initialValue_->dump() +
                  ", step: " + to_string(pattern.value().step_));
            if (llvm::isa<SymbolAddress>(addr.get()))
                EXPECT_EQ(pattern.value().step_, -1);
            else
                EXPECT_EQ(pattern.value().step_, 1);
        }
    }

    TEST(SetPatternsPluginTest, openHiTLS_4) {
        auto pluginId                 = vector{"setPatterns"s};
        auto code                     = R"(
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
        auto [loopInfo, continueFlag] = doPluginsOnFirstLoop(code, pluginId);
        EXPECT_EQ(continueFlag, true);
        ASSERT_NE(loopInfo.patternInfo_, nullopt);
        auto &patternInfo = loopInfo.patternInfo_.value();
        EXPECT_EQ(patternInfo.patternsMap_.size(), 6);
        for (auto &[addr, pattern] : patternInfo.patternsMap_) {
            auto res = addr.get().getACSLOfValue({.noStateLabelFunctionAt = true});
            ASSERT_TRUE(res) << "Address {" + addr.get().dump() << "} getACSL failed.";
            auto addrStr = res.value().first;
            if (addrStr == "aa") {
                ASSERT_NE(pattern, nullopt);
                EXPECT_EQ(pattern.value().step_, 1);
            } else if (addrStr == "bb") {
                ASSERT_NE(pattern, nullopt);
                EXPECT_EQ(pattern.value().step_, 1);
            } else if (addrStr == "rr") {
                ASSERT_NE(pattern, nullopt);
                EXPECT_EQ(pattern.value().step_, 1);
            } else if (addrStr == "nn") {
                ASSERT_NE(pattern, nullopt);
                EXPECT_EQ(pattern.value().step_, -1);
            } else if (addrStr == "*rr" || addrStr == "rr[0]") {
                EXPECT_EQ(pattern, nullopt);
            } else if (addrStr == "carry") {
                EXPECT_EQ(pattern, nullopt);
            } else {
                FAIL() << addrStr << ": "
                       << (pattern == nullopt ? "nullopt" : pattern.value().dump());
            }
        }
    }

    TEST(SetIndexPluginTest, SimpleLoop_1) {
        auto pluginIds                = vector{"setPatterns"s, "setIndex"s};
        auto code                     = R"(
        int func(int x, int n){
            for(int i = 0; i < n; i++){
                x++;
            } 
            return x;
        }
    )";
        auto [loopInfo, continueFlag] = doPluginsOnFirstLoop(code, pluginIds);
        EXPECT_EQ(continueFlag, true);
        ASSERT_NE(loopInfo.indexInfo_, nullopt);
        auto &indexInfo = loopInfo.indexInfo_.value();
        ASSERT_NE(indexInfo.indexRealAddr_->getFromRoot(), nullopt);
        EXPECT_EQ(indexInfo.indexRealAddr_->getFromRoot().value()->getNameAsString(), "i");
        EXPECT_TRUE(llvm::isa<SymbolValue>(*indexInfo.indexSymbolicValue_));
        EXPECT_OK_AND_FIRST_EQ(
            indexInfo.indexSymbolicValue_->getACSL({.noStateLabelFunctionAt = true}), "i");
        EXPECT_EQ(indexInfo.op_, clang::BinaryOperatorKind::BO_LT);
        EXPECT_OK_AND_FIRST_EQ(indexInfo.indexBound_->getACSL({.noStateLabelFunctionAt = true}),
                               "n");
        EXPECT_OK_AND_FIRST_EQ(indexInfo.preciseLoopCount_->simplifiedExpr()->getACSL(
                                   {.noStateLabelFunctionAt = true}),
                               "n - i");
    }

    TEST(SetIndexPluginTest, SimpleLoop_2) {
        auto pluginIds                = vector{"setPatterns"s, "setIndex"s};
        auto code                     = R"(
        int func(int x, int n){
            for(int i = n; i; i--){
                x++;
            } 
            return x;
        }
    )";
        auto [loopInfo, continueFlag] = doPluginsOnFirstLoop(code, pluginIds);
        EXPECT_EQ(continueFlag, true);
        ASSERT_NE(loopInfo.indexInfo_, nullopt);
        auto &indexInfo = loopInfo.indexInfo_.value();
        ASSERT_NE(indexInfo.indexRealAddr_->getFromRoot(), nullopt);
        EXPECT_EQ(indexInfo.indexRealAddr_->getFromRoot().value()->getNameAsString(), "i");
        EXPECT_TRUE(llvm::isa<SymbolValue>(*indexInfo.indexSymbolicValue_));
        EXPECT_OK_AND_FIRST_EQ(
            indexInfo.indexSymbolicValue_->getACSL({.noStateLabelFunctionAt = true}), "i");
        EXPECT_EQ(indexInfo.op_, clang::BinaryOperatorKind::BO_NE);
        EXPECT_OK_AND_FIRST_EQ(indexInfo.indexBound_->getACSL({.noStateLabelFunctionAt = true}),
                               "0");
        EXPECT_OK_AND_FIRST_EQ(indexInfo.preciseLoopCount_->simplifiedExpr()->getACSL(
                                   {.noStateLabelFunctionAt = true}),
                               "i");
    }

    TEST(SetIndexPluginTest, SimpleLoop_3) {
        auto pluginIds                = vector{"setPatterns"s, "setIndex"s};
        auto code                     = R"(
        int func(int x, int n){
            int i = n;
            while(i >= 0){
                x++;
                i--;
            }
            return x;
        }
    )";
        auto [loopInfo, continueFlag] = doPluginsOnFirstLoop(code, pluginIds);
        EXPECT_EQ(continueFlag, true);
        ASSERT_NE(loopInfo.indexInfo_, nullopt);
        auto &indexInfo = loopInfo.indexInfo_.value();
        ASSERT_NE(indexInfo.indexRealAddr_->getFromRoot(), nullopt);
        EXPECT_EQ(indexInfo.indexRealAddr_->getFromRoot().value()->getNameAsString(), "i");
        EXPECT_TRUE(llvm::isa<SymbolValue>(*indexInfo.indexSymbolicValue_));
        EXPECT_OK_AND_FIRST_EQ(
            indexInfo.indexSymbolicValue_->getACSL({.noStateLabelFunctionAt = true}), "i");
        EXPECT_EQ(indexInfo.op_, clang::BinaryOperatorKind::BO_GE);
        EXPECT_OK_AND_FIRST_EQ(indexInfo.indexBound_->getACSL({.noStateLabelFunctionAt = true}),
                               "0");
        EXPECT_OK_AND_FIRST_EQ(indexInfo.preciseLoopCount_->simplifiedExpr()->getACSL(
                                   {.noStateLabelFunctionAt = true}),
                               "i + 1");
    }

    TEST(SetIndexPluginTest, SimpleLoop_4) {
        auto pluginIds                = vector{"setPatterns"s, "setIndex"s};
        auto code                     = R"(
        void func(int* start, int* end){
            for(int* pt = start; pt < end; pt++){
                *pt = 0;
            } 
        }
    )";
        auto [loopInfo, continueFlag] = doPluginsOnFirstLoop(code, pluginIds);
        EXPECT_EQ(continueFlag, true);
        ASSERT_NE(loopInfo.indexInfo_, nullopt);
        auto &indexInfo = loopInfo.indexInfo_.value();
        ASSERT_NE(indexInfo.indexRealAddr_->getFromRoot(), nullopt);
        EXPECT_EQ(indexInfo.indexRealAddr_->getFromRoot().value()->getNameAsString(), "pt");
        EXPECT_TRUE(llvm::isa<Address>(*indexInfo.indexSymbolicValue_));
        EXPECT_OK_AND_FIRST_EQ(
            indexInfo.indexSymbolicValue_->getACSL({.noStateLabelFunctionAt = true}), "pt");
        EXPECT_EQ(indexInfo.op_, clang::BinaryOperatorKind::BO_LT);
        EXPECT_OK_AND_FIRST_EQ(indexInfo.indexBound_->getACSL({.noStateLabelFunctionAt = true}),
                               "end");
        EXPECT_OK_AND_FIRST_THAT(indexInfo.preciseLoopCount_->simplifiedExpr()->getACSL(
                                     {.noStateLabelFunctionAt = true}),
                                 AnyOf("end - pt", "-1 * pt + end"));
    }

    TEST(SetIndexPluginTest, ComplexLoop_1) {
        auto pluginIds                = vector{"setPatterns"s, "setIndex"s};
        auto code                     = R"(
    int func() {
        int i, j;
        i = 1;
        j = 10;
        while (j >= i) {
            i = i + 2;
            j = -1 + j;
        }
        return 0;
    }
    )";
        auto [loopInfo, continueFlag] = doPluginsOnFirstLoop(code, pluginIds);
        EXPECT_EQ(continueFlag, false);
        EXPECT_EQ(loopInfo.indexInfo_, nullopt);
    }

    TEST(SetIndexPluginTest, openHITLS_1) {
        auto pluginIds                = vector{"setPatterns"s, "setIndex"s};
        auto code                     = R"(
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
        auto [loopInfo, continueFlag] = doPluginsOnFirstLoop(code, pluginIds);
        EXPECT_EQ(continueFlag, false);
        ASSERT_NE(loopInfo.indexInfo_, nullopt);
        auto &indexInfo = loopInfo.indexInfo_.value();
        ASSERT_NE(indexInfo.indexRealAddr_->getFromRoot(), nullopt);
        EXPECT_EQ(indexInfo.indexRealAddr_->getFromRoot().value()->getNameAsString(), "i");
        EXPECT_TRUE(llvm::isa<SymbolValue>(*indexInfo.indexSymbolicValue_));
        EXPECT_OK_AND_FIRST_EQ(
            indexInfo.indexSymbolicValue_->getACSL({.noStateLabelFunctionAt = true}), "i");
        EXPECT_EQ(indexInfo.op_, clang::BinaryOperatorKind::BO_LT);
        EXPECT_OK_AND_FIRST_EQ(indexInfo.indexBound_->getACSL({.noStateLabelFunctionAt = true}),
                               "size");
        EXPECT_EQ(indexInfo.preciseLoopCount_->isUnknown(), true);
        EXPECT_OK_AND_FIRST_THAT(
            indexInfo.maxLoopCount_->simplifiedExpr()->getACSL({.noStateLabelFunctionAt = true}),
            AllOf(AnyOf(StartsWith("size"), HasSubstr("+ size")),
                  AnyOf(StartsWith("-1 * i"), HasSubstr("- i"))));
        EXPECT_EQ(loopInfo.extraCondConjuncts_.size(), 1);
    }
} // namespace acslg::test::unit::spec_generator