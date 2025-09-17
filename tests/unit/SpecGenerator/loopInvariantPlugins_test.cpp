// tests/unit/SpecGenerator/loopInvariantPlugins_test.cpp

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

using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::StartsWith;
using ::testing::StrEq;

namespace {
    ASTExtractor e;
    // This code performs minimal safety checks, so please ensure the validity of the input.
    std::tuple<std::optional<std::string>, bool, vector<PostInfo>> doPluginOnFirstLoop(
        const string &code,
        const string &pid) {
        e.init(code);
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

        auto loopInfo = parseLoopInfo(*preState, *loopEntry, cond, inc, body);
        if (!loopInfo) {
            // TODO(complex loop)
            UNIMPLEMENT("Loop is too complex!");
        }

        auto *pl = ACSLPluginRegistry::instance().get(pid);
        if (!pl)
            ERROR("Plugin with id " + pid + " does not exist!");
        auto *fcp = dynamic_cast<const LoopInvariantPlugin *>(pl);

        return fcp->generate(*preState, *loopEntry, cond, inc, body, *loopInfo);
    }
} // namespace

namespace Symbolic {
    std::ostream &operator<<(std::ostream &os, SymbolicExpr::ExprType e) {
        using enum SymbolicExpr::ExprType;
        switch (e) {
            case Literal: return os << "Literal";
            case Variable: return os << "Variable";
            case Address: return os << "Address";
            case BinaryOp: return os << "BinaryOp";
            case UnaryOp: return os << "UnaryOp";
            case Structure: return os << "Structure";
            case Unknown: return os << "Unknown";
        }
        return os << static_cast<std::underlying_type_t<SymbolicExpr::ExprType>>(e);
    }
} // namespace Symbolic

using ::testing::HasSubstr;
using ::testing::MatchesRegex;

TEST(LoopAssignsPluginTest, Simple_0) {
    auto pluginId                        = "loopAssigns";
    auto code                            = R"(
        void func(int *p, int n){
            int mx = 0;
            for(int i = 0; i < n; i++){
                if(mx < p[i])
                    mx = p[i];
            } 
        }
    )";
    auto [spec, continueFlag, postState] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_THAT(*spec, HasSubstr("mx"));
    EXPECT_EQ(continueFlag, true);
    ASSERT_EQ(postState.size(), 1);
    ASSERT_EQ(postState.at(0).memoryMap_.size(), 1);
    EXPECT_EQ(postState.at(0).memoryMap_.begin()->second->getType(),
              SymbolicExpr::ExprType::Unknown);
}

TEST(LoopAssignsPluginTest, Simple_1) {
    auto pluginId                        = "loopAssigns";
    auto code                            = R"(
        void func(int *p, int n){
            int cnt = 0;
            for(int i = 0; i < n; i++){
                p[i]++;
                cnt -= 1;
            } 
        }
    )";
    auto [spec, continueFlag, postState] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_THAT(*spec, HasSubstr("cnt"));
    EXPECT_THAT(*spec, HasSubstr("p[0..n - 1]"));
    EXPECT_EQ(continueFlag, true);
    ASSERT_EQ(postState.size(), 1);
    EXPECT_EQ(postState.at(0).memoryMap_.size(), 2);
    for (auto &[addr, value] : postState.at(0).memoryMap_) {
        auto addrStr = addr.get().regularFormOfValue();
        if (addrStr == nullopt)
            FAIL() << "address {" + addr.get().dump() + "} has no regular form.";
        auto valueStr = value->simplifiedExpr()->regularForm();
        if (valueStr == nullopt)
            FAIL() << "value {" + addr.get().dump() + "} has no regular form.";
        if (addrStr.value() == "p[0..n - 1]") {
            EXPECT_TRUE(value->isUnknown()) << valueStr.value();
        } else if (addrStr.value() == "cnt") {
            EXPECT_EQ(valueStr.value(), "-1 * n");
        } else {
            FAIL() << addrStr.value() << valueStr.value();
        }
    }
}

TEST(LoopAssignsPluginTest, Simple_2) {
    auto pluginId                        = "loopAssigns";
    auto code                            = R"(
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
    auto [spec, continueFlag, postState] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_THAT(*spec, HasSubstr("i"));
    EXPECT_THAT(*spec, HasSubstr("cnt"));
    EXPECT_THAT(*spec, HasSubstr("p[0..n - 1]"));
    EXPECT_EQ(continueFlag, true);
    ASSERT_EQ(postState.size(), 1);
    EXPECT_EQ(postState.at(0).memoryMap_.size(), 3);
    for (auto &[addr, value] : postState.at(0).memoryMap_) {
        auto addrStr = addr.get().regularFormOfValue();
        if (addrStr == nullopt)
            FAIL() << "address {" + addr.get().dump() + "} has no regular form.";
        auto valueStr = value->simplifiedExpr()->regularForm();
        if (valueStr == nullopt)
            FAIL() << "value {" + addr.get().dump() + "} has no regular form.";
        if (addrStr == "p[0..n - 1]") {
            EXPECT_TRUE(value->isUnknown()) << valueStr.value();
        } else if (addrStr.value() == "cnt") {
            EXPECT_EQ(valueStr.value(), "-1 * n");
        } else if (addrStr.value() == "i") {
            EXPECT_EQ(valueStr.value(), "n");
        } else {
            FAIL() << addrStr.value() << valueStr.value();
        }
    }
}

TEST(LoopAssignsPluginTest, Simple_3) {
    auto pluginId                        = "loopAssigns";
    auto code                            = R"(
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
    auto [spec, continueFlag, postState] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_THAT(*spec, HasSubstr("i"));
    EXPECT_THAT(*spec, HasSubstr("cnt"));
    EXPECT_THAT(*spec, HasSubstr("p[0..n - 1]"));
    EXPECT_EQ(continueFlag, true);
    ASSERT_EQ(postState.size(), 1);
    EXPECT_EQ(postState.at(0).memoryMap_.size(), 4);
    for (auto &[addr, value] : postState.at(0).memoryMap_) {
        auto addrStr = addr.get().regularFormOfValue();
        if (addrStr == nullopt)
            FAIL() << "address {" + addr.get().dump() + "} has no regular form.";
        auto valueStr = value->simplifiedExpr()->regularForm();
        if (valueStr == nullopt)
            FAIL() << "value {" + addr.get().dump() + "} has no regular form.";
        if (addrStr.value() == "p") {
            EXPECT_THAT(valueStr.value(), AllOf(AnyOf(StartsWith("p"), HasSubstr("+ p")),
                                                AnyOf(StartsWith("n"), HasSubstr("+ n"))));
        } else if (addrStr.value() == "p[0..n - 1]") {
            EXPECT_TRUE(value->isUnknown()) << valueStr.value();
        } else if (addrStr.value() == "cnt") {
            EXPECT_EQ(valueStr.value(), "-1 * n");
        } else if (addrStr.value() == "i") {
            EXPECT_EQ(valueStr.value(), "n");
        } else {
            FAIL() << addrStr.value() << valueStr.value();
        }
    }
}

TEST(LoopAssignsPluginTest, openHiTLS_1) {
    auto pluginId                        = "loopAssigns";
    auto code                            = R"(
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
    auto [spec, continueFlag, postState] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_THAT(*spec, HasSubstr("aa"));
    EXPECT_THAT(*spec, HasSubstr("bb"));
    EXPECT_THAT(*spec, HasSubstr("rr"));
    EXPECT_THAT(*spec, HasSubstr("nn"));
    EXPECT_THAT(*spec, HasSubstr("rr[0..n - 1]"));
    EXPECT_EQ(continueFlag, true);
    ASSERT_EQ(postState.size(), 1);
    EXPECT_EQ(postState.at(0).memoryMap_.size(), 6);
    for (auto &[addr, value] : postState.at(0).memoryMap_) {
        auto addrStr = addr.get().regularFormOfValue();
        if (addrStr == nullopt)
            FAIL() << "address {" + addr.get().dump() + "} has no regular form.";
        auto valueStr = value->simplifiedExpr()->regularForm();
        if (valueStr == nullopt)
            FAIL() << "value {" + addr.get().dump() + "} has no regular form.";
        if (addrStr.value() == "aa") {
            EXPECT_THAT(valueStr.value(), AllOf(AnyOf(StartsWith("a"), HasSubstr("+ a")),
                                                AnyOf(StartsWith("n"), HasSubstr("+ n"))));
        } else if (addrStr.value() == "bb") {
            EXPECT_THAT(valueStr.value(), AllOf(AnyOf(StartsWith("b"), HasSubstr("+ b")),
                                                AnyOf(StartsWith("n"), HasSubstr("+ n"))));
        } else if (addrStr.value() == "rr") {
            EXPECT_THAT(valueStr.value(), AllOf(AnyOf(StartsWith("r"), HasSubstr("+ r")),
                                                AnyOf(StartsWith("n"), HasSubstr("+ n"))));
        } else if (addrStr.value() == "nn") {
            EXPECT_EQ(valueStr.value(), "0");
        } else if (addrStr.value() == "rr[0..n - 1]") {
            EXPECT_TRUE(value->isUnknown());
        } else if (addrStr.value() == "carry") {
            EXPECT_TRUE(value->isUnknown());
        } else {
            FAIL() << addrStr.value() << valueStr.value();
        }
    }
}

TEST(ParadigmMaxMinPluginTest, Simple_0) {
    auto pluginId                = "paradigmMaxMin";
    auto code                    = R"(
        void func(int *p, int n){
            int mx = 0;
            for(int i = 0; i < n; i++){
                if(mx < p[i])
                    mx = p[i];
            } 
        }
    )";
    auto [spec, continueFlag, _] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}

TEST(ParadigmMaxMinPluginTest, Simple_1) {
    auto pluginId                = "paradigmMaxMin";
    auto code                    = R"(
        void func(int *p, int n){
            int ms = 0;
            for(int i = 0; i < n; i++){
                if(ms > p[i])
                    ms = p[i];
            } 
        }
    )";
    auto [spec, continueFlag, _] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}

TEST(ParadigmMaxMinPluginTest, Simple_2) {
    auto pluginId                = "paradigmMaxMin";
    auto code                    = R"(
        void func(int *p, int n){
            int mx = 0;
            for(int i = 0; i < n; i++){
                if(p[i] >= mx)
                    mx = p[i];
            } 
        }
    )";
    auto [spec, continueFlag, _] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}

TEST(ParadigmMaxMinPluginTest, Simple_3) {
    auto pluginId                = "paradigmMaxMin";
    auto code                    = R"(
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
    auto [spec, continueFlag, _] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}

TEST(ParadigmMaxMinPluginTest, Simple_4) {
    auto pluginId                = "paradigmMaxMin";
    auto code                    = R"(
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
    auto [spec, continueFlag, _] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}

TEST(ParadigmMaxMinPluginTest, Simple_5) {
    auto pluginId                = "paradigmMaxMin";
    auto code                    = R"(
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
    auto [spec, continueFlag, _] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_EQ(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}

TEST(ParadigmMaxMinPluginTest, Simple_6) {
    auto pluginId                = "paradigmMaxMin";
    auto code                    = R"(
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
    auto [spec, continueFlag, _] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
}

TEST(LinearInvariantPluginTest, Simple_1) {
    auto pluginId                         = "StInGXPlugin";
    auto code                             = R"(
        void func(int n){
            int x = 0, y = n, z = 10;
            for(int i = 0; i < n; i++){
                x++;
                y--;
                z--;
            } 
        }
    )";
    auto [spec, continueFlag, postStates] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
    ASSERT_EQ(postStates.size(), 1);
    auto &postState = postStates.at(0);
    for (auto &[addr, value] : postState.memoryMap_) {
        auto var = addr.get().regularFormOfValue();
        if (var == nullopt)
            FAIL() << "value {" + addr.get().dump() + "} has no regular form.";
        auto valueStr = value->simplifiedExpr()->regularForm();
        if (valueStr == nullopt)
            FAIL() << "value {" + addr.get().dump() + "} has no regular form.";
        if (var.value() == "x") {
            EXPECT_THAT(valueStr.value(), AllOf(AnyOf(StartsWith("x"), HasSubstr("+ x")),
                                                AnyOf(StartsWith("-1 * i"), HasSubstr("- i")),
                                                AnyOf(StartsWith("n"), HasSubstr("+ n"))));
        } else if (var.value() == "y") {
            EXPECT_THAT(valueStr.value(), AllOf(AnyOf(StartsWith("y"), HasSubstr("+ y")),
                                                AnyOf(StartsWith("i"), HasSubstr("+ i")),
                                                AnyOf(StartsWith("-1 * n"), HasSubstr("- n"))));
        } else if (var.value() == "z") {
            EXPECT_THAT(valueStr.value(), AllOf(AnyOf(StartsWith("z"), HasSubstr("+ z")),
                                                AnyOf(StartsWith("i"), HasSubstr("+ i")),
                                                AnyOf(StartsWith("-1 * n"), HasSubstr("- n"))));
        } else if (var.value() != "i" && var.value() != "n") {
            FAIL() << var.value();
        }
    }
}

TEST(LinearInvariantPluginTest, Simple_2) {
    auto pluginId                         = "StInGXPlugin";
    auto code                             = R"(
        void func(int n){
            int x = 0, sum = 0;
            for(int i = 0; i < n; i++){
                x++;
                sum += x;
            } 
        }
    )";
    auto [spec, continueFlag, postStates] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
    ASSERT_EQ(postStates.size(), 1);
    auto &postState = postStates.at(0);
    for (auto &[addr, value] : postState.memoryMap_) {
        auto var = addr.get().regularFormOfValue();
        if (var == nullopt)
            FAIL() << "value {" + addr.get().dump() + "} has no regular form.";
        auto valueStr = value->simplifiedExpr()->regularForm();
        if (valueStr == nullopt)
            FAIL() << "value {" + addr.get().dump() + "} has no regular form.";
        if (var.value() == "x") {
            EXPECT_THAT(valueStr.value(), AllOf(AnyOf(StartsWith("x"), HasSubstr("+ x")),
                                                AnyOf(StartsWith("-1 * i"), HasSubstr("- i")),
                                                AnyOf(StartsWith("n"), HasSubstr("+ n"))));
        } else if (var.value() != "i" && var.value() != "n") {
            FAIL() << var.value();
        }
    }
}

TEST(LinearInvariantPluginTest, Simple_3) {
    auto pluginId                         = "StInGXPlugin";
    auto code                             = R"(
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
    auto [spec, continueFlag, postStates] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
    ASSERT_EQ(postStates.size(), 1);
    auto &postState = postStates.at(0);
    for (auto &[addr, value] : postState.memoryMap_) {
        auto var = addr.get().regularFormOfValue();
        if (var == nullopt)
            FAIL() << "value {" + addr.get().dump() + "} has no regular form.";
        auto valueStr = value->simplifiedExpr()->regularForm();
        if (valueStr == nullopt)
            FAIL() << "value {" + addr.get().dump() + "} has no regular form.";
        if (var.value() == "i") {
            EXPECT_THAT(valueStr.value(), AllOf(AnyOf(StartsWith("i"), HasSubstr("+ i")),
                                                AnyOf(StartsWith("2 * j"), HasSubstr("+ 2 * j")),
                                                AnyOf(StartsWith("22"), HasSubstr("+ 22"))));
        } else if (var.value() == "j") {
            EXPECT_THAT(valueStr.value(), "-11");
        } else {
            FAIL() << var.value();
        }
    }
}

TEST(LinearInvariantPluginTest, Simple_4) {
    auto pluginId                         = "StInGXPlugin";
    auto code                             = R"(
    void func(int *p, int n) {
        int *pt = p;
        for(int i = 0; i < n; ++i){
            *pt = 0;
            ++pt;
        }
    }
    )";
    auto [spec, continueFlag, postStates] = doPluginOnFirstLoop(code, pluginId);
    EXPECT_NE(spec, nullopt);
    EXPECT_EQ(continueFlag, true);
    ASSERT_EQ(postStates.size(), 1);
    auto &postState = postStates.at(0);
    for (auto &[addr, value] : postState.memoryMap_) {
        auto var = addr.get().regularFormOfValue();
        if (var == nullopt)
            FAIL() << "value {" + addr.get().dump() + "} has no regular form.";
        auto valueStr = value->simplifiedExpr()->regularForm();
        if (valueStr == nullopt)
            FAIL() << "value {" + addr.get().dump() + "} has no regular form.";
        if (var.value() == "pt") {
            EXPECT_THAT(valueStr.value(), AllOf(AnyOf(StartsWith("-1 * i"), HasSubstr("- i")),
                                                AnyOf(StartsWith("n"), HasSubstr("+ n")),
                                                AnyOf(StartsWith("p"), HasSubstr("+ p"))));
        } else if (var.value() != "p" && var.value() != "n" && var.value() != "i") {
            FAIL() << var.value();
        }
    }
}

// TEST(LinearInvariantPluginTest, Simple_5) {
//     auto pluginId                = "StInGXPlugin";
//     auto code                    = R"(
//         void func(int *p, int n){
//             int mx = 0;
//             for(int i = 0; i < n; i++){
//                 if(mx < p[i])
//                     mx = p[i];
//             }
//         }
//     )";
//     auto [spec, continueFlag, _] = doPluginOnFirstLoop(code, pluginId);
//     EXPECT_NE(spec, nullopt);
//     EXPECT_EQ(continueFlag, true);
// }