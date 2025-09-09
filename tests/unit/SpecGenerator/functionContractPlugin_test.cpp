// tests/unit/SpecGenerator/functionContractPlugin_test.cpp

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
    ASTExtractor e;
    // This code performs minimal safety checks, so please ensure the validity of the input.
    auto doPluginOnFirstFunc(const string &code, const string &pid) {
        e.init(code);
        GlobalSM::getInstance().initialize(e.getSourceManager(), e.getLangOptions());
        auto func     = e.findFirstDecl<clang::FunctionDecl>();
        auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func));
        preState->init();
        DEBUG(preState->dump());
        auto postState = preState->clone();
        for (clang::Stmt *stmt : func->getBody()->children()) {
            postState->step(stmt);
            DEBUG(postState->dump());
        }

        auto *pl = ACSLPluginRegistry::instance().get(pid);
        if (!pl)
            ERROR("Plugin with id " + pid + " does not exist!");
        auto *fcp = dynamic_cast<const FunctionContractPlugin *>(pl);

        auto spec = fcp->generate(*preState, *postState);
        if (spec)
            DEBUG(*spec);
        return spec;
    }
} // namespace

TEST(ResultPluginTest, WithStructure) {
    auto pluginId = "result";
    auto code     = R"(
        struct A{
            int x;
            unsigned long y;
        };
        void func(A x, int n){
            return x.x - x.y + n - 100;
        }
    )";
    auto spec     = doPluginOnFirstFunc(code, pluginId);
    using ::testing::HasSubstr;
    ASSERT_NE(spec, nullopt);
    EXPECT_THAT(*spec, HasSubstr(R"(ensures \result == \Old(n) - \Old(x).y + \Old(x).x - 100)"));
}