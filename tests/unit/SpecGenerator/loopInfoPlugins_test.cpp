// tests/unit/SpecGenerator/loopInfoPlugins_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <unordered_map>
#include <string>
#include <llvm/Support/Casting.h>
#include "ASTExtractor.h"
#include "SpecGenerator/specGenerator.h"
#include "Analyzer/function.h"
#include "Analyzer/state.h"

using namespace std;
using namespace llvm;

using ::testing::AnyOf;
using ::testing::Eq;
using ::testing::StrEq;

namespace {
    ASTExtractor e;
    // This code performs minimal safety checks, so please ensure the validity of the input.
    auto doPluginOnFirstLoop(string_view code, const vector<string> pids) {
        e.init(code);
        static optional<ACSLContext> context{};
        context.emplace(e.getASTContext());
        auto func     = e.findFirstDecl<clang::FunctionDecl>();
        auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
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

        bool result;
        for (auto &pid : pids) {
            auto *pl = ACSLPluginRegistry::instance().get(pid);
            if (!pl)
                ERROR("Plugin with id " + pid + " does not exist!");
            auto *fcp = dynamic_cast<const LoopInfoPlugin *>(pl);
            result    = fcp->parse(*preState, *loopEntry, cond, inc, body, loopInfo);
        }
        return pair{std::move(loopInfo), result};
    }
} // namespace

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
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginIds);
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
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginId);
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
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginId);
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
            if (addr.get().getAddressType() != Address::AddressType::SymbolAddr)
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
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_EQ(continueFlag, true);
    ASSERT_NE(loopInfo.patternInfo_, nullopt);
    auto &patternInfo = loopInfo.patternInfo_.value();
    EXPECT_EQ(patternInfo.patternsMap_.size(), 2);
    for (auto &[addr, pattern] : patternInfo.patternsMap_) {
        DEBUG(addr.get().dump());
        DEBUG(pattern.value().initialValue_->dump() +
              ", step: " + to_string(pattern.value().step_));
        if (addr.get().getAddressType() == Address::AddressType::SymbolAddr)
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
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_EQ(continueFlag, true);
    ASSERT_NE(loopInfo.patternInfo_, nullopt);
    auto &patternInfo = loopInfo.patternInfo_.value();
    EXPECT_EQ(patternInfo.patternsMap_.size(), 6);
    for (auto &[addr, pattern] : patternInfo.patternsMap_) {
        auto addrStr = addr.get().regularFormOfValue();
        if (addrStr == nullopt)
            FAIL() << "address {" + addr.get().dump() << "} has no regular form.";
        if (addrStr.value() == "aa") {
            ASSERT_NE(pattern, nullopt);
            EXPECT_EQ(pattern.value().step_, 1);
        } else if (addrStr.value() == "bb") {
            ASSERT_NE(pattern, nullopt);
            EXPECT_EQ(pattern.value().step_, 1);
        } else if (addrStr.value() == "rr") {
            ASSERT_NE(pattern, nullopt);
            EXPECT_EQ(pattern.value().step_, 1);
        } else if (addrStr.value() == "nn") {
            ASSERT_NE(pattern, nullopt);
            EXPECT_EQ(pattern.value().step_, -1);
        } else if (addrStr.value() == "*(rr)" || addrStr == "rr[0]") {
            EXPECT_EQ(pattern, nullopt);
        } else if (addrStr.value() == "carry") {
            EXPECT_EQ(pattern, nullopt);
        } else {
            FAIL() << addrStr.value() << ": "
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
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginIds);
    EXPECT_EQ(continueFlag, true);
    ASSERT_NE(loopInfo.indexInfo_, nullopt);
    auto &indexInfo = loopInfo.indexInfo_.value();
    ASSERT_NE(indexInfo.indexAddr_->getFromRoot(), nullopt);
    EXPECT_EQ(indexInfo.indexAddr_->getFromRoot().value()->getNameAsString(), "i");
    EXPECT_EQ(indexInfo.indexSymbolicValue_->getType(), SymbolicExpr::ExprType::Variable);
    EXPECT_EQ(indexInfo.indexSymbolicValue_->regularForm().value_or(""), "i");
    EXPECT_EQ(indexInfo.op_, clang::BinaryOperatorKind::BO_LT);
    EXPECT_EQ(indexInfo.indexBound_->regularForm().value_or(""), "n");
    EXPECT_EQ(indexInfo.preciseLoopCount_->simplifiedExpr()->regularForm().value_or(""), "n - i");
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
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginIds);
    EXPECT_EQ(continueFlag, true);
    ASSERT_NE(loopInfo.indexInfo_, nullopt);
    auto &indexInfo = loopInfo.indexInfo_.value();
    ASSERT_NE(indexInfo.indexAddr_->getFromRoot(), nullopt);
    EXPECT_EQ(indexInfo.indexAddr_->getFromRoot().value()->getNameAsString(), "i");
    EXPECT_EQ(indexInfo.indexSymbolicValue_->getType(), SymbolicExpr::ExprType::Variable);
    EXPECT_EQ(indexInfo.indexSymbolicValue_->regularForm().value_or(""), "i");
    EXPECT_EQ(indexInfo.op_, clang::BinaryOperatorKind::BO_NE);
    EXPECT_EQ(indexInfo.indexBound_->regularForm().value_or(""), "0");
    EXPECT_EQ(indexInfo.preciseLoopCount_->simplifiedExpr()->regularForm().value_or(""), "i");
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
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginIds);
    EXPECT_EQ(continueFlag, true);
    ASSERT_NE(loopInfo.indexInfo_, nullopt);
    auto &indexInfo = loopInfo.indexInfo_.value();
    ASSERT_NE(indexInfo.indexAddr_->getFromRoot(), nullopt);
    EXPECT_EQ(indexInfo.indexAddr_->getFromRoot().value()->getNameAsString(), "i");
    EXPECT_EQ(indexInfo.indexSymbolicValue_->getType(), SymbolicExpr::ExprType::Variable);
    EXPECT_EQ(indexInfo.indexSymbolicValue_->regularForm().value_or(""), "i");
    EXPECT_EQ(indexInfo.op_, clang::BinaryOperatorKind::BO_GE);
    EXPECT_EQ(indexInfo.indexBound_->regularForm().value_or(""), "0");
    EXPECT_EQ(indexInfo.preciseLoopCount_->simplifiedExpr()->regularForm().value_or(""), "i + 1");
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
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginIds);
    EXPECT_EQ(continueFlag, true);
    ASSERT_NE(loopInfo.indexInfo_, nullopt);
    auto &indexInfo = loopInfo.indexInfo_.value();
    ASSERT_NE(indexInfo.indexAddr_->getFromRoot(), nullopt);
    EXPECT_EQ(indexInfo.indexAddr_->getFromRoot().value()->getNameAsString(), "pt");
    EXPECT_EQ(indexInfo.indexSymbolicValue_->getType(), SymbolicExpr::ExprType::Address);
    EXPECT_EQ(indexInfo.indexSymbolicValue_->regularForm().value_or(""), "pt");
    EXPECT_EQ(indexInfo.op_, clang::BinaryOperatorKind::BO_LT);
    EXPECT_EQ(indexInfo.indexBound_->regularForm().value_or(""), "end");
    EXPECT_THAT(indexInfo.preciseLoopCount_->simplifiedExpr()->regularForm().value_or(""),
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
    auto [loopInfo, continueFlag] = doPluginOnFirstLoop(code, pluginIds);
    EXPECT_EQ(continueFlag, false);
    EXPECT_EQ(loopInfo.indexInfo_, nullopt);
}