// tests/testUtils/testHelper.h

#ifndef __ACSLG_TESTS_TESTUTILS_TESTHELPER_H__
#define __ACSLG_TESTS_TESTUTILS_TESTHELPER_H__

#include <utility>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ASTExtractor.h"
#include "SpecGenerator/specGenerator.h"
#include "Analyzer/Symbolic/aggregateExpr.h"
#include "Analyzer/Symbolic/detail/facadeAccess.h"
#include "Analyzer/state.h"

namespace acslg::test::utils {
    using analyzer::symbolic::detail::FacadeAccess;

    // These codes performs minimal safety checks, so please ensure the validity of the input.

    std::optional<std::string> doPluginOnFirstFunc(const std::string &code, const std::string &pid);
    std::string doAll(const std::string_view code);
    std::unique_ptr<analyzer::ProgramState> execOnFirstFunc(const std::string &code);
    analyzer::symbolic::ExprFactory &getLastExprFactory();
    inline analyzer::symbolic::detail::ExprHandle facadeHandle(
        const analyzer::symbolic::Expr &expression) {
        return FacadeAccess::exprHandle(expression);
    }
    inline analyzer::symbolic::detail::AddrHandle facadeHandle(
        const analyzer::symbolic::Addr &address) {
        return FacadeAccess::addressHandle(address);
    }
    inline analyzer::symbolic::detail::AddrHandle facadeHandle(
        const analyzer::symbolic::AddressBox &address) {
        return FacadeAccess::addressBoxHandle(address);
    }
    inline analyzer::symbolic::Expr facadeExpr(analyzer::symbolic::ExprFactory &factory,
                                                analyzer::symbolic::detail::ExprHandle handle) {
        return FacadeAccess::makeExpr(factory, handle);
    }
    inline analyzer::symbolic::Expr facadeExpr(analyzer::symbolic::detail::ExprHandle handle) {
        return FacadeAccess::makeExpr(analyzer::symbolic::ExprFactoryScope::current(), handle);
    }
    inline analyzer::symbolic::Addr facadeAddr(analyzer::symbolic::ExprFactory &factory,
                                                analyzer::symbolic::detail::AddrHandle handle) {
        return FacadeAccess::makeAddress(factory, handle);
    }
    inline analyzer::symbolic::Addr facadeAddr(analyzer::symbolic::detail::AddrHandle handle) {
        return FacadeAccess::makeAddress(analyzer::symbolic::ExprFactoryScope::current(), handle);
    }
    inline analyzer::symbolic::detail::ExprHandle importExprHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::ExprHandle handle) {
        return facadeHandle(facadeExpr(factory, handle));
    }
    inline analyzer::symbolic::detail::AddrHandle importAddressHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::AddrHandle handle) {
        return facadeHandle(facadeAddr(factory, handle));
    }
    inline analyzer::symbolic::AddressBox facadeAddressBox(
        analyzer::symbolic::detail::AddrHandle handle) {
        return FacadeAccess::makeAddressBox(handle);
    }
    template <typename T>
    inline analyzer::symbolic::detail::ExprHandle literalHandle(
        analyzer::symbolic::ExprFactory &factory,
        T value) {
        return facadeHandle(analyzer::symbolic::LiteralExpr{factory, value});
    }
    inline analyzer::symbolic::detail::ExprHandle rangeIndexHandle(
        analyzer::symbolic::ExprFactory &factory,
        std::string_view name) {
        return facadeHandle(analyzer::symbolic::Expr::rangeIndex(factory, name));
    }
    inline analyzer::symbolic::detail::ExprHandle unknownHandle(
        analyzer::symbolic::ExprFactory &factory) {
        return facadeHandle(analyzer::symbolic::Expr::unknown(factory));
    }
    inline analyzer::symbolic::detail::ExprHandle binaryHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::ExprHandle left,
        analyzer::symbolic::BinaryOp op,
        analyzer::symbolic::detail::ExprHandle right) {
        return facadeHandle(facadeExpr(factory, left).binary(op, facadeExpr(factory, right)));
    }
    inline analyzer::symbolic::detail::ExprHandle unaryHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::UnaryOp op,
        analyzer::symbolic::detail::ExprHandle expression) {
        return facadeHandle(facadeExpr(factory, expression).unary(op));
    }
    inline analyzer::symbolic::detail::ExprHandle withTypeHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::ExprHandle expression,
        analyzer::symbolic::ExprType type) {
        return facadeHandle(facadeExpr(factory, expression).withType(type));
    }
    inline analyzer::symbolic::detail::ExprHandle simplifiedBinaryHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::ExprHandle left,
        analyzer::symbolic::BinaryOp op,
        analyzer::symbolic::detail::ExprHandle right) {
        return facadeHandle(
            facadeExpr(factory, left).binary(op, facadeExpr(factory, right)).simplified());
    }
    inline analyzer::symbolic::detail::ExprHandle symbolValueHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::ExprType type,
        analyzer::symbolic::detail::AddrHandle from,
        analyzer::symbolic::SourcePoint fromPoint) {
        return facadeHandle(analyzer::symbolic::Expr::symbolValue(
            type, facadeAddr(factory, from), std::move(fromPoint)));
    }
    inline analyzer::symbolic::detail::ExprHandle structureHandle(
        analyzer::symbolic::ExprFactory &factory,
        const clang::RecordDecl *record,
        analyzer::symbolic::detail::AddrHandle from,
        analyzer::symbolic::SourcePoint fromPoint) {
        return facadeHandle(analyzer::symbolic::Expr::structure(
            record, facadeAddr(factory, from), std::move(fromPoint)));
    }
    inline analyzer::symbolic::detail::ExprHandle structureHandle(
        analyzer::symbolic::ExprFactory &factory,
        const clang::RecordDecl *record,
        const clang::ASTRecordLayout &,
        analyzer::symbolic::detail::AddrHandle from,
        analyzer::symbolic::SourcePoint fromPoint) {
        return structureHandle(factory, record, from, std::move(fromPoint));
    }
    inline analyzer::symbolic::detail::ExprHandle withFieldHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::ExprHandle structure,
        size_t index,
        analyzer::symbolic::detail::ExprHandle value) {
        return facadeHandle(
            facadeExpr(factory, structure).withField(index, facadeExpr(factory, value)));
    }
    inline analyzer::symbolic::detail::AddrHandle variableAddressHandle(
        analyzer::symbolic::ExprFactory &factory,
        ::acslg::utils::not_null<const clang::VarDecl *> variable) {
        return facadeHandle(analyzer::symbolic::Addr::variable(factory, variable));
    }
    inline analyzer::symbolic::detail::AddrHandle fieldAddressHandle(
        analyzer::symbolic::ExprFactory &factory,
        clang::QualType pointeeType,
        const clang::RecordDecl *record,
        analyzer::symbolic::detail::AddrHandle base,
        size_t fieldIndex) {
        return facadeHandle(facadeAddr(factory, base).field(pointeeType, record, fieldIndex));
    }
    inline analyzer::symbolic::detail::AddrHandle symbolAddressHandle(
        analyzer::symbolic::ExprFactory &factory,
        clang::QualType pointeeType,
        std::optional<analyzer::symbolic::detail::AddrHandle> from,
        analyzer::symbolic::SourcePoint fromPoint,
        std::optional<analyzer::symbolic::detail::ExprHandle> offset = std::nullopt,
        std::optional<analyzer::symbolic::detail::ExprHandle> length = std::nullopt) {
        auto address = from
                           ? analyzer::symbolic::Addr::symbol(
                                 pointeeType, facadeAddr(factory, *from), fromPoint)
                           : analyzer::symbolic::Addr::symbol(factory, pointeeType, fromPoint);
        if (offset)
            address = address.withOffset(facadeExpr(factory, *offset));
        if (length)
            address = address.withLength(facadeExpr(factory, *length));
        return facadeHandle(address);
    }
    inline analyzer::symbolic::detail::AddrHandle withOffsetHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::AddrHandle address,
        analyzer::symbolic::detail::ExprHandle offset) {
        return facadeHandle(facadeAddr(factory, address).withOffset(facadeExpr(factory, offset)));
    }
    inline analyzer::symbolic::detail::AddrHandle withAddedOffsetHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::AddrHandle address,
        analyzer::symbolic::detail::ExprHandle extra) {
        return facadeHandle(
            facadeAddr(factory, address).withAddedOffset(facadeExpr(factory, extra)));
    }
    inline analyzer::symbolic::detail::AddrHandle withSubtractedOffsetHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::AddrHandle address,
        analyzer::symbolic::detail::ExprHandle extra) {
        return facadeHandle(
            facadeAddr(factory, address).withSubtractedOffset(facadeExpr(factory, extra)));
    }
    inline analyzer::symbolic::detail::AddrHandle withLengthHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::AddrHandle address,
        analyzer::symbolic::detail::ExprHandle length) {
        return facadeHandle(facadeAddr(factory, address).withLength(facadeExpr(factory, length)));
    }
    inline analyzer::symbolic::detail::AddrHandle withAddedLengthHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::AddrHandle address,
        analyzer::symbolic::detail::ExprHandle extra) {
        return facadeHandle(
            facadeAddr(factory, address).withAddedLength(facadeExpr(factory, extra)));
    }
    inline analyzer::symbolic::detail::AddrHandle withoutLengthHandle(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::AddrHandle address) {
        return facadeHandle(facadeAddr(factory, address).withoutLength());
    }
    class FacadeExprForTest : public analyzer::symbolic::Expr {
      public:
        FacadeExprForTest(analyzer::symbolic::ExprFactory &factory,
                          analyzer::symbolic::detail::ExprHandle handle)
            : Expr(FacadeAccess::makeExpr(factory, handle)) {}
        explicit FacadeExprForTest(analyzer::symbolic::detail::ExprHandle handle)
            : Expr(FacadeAccess::makeExpr(analyzer::symbolic::ExprFactoryScope::current(), handle)) {}
        explicit FacadeExprForTest(const analyzer::symbolic::Expr &expression)
            : Expr(expression) {}
    };
    class FacadeAddrForTest : public analyzer::symbolic::Addr {
      public:
        FacadeAddrForTest(analyzer::symbolic::ExprFactory &factory,
                          analyzer::symbolic::detail::AddrHandle handle)
            : Addr(FacadeAccess::makeAddress(factory, handle)) {}
        explicit FacadeAddrForTest(analyzer::symbolic::detail::AddrHandle handle)
            : Addr(FacadeAccess::makeAddress(analyzer::symbolic::ExprFactoryScope::current(),
                                             handle)) {}
        explicit FacadeAddrForTest(const analyzer::symbolic::Addr &address) : Addr(address) {}
    };
    inline analyzer::symbolic::detail::ExprHandle simplifyForTest(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::ExprHandle expression) {
        return FacadeAccess::exprHandle(
            FacadeAccess::makeExpr(factory, expression).simplified());
    }
    inline std::optional<analyzer::symbolic::detail::AddrHandle> evaluateAddressForTest(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::ExprHandle expression) {
        auto address = FacadeAccess::makeExpr(factory, expression).evaluatedAddress();
        if (!address)
            return std::nullopt;
        return FacadeAccess::addressHandle(*address);
    }
    inline analyzer::symbolic::detail::ExprHandle substituteValuesForTest(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::ExprHandle expression,
        const analyzer::symbolic::ExprSubstitutions &substitutions) {
        return FacadeAccess::exprHandle(
            FacadeAccess::makeExpr(factory, expression).substituteValues(substitutions));
    }
    inline analyzer::symbolic::detail::ExprHandle substitutePathForTest(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::ExprHandle expression,
        const analyzer::Path &path,
        const analyzer::symbolic::SourcePoint &point) {
        return FacadeAccess::exprHandle(
            FacadeAccess::makeExpr(factory, expression).substitutePath(path, point));
    }
    inline analyzer::symbolic::detail::ExprHandle substituteRangeIndexForTest(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::ExprHandle expression,
        const analyzer::symbolic::SymbolAddrBaseInfo &rangeBase,
        analyzer::symbolic::detail::ExprHandle index) {
        auto indexExpr = FacadeAccess::makeExpr(factory, index);
        return FacadeAccess::exprHandle(FacadeAccess::makeExpr(factory, expression)
                                            .substituteRangeIndex(rangeBase, indexExpr));
    }
    inline analyzer::symbolic::detail::ExprHandle makeSumOverRangeForTest(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::AddrHandle range,
        std::string_view indexName,
        analyzer::symbolic::SourcePoint fromPoint) {
        return FacadeAccess::exprHandle(analyzer::symbolic::SumOverRangeExpr{
            FacadeAccess::makeAddress(factory, range), indexName, std::move(fromPoint)});
    }
    inline analyzer::symbolic::detail::ExprHandle makeQuantifierOverRangeForTest(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::AddrHandle range,
        std::string_view indexName,
        analyzer::symbolic::RangeQuantifier quantifier,
        analyzer::symbolic::detail::ExprHandle predicate) {
        return FacadeAccess::exprHandle(analyzer::symbolic::QuantifierOverRangeExpr{
            FacadeAccess::makeAddress(factory, range), indexName, quantifier,
            FacadeAccess::makeExpr(factory, predicate)});
    }
    inline analyzer::symbolic::detail::ExprHandle makeMaxMinOverRangeForTest(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::AddrHandle range,
        std::string_view indexName,
        analyzer::symbolic::RangeExtremum extremum,
        analyzer::symbolic::SourcePoint fromPoint) {
        return FacadeAccess::exprHandle(analyzer::symbolic::MaxMinOverRangeExpr{
            FacadeAccess::makeAddress(factory, range), indexName, extremum,
            std::move(fromPoint)});
    }
    inline analyzer::symbolic::detail::ExprHandle makeMaxMinOverRangeForTest(
        analyzer::symbolic::ExprFactory &factory,
        analyzer::symbolic::detail::AddrHandle range,
        std::string_view indexName,
        analyzer::symbolic::RangeExtremum extremum,
        analyzer::symbolic::detail::ExprHandle body,
        analyzer::symbolic::SourcePoint fromPoint) {
        return FacadeAccess::exprHandle(analyzer::symbolic::MaxMinOverRangeExpr{
            FacadeAccess::makeAddress(factory, range), indexName, extremum,
            FacadeAccess::makeExpr(factory, body), std::move(fromPoint)});
    }
    analyzer::symbolic::detail::ExprHandle getReturnExprOfFirstPath(
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
        analyzer::symbolic::detail::AddrHandle makeVariableAddr(unsigned int id);
        analyzer::symbolic::detail::AddrHandle makeRangeAddr(
            unsigned int id,
            analyzer::symbolic::detail::ExprHandle offset,
            std::optional<analyzer::symbolic::detail::ExprHandle> len,
            std::optional<analyzer::symbolic::SourcePoint> fromPoint = std::nullopt);
        analyzer::symbolic::detail::ExprHandle makeSymbolValue(
            unsigned int id,
            std::optional<analyzer::symbolic::SourcePoint> fromPoint = std::nullopt);
        analyzer::symbolic::detail::AddrHandle makeSimpleSymbolAddr(
            unsigned int id,
            std::optional<analyzer::symbolic::SourcePoint> fromPoint = std::nullopt);
        analyzer::symbolic::detail::AddrHandle makePointAddr(unsigned int id, std::uint64_t off);
        ::testing::AssertionResult ExpectReadEqAt(analyzer::MemoryModel &mm,
                                                  unsigned id,
                                                  uint64_t off,
                                                  analyzer::symbolic::detail::ExprHandle expected);
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
