// tests/testUtils/testHelper.h

#ifndef __ACSLG_TESTS_TESTUTILS_TESTHELPER_H__
#define __ACSLG_TESTS_TESTUTILS_TESTHELPER_H__

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ASTExtractor.h"
#include "SpecGenerator/specGenerator.h"
#include "Analyzer/state.h"

namespace acslg::test::utils {
    // These codes performs minimal safety checks, so please ensure the validity of the input.

    std::optional<std::string> doPluginOnFirstFunc(const std::string &code, const std::string &pid);
    std::string doAll(const std::string_view code);
    std::unique_ptr<analyzer::ProgramState> execOnFirstFunc(const std::string &code);
    ::acslg::utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>> getReturnExprOfFirstPath(
        const analyzer::ProgramState &state);
    ::acslg::utils::not_null<std::unique_ptr<analyzer::ProgramState>> getPostStateOfFirstLoop(
        const std::string_view code);
    std::pair<spec_generator::LoopInfo, bool> doPluginsOnFirstLoop(
        std::string_view code,
        const std::vector<std::string> pids);
    spec_generator::PathInsensitiveLoopInvPlugin::GenResultType doPIPluginOnFirstLoop(
        const std::string &code,
        const std::string &pid);
    std::optional<spec_generator::PathSensitiveLoopInvPlugin::GenResultType> doPSPluginOnFirstLoop(
        const std::string &code,
        const std::string &pid);

    namespace details {
        consteval unsigned digitCount(unsigned x) {
            unsigned n = 1;
            while (x >= 10) {
                x /= 10;
                ++n;
            }
            return n;
        }

        consteval auto makeCodeArr() {
            struct Buf {
                char data[2048];
                unsigned n = 0;
            } b{};
            auto app = [&](const char *s) {
                while (*s)
                    b.data[b.n++] = *s++;
            };
            auto appUInt = [&](unsigned v) {
                char tmp[16];
                int k = 0;
                do {
                    tmp[k++] = char('0' + (v % 10));
                    v /= 10;
                } while (v);
                while (k--)
                    b.data[b.n++] = tmp[k];
            };

            app("void func() {}");
            for (unsigned i = 1; i <= 20; ++i) {
                app("int g");
                appUInt(i);
                app(";");
            }
            for (unsigned i = 1; i <= 20; ++i) {
                app("int f");
                appUInt(i);
                app("(int x){return 0;}");
            }

            std::array<char, 2048> arr{};
            for (unsigned i = 0; i < b.n; ++i)
                arr[i] = b.data[i];
            arr[b.n] = '\0';
            return std::pair{arr, b.n};
        }
    } // namespace details

    class FixtureWithCode : public ::testing::Test {
      protected:
        static constexpr auto codeArr = details::makeCodeArr();
        static constexpr std::string_view code{codeArr.first.data(), codeArr.second};

        FixtureWithCode();
        ::acslg::utils::not_null<const clang::VarDecl *> getVarDecl(unsigned int id);
        ::acslg::utils::not_null<const clang::FunctionDecl *> getFuncDecl(unsigned int id);
        analyzer::symbolic::VariableAddress makeVariableAddr(unsigned int id);
        ::acslg::utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>
        makeLiteralExpr(std::uint64_t value);
        ::acslg::utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>
        makeRangeIndexExpr(std::string_view name);
        analyzer::symbolic::SymbolAddress makeRangeAddr(
            unsigned int id,
            std::unique_ptr<const analyzer::symbolic::SymbolicExpr> offset,
            std::unique_ptr<const analyzer::symbolic::SymbolicExpr> len,
            std::optional<analyzer::symbolic::SourcePoint> fromPoint = std::nullopt);
        std::unique_ptr<analyzer::symbolic::SymbolValue> makeSymbolValue(
            unsigned int id,
            std::optional<analyzer::symbolic::SourcePoint> fromPoint = std::nullopt);
        analyzer::symbolic::SymbolAddress makeSimpleSymbolAddr(
            unsigned int id,
            std::optional<analyzer::symbolic::SourcePoint> fromPoint = std::nullopt);
        analyzer::symbolic::SymbolAddress makePointAddr(unsigned int id, std::uint64_t off);
        ::testing::AssertionResult ExpectReadEqAt(analyzer::MemoryModel &mm,
                                                  unsigned id,
                                                  uint64_t off,
                                                  const analyzer::symbolic::SymbolicExpr &expected);
        ::testing::AssertionResult ExpectReadNullAt(analyzer::MemoryModel &mm,
                                                    unsigned id,
                                                    uint64_t off);

        ASTExtractor e;
        analyzer::symbolic::SourcePoint defaultPoint;

      private:
        std::vector<const clang::VarDecl *> varDecls;
        size_t var_count{0};
        std::unordered_map<unsigned int, size_t> varIdCountMap{};

        std::vector<const clang::FunctionDecl *> funcDecls;
        size_t func_count{0};
        std::unordered_map<unsigned int, size_t> funcIdCountMap{};

        analyzer::symbolic::ExprFactory exprFactory_;
        analyzer::symbolic::ExprFactoryScope exprScope_;
    };

#define EXPECT_OK_AND_FIRST_EQ(expr, expected_first)                                               \
    do {                                                                                           \
        auto _res = (expr);                                                                        \
        ASSERT_TRUE(_res) << "Expected success but got error";                                     \
        EXPECT_EQ(_res.value().first, expected_first);                                             \
    } while (0)

#define EXPECT_OK_AND_FIRST_THAT(expr, expected_first)                                             \
    do {                                                                                           \
        auto _res = (expr);                                                                        \
        ASSERT_TRUE(_res) << "Expected success but got error";                                     \
        EXPECT_THAT(_res.value().first, expected_first);                                           \
    } while (0)

#define ASSERT_OK_AND_GET_FIRST_TO_VAR(expr, var)                                                  \
    auto _res##var = (expr);                                                                       \
    ASSERT_TRUE(_res##var) << "Expected success but got error";                                    \
    auto var = _res##var.value().first;

} // namespace acslg::test::utils

#endif
