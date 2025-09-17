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
    auto doPluginOnFirstFunc(const string &code, const string &pid) {
        e.init(code);
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

TEST(ResultPluginTest, WithStructure_1) {
    auto pluginId = "result";
    auto code     = R"(
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
            doPluginOnFirstFunc(code, pluginId);
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    auto spec = doPluginOnFirstFunc(code, pluginId);
    ASSERT_NE(spec, nullopt);
    auto text = "== "s;
    if (size_t pos = spec.value().find(text); pos != std::string::npos) {
        std::string after = spec.value().substr(pos + text.size());
        EXPECT_THAT(after, AllOf(AnyOf(StartsWith("\\Old(x.x)"), HasSubstr("+ \\Old(x.x)")),
                                 AnyOf(StartsWith("-1 * \\Old(x.y)"), HasSubstr("- \\Old(x.y)")),
                                 AnyOf(StartsWith("\\Old(n)"), HasSubstr("+ \\Old(n)")),
                                 AnyOf(StartsWith("-1 * 100"), HasSubstr("- 100"))));
    } else {
        FAIL();
    }
}

TEST(ResultPluginTest, WithStructure_2) {
    auto pluginId = "result";
    auto code     = R"(
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
            doPluginOnFirstFunc(code, pluginId);
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    auto spec = doPluginOnFirstFunc(code, pluginId);
    ASSERT_NE(spec, nullopt);
    auto text = "== "s;
    if (size_t pos = spec.value().find(text); pos != std::string::npos) {
        std::string after = spec.value().substr(pos + text.size());
        EXPECT_THAT(after, AllOf(AnyOf(StartsWith("\\Old(x.x)"), HasSubstr("+ \\Old(x.x)")),
                                 AnyOf(StartsWith("-1 * 99"), HasSubstr("- 99")),
                                 AnyOf(StartsWith("\\Old(n)"), HasSubstr("+ \\Old(n)"))));
    } else {
        FAIL();
    }
}

TEST(ResultPluginTest, WithStructure_3) {
    auto pluginId = "result";
    auto code     = R"(
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
            doPluginOnFirstFunc(code, pluginId);
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    auto spec = doPluginOnFirstFunc(code, pluginId);
    ASSERT_NE(spec, nullopt);
    auto text = "== "s;
    if (size_t pos = spec.value().find(text); pos != std::string::npos) {
        std::string after = spec.value().substr(pos + text.size());
        EXPECT_THAT(after, AllOf(AnyOf(StartsWith("\\Old(*x.x)"), HasSubstr("+ \\Old(*x.x)")),
                                 AnyOf(StartsWith("-1 * 98"), HasSubstr("- 98")),
                                 AnyOf(StartsWith("\\Old(n)"), HasSubstr("+ \\Old(n)"))));
    } else {
        FAIL();
    }
}

TEST(ResultPluginTest, WithStructure_4) {
    auto pluginId = "result";
    auto code     = R"(
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
            doPluginOnFirstFunc(code, pluginId);
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
    auto spec = doPluginOnFirstFunc(code, pluginId);
    ASSERT_NE(spec, nullopt);
    auto text = "== "s;
    if (size_t pos = spec.value().find(text); pos != std::string::npos) {
        std::string after = spec.value().substr(pos + text.size());
        EXPECT_THAT(after, AllOf(AnyOf(StartsWith("\\Old(*a.x)"), HasSubstr("+ \\Old(*a.x)")),
                                 AnyOf(StartsWith("\\Old(*b.x)"), HasSubstr("+ \\Old(*b.x)")),
                                 AnyOf(StartsWith("\\Old(b.y)"), HasSubstr("+ \\Old(b.y)")),
                                 AnyOf(StartsWith("142"), HasSubstr("+ 142"))));
    } else {
        FAIL();
    }
}