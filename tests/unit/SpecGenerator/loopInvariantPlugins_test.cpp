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
#include "globalSM.h"

using namespace std;
using namespace llvm;

namespace {
    using mem_map_vector =
        vector<unordered_map<Address, unique_ptr<SymbolicExpr>, AddressHash, AddressEqual>>;
    // This code performs minimal safety checks, so please ensure the validity of the input.
    std::tuple<std::optional<std::string>, bool, mem_map_vector> doPluginOnFirstLoop(
        const string &code,
        const string &pid) {
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
            case SymbolAddress: return os << "SymbolAddress";
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
    ASSERT_EQ(postState.at(0).size(), 1);
    EXPECT_EQ(postState.at(0).begin()->second->getType(), SymbolicExpr::ExprType::Unknown);
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
    EXPECT_THAT(*spec, HasSubstr("p[0...n]"));
    EXPECT_EQ(continueFlag, true);
    ASSERT_EQ(postState.size(), 1);
    ASSERT_EQ(postState.at(0).size(), 2);
    for (auto &[addr, value] : postState.at(0)) {
        DEBUG(addr.dump());
        DEBUG(value->dump());
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
    EXPECT_THAT(*spec, HasSubstr("p[0...n]"));
    EXPECT_EQ(continueFlag, true);
    ASSERT_EQ(postState.size(), 1);
    ASSERT_EQ(postState.at(0).size(), 3);
    for (auto &[addr, value] : postState.at(0)) {
        DEBUG(addr.dump());
        DEBUG(value->dump());
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