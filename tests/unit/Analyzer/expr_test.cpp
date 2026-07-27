// tests/unit/SpecGenerator/expr_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <clang/AST/AST.h>
#include <clang/AST/Decl.h>
#include <limits>
#include <type_traits>

#include "ASTExtractor.h"
#include "Context/context.h"
#include "Symbolic/aggregateExpr.h"
#include "Symbolic/detail/aggregateViews.h"
#include "Symbolic/detail/exprViews.h"
#include "Symbolic/detail/facadeAccess.h"
#include "Symbolic/detail/factoryInternals.h"
#include "Symbolic/expr.h"
#include "testHelper.h"

using namespace clang;
using namespace std;

using ::testing::HasSubstr;

namespace acslg::test::unit::analyzer {
    using namespace acslg::analyzer;
    using namespace utils;

    static symbolic::detail::ExprHandle handle(const symbolic::Expr &expression) {
        return symbolic::detail::FacadeAccess::exprHandle(expression);
    }

    static symbolic::detail::AddrHandle handle(const symbolic::Addr &address) {
        return symbolic::detail::FacadeAccess::addressHandle(address);
    }

    static symbolic::detail::AddrHandle handle(const symbolic::AddressBox &address) {
        return symbolic::detail::FacadeAccess::addressBoxHandle(address);
    }

    template <typename T>
    auto handle(const T &internalValue) -> decltype(internalValue.handle()) {
        return internalValue.handle();
    }

    template <typename T>
    concept HasLegacyClassof = requires(const symbolic::detail::SymbolicExprNode *expr) {
        T::classof(expr);
    };

    template <typename T>
    concept HasFacadeNodeAccess = requires(const T &value) {
        *value;
        value.operator->();
    };

    template <typename T>
    concept HasRawGet = requires(const T &value) {
        value.get();
    };

    template <typename T>
    concept CompleteType = requires {
        sizeof(T);
    };

    template <typename T>
    concept HasHandleIsa = requires(const T &value) {
        value.template isa<symbolic::detail::SymbolicExprNode>();
    };

    template <typename T>
    concept HasHandleDynCast = requires(const T &value) {
        value.template dyn_cast<symbolic::detail::SymbolicExprNode>();
    };

    template <typename T>
    concept HasHandleCast = requires(const T &value) {
        value.template cast<symbolic::detail::SymbolicExprNode>();
    };

    static_assert(std::is_constructible_v<symbolic::LiteralExpr,
                                          symbolic::ExprFactory &,
                                          int64_t>);
    static_assert(std::is_copy_constructible_v<symbolic::detail::LiteralExprView>);
    static_assert(std::is_copy_constructible_v<symbolic::detail::UnaryExprView>);
    static_assert(std::is_copy_constructible_v<symbolic::detail::BinaryExprView>);
    static_assert(std::is_same_v<decltype(std::declval<const symbolic::UnaryExpr &>().operand()),
                                 symbolic::Expr>);
    static_assert(std::is_same_v<decltype(std::declval<const symbolic::BinaryExpr &>().left()),
                                 symbolic::Expr>);
    static_assert(std::is_same_v<decltype(std::declval<const symbolic::BinaryExpr &>().right()),
                                 symbolic::Expr>);
    static_assert(std::is_copy_constructible_v<symbolic::detail::SumOverRangeView>);
    static_assert(std::is_copy_constructible_v<symbolic::detail::QuantifierOverRangeView>);
    static_assert(std::is_copy_constructible_v<symbolic::detail::MaxMinOverRangeView>);
    static_assert(std::is_constructible_v<symbolic::SumOverRangeExpr,
                                          const symbolic::Addr &,
                                          std::string_view,
                                          symbolic::SourcePoint>);
    static_assert(std::is_constructible_v<symbolic::QuantifierOverRangeExpr,
                                          const symbolic::Addr &,
                                          std::string_view,
                                          symbolic::RangeQuantifier,
                                          const symbolic::Expr &>);
    static_assert(std::is_constructible_v<symbolic::MaxMinOverRangeExpr,
                                          const symbolic::Addr &,
                                          std::string_view,
                                          symbolic::RangeExtremum,
                                          const symbolic::Expr &,
                                          symbolic::SourcePoint>);
    static_assert(std::is_same_v<decltype(std::declval<const symbolic::SumOverRangeExpr &>().range()),
                                 symbolic::SymbolAddress>);
    static_assert(std::is_same_v<decltype(std::declval<const symbolic::QuantifierOverRangeExpr &>()
                                              .predicate()),
                                 symbolic::Expr>);
    static_assert(std::is_same_v<decltype(std::declval<const symbolic::MaxMinOverRangeExpr &>().body()),
                                 symbolic::Expr>);
    static_assert(!std::is_constructible_v<symbolic::SumOverRangeExpr,
                                           symbolic::ExprFactory &,
                                           symbolic::detail::AddrHandle,
                                           std::string_view,
                                           symbolic::SourcePoint>);
    static_assert(!std::is_constructible_v<symbolic::QuantifierOverRangeExpr,
                                           symbolic::ExprFactory &,
                                           symbolic::detail::AddrHandle,
                                           std::string_view,
                                           symbolic::RangeQuantifier,
                                           symbolic::detail::ExprHandle>);
    static_assert(!std::is_constructible_v<symbolic::MaxMinOverRangeExpr,
                                           symbolic::ExprFactory &,
                                           symbolic::detail::AddrHandle,
                                           std::string_view,
                                           symbolic::RangeExtremum,
                                           symbolic::detail::ExprHandle,
                                           symbolic::SourcePoint>);
    static_assert(std::is_copy_constructible_v<symbolic::detail::StructureView>);
    static_assert(std::is_copy_constructible_v<symbolic::detail::SymbolValueView>);
    static_assert(std::is_same_v<decltype(std::declval<const symbolic::StructureExpr &>().field(0)),
                                 symbolic::Expr>);
    static_assert(std::is_same_v<decltype(std::declval<const symbolic::SymbolValueExpr &>().from()),
                                 symbolic::Addr>);
    static_assert(std::is_same_v<decltype(std::declval<const symbolic::FieldAddress &>().base()),
                                 symbolic::Addr>);
    static_assert(
        !std::is_convertible_v<symbolic::AddressBox &, symbolic::detail::AddressNode &>);
    static_assert(
        !std::is_convertible_v<const symbolic::AddressBox &, const symbolic::detail::AddressNode &>);
    static_assert(!std::is_constructible_v<symbolic::Expr,
                                           const symbolic::detail::SymbolicExprNode &>);
    static_assert(!std::is_constructible_v<symbolic::Addr,
                                           const symbolic::detail::AddressNode &>);
    static_assert(!HasRawGet<symbolic::detail::ExprHandle>);
    static_assert(!HasRawGet<symbolic::detail::AddrHandle>);
    static_assert(!CompleteType<symbolic::detail::AddressNode>);
    static_assert(!HasHandleIsa<symbolic::detail::ExprHandle>);
    static_assert(!HasHandleIsa<symbolic::detail::AddrHandle>);
    static_assert(!HasHandleDynCast<symbolic::detail::ExprHandle>);
    static_assert(!HasHandleDynCast<symbolic::detail::AddrHandle>);
    static_assert(!HasHandleCast<symbolic::detail::ExprHandle>);
    static_assert(!HasHandleCast<symbolic::detail::AddrHandle>);
    static_assert(!std::is_constructible_v<symbolic::detail::ExprHandle,
                                           const symbolic::detail::SymbolicExprNode *>);
    static_assert(!std::is_constructible_v<symbolic::detail::AddrHandle,
                                           const symbolic::detail::AddressNode *>);
    static_assert(std::is_same_v<
                  decltype(static_cast<symbolic::Expr (symbolic::Expr::*)(
                               const symbolic::ExprSubstitutions &) const>(
                      &symbolic::Expr::substituteValues)),
                  symbolic::Expr (symbolic::Expr::*)(const symbolic::ExprSubstitutions &) const>);
    static_assert(
        std::is_same_v<decltype(&symbolic::Expr::substitutePath),
                       symbolic::Expr (symbolic::Expr::*)(const Path &,
                                                          const symbolic::SourcePoint &) const>);
    static_assert(
        std::is_same_v<decltype(&symbolic::Expr::substituteRangeIndex),
                       symbolic::Expr (symbolic::Expr::*)(const symbolic::SymbolAddrBaseInfo &,
                                                          const symbolic::Expr &) const>);
    static_assert(std::is_same_v<decltype(&symbolic::Expr::sourceAddress),
                                 std::optional<symbolic::Addr> (symbolic::Expr::*)() const>);
    static_assert(std::is_same_v<decltype(&symbolic::Expr::tryAsAddress),
                                 std::optional<symbolic::Addr> (symbolic::Expr::*)() const>);
    static_assert(std::is_same_v<decltype(&symbolic::Expr::isFrom),
                                 bool (symbolic::Expr::*)(const symbolic::Addr &,
                                                          const symbolic::SourcePoint &) const>);
    static_assert(
        std::is_same_v<
            decltype(static_cast<symbolic::Expr (
                             *)(clang::QualType, const symbolic::Addr &, symbolic::SourcePoint)>(
                &symbolic::Expr::symbol)),
            symbolic::Expr (*)(clang::QualType, const symbolic::Addr &, symbolic::SourcePoint)>);
    static_assert(std::is_same_v<
                  decltype(static_cast<symbolic::Expr (*)(clang::QualType, symbolic::SourcePoint)>(
                      &symbolic::Expr::symbol)),
                  symbolic::Expr (*)(clang::QualType, symbolic::SourcePoint)>);
    static_assert(!HasLegacyClassof<symbolic::detail::SymbolicExprNode>);
    static_assert(!HasLegacyClassof<symbolic::detail::AddressNode>);
    static_assert(std::is_invocable_v<symbolic::AddressBoxHash,
                                      const symbolic::AddressBox &>);
    static_assert(!std::is_invocable_v<symbolic::AddressBoxHash,
                                       const symbolic::detail::AddressNode &>);
    static_assert(!std::is_invocable_v<symbolic::AddressBoxEq,
                                       const symbolic::AddressBox &,
                                       const symbolic::detail::AddressNode &>);
    static_assert(!HasFacadeNodeAccess<symbolic::Expr>);
    static_assert(!HasFacadeNodeAccess<symbolic::Addr>);
    static_assert(!HasFacadeNodeAccess<symbolic::detail::ExprHandle>);
    static_assert(!HasFacadeNodeAccess<symbolic::detail::AddrHandle>);

    namespace {
        const Stmt *nthStmtInBody(const FunctionDecl *FD, unsigned n) {
            if (!FD || !FD->hasBody())
                return nullptr;
            const Stmt *Body = FD->getBody();
            if (auto CS = llvm::dyn_cast<CompoundStmt>(Body)) {
                if (n < CS->size()) {
                    auto it = CS->body_begin();
                    advance(it, n);
                    return *it;
                }
            }
            return nullptr;
        }

        symbolic::detail::ExprHandle makeStructureWithFacade(
            symbolic::ExprFactory &factory,
            clang::QualType type,
            ::acslg::utils::not_null<const clang::VarDecl *> var,
            symbolic::SourcePoint point) {
            auto *record = type->getAsRecordDecl()->getDefinition();
            auto &layout = record->getASTContext().getASTRecordLayout(record);
            auto from    = symbolic::Addr::variable(var);
            return structureHandle(factory, record, layout, handle(from), point);
        }

        class SourcePointTest : public ::testing::Test {
          protected:
            ASTExtractor e;

            void SetUp() override {
                static const char *kCode = R"cpp(
      int glob = 0;

      void f() {
        int a = 0;
        a = a + 1;
      }

      void g() {
        int x = 42;
      }
    )cpp";
                e.init(kCode);
            }
        };

    } // namespace

    TEST_F(SourcePointTest, FromFuncDeclBefore_BasicOrdering) {
        const FunctionDecl *F = e.findFunc("f");
        const FunctionDecl *G = e.findFunc("g");
        ASSERT_NE(F, nullptr);
        ASSERT_NE(G, nullptr);

        auto spF = symbolic::SourcePoint::fromFuncDecl(F, e.getSourceManager(), e.getLangOptions());
        auto spG = symbolic::SourcePoint::fromFuncDecl(G, e.getSourceManager(), e.getLangOptions());

        EXPECT_TRUE(spF < spG);
        EXPECT_FALSE(spG < spF);

        auto spF2 =
            symbolic::SourcePoint::fromFuncDecl(F, e.getSourceManager(), e.getLangOptions());
        EXPECT_TRUE(spF == spF2);
        EXPECT_FALSE(spF < spF2);
    }

    TEST_F(SourcePointTest, FromStmtBeforeAfter_LocalOrdering) {
        const FunctionDecl *F = e.findFunc("f");
        ASSERT_NE(F, nullptr);

        const Stmt *s0 = nthStmtInBody(F, 0); // int a = 0;
        const Stmt *s1 = nthStmtInBody(F, 1); // a = a + 1;
        ASSERT_NE(s0, nullptr);
        ASSERT_NE(s1, nullptr);

        auto before0 =
            symbolic::SourcePoint::fromStmtBefore(s0, e.getSourceManager(), e.getLangOptions());
        auto after0 =
            symbolic::SourcePoint::fromStmtAfter(s0, e.getSourceManager(), e.getLangOptions());
        auto before1 =
            symbolic::SourcePoint::fromStmtBefore(s1, e.getSourceManager(), e.getLangOptions());
        auto after1 =
            symbolic::SourcePoint::fromStmtAfter(s1, e.getSourceManager(), e.getLangOptions());

        EXPECT_TRUE(before0 < after0);
        EXPECT_TRUE(before1 < after1);

        EXPECT_TRUE(after0 < before1);

        auto before0_again =
            symbolic::SourcePoint::fromStmtBefore(s0, e.getSourceManager(), e.getLangOptions());
        EXPECT_TRUE(before0 == before0_again);
    }

    TEST(ExprFactoryScopeTest, CurrentRequiresActiveScope) {
        ASSERT_FALSE(symbolic::ExprFactoryScope::hasCurrent());
        ASSERT_DEATH({ (void)symbolic::ExprFactoryScope::current(); }, "");
    }

    TEST(ExprFactoryScopeTest, UsesFactoryOwnedByACSLGContext) {
        ASTExtractor e;
        e.init("int f(void) { return 0; }");
        context::ACSLGContext acslContext(e.getASTContext());

        symbolic::ExprFactoryScope outer(acslContext.getExprFactory());
        EXPECT_TRUE(symbolic::ExprFactoryScope::hasCurrent());
        EXPECT_EQ(&symbolic::ExprFactoryScope::current(), &acslContext.getExprFactory());

        symbolic::ExprFactory nestedFactory;
        {
            symbolic::ExprFactoryScope nested(nestedFactory);
            EXPECT_EQ(&symbolic::ExprFactoryScope::current(), &nestedFactory);
        }

        EXPECT_EQ(&symbolic::ExprFactoryScope::current(), &acslContext.getExprFactory());
    }

    TEST_F(SourcePointTest, HashConsistentWithEquality) {
        const FunctionDecl *F = e.findFunc("f");
        ASSERT_NE(F, nullptr);
        const Stmt *s0 = nthStmtInBody(F, 0);
        ASSERT_NE(s0, nullptr);

        auto before0 =
            symbolic::SourcePoint::fromStmtBefore(s0, e.getSourceManager(), e.getLangOptions());
        auto before0_bis =
            symbolic::SourcePoint::fromStmtBefore(s0, e.getSourceManager(), e.getLangOptions());

        EXPECT_TRUE(before0 == before0_bis);
        EXPECT_EQ(before0.hash(), before0_bis.hash());

        struct WrapperHash {
            size_t operator()(const symbolic::SourcePoint &sp) const noexcept { return sp.hash(); }
        };
        struct WrapperEq {
            bool operator()(const symbolic::SourcePoint &a,
                            const symbolic::SourcePoint &b) const noexcept {
                return a == b;
            }
        };

        unordered_set<symbolic::SourcePoint, WrapperHash, WrapperEq> S;
        S.insert(before0);
        S.insert(before0_bis);

        EXPECT_EQ(S.size(), 1u);
    }

    TEST_F(SourcePointTest, DumpIsNonEmptyAndLooksLikeLocation) {
        const FunctionDecl *F = e.findFunc("f");
        ASSERT_NE(F, nullptr);
        auto sp  = symbolic::SourcePoint::fromFuncDecl(F, e.getSourceManager(), e.getLangOptions());
        string d = sp.dump();

        EXPECT_FALSE(d.empty());
        EXPECT_THAT(d, HasSubstr(":"));
    }

    namespace {
        using namespace symbolic;
        using symbolic::detail::ExprHandle;
        ExprHandle makeConstU64(uint64_t value) {
            return literalHandle(ExprFactoryScope::current(), value);
        }

        template <class ExprPtr> ExprHandle internForTest(const ExprPtr &expr) {
            return importExprHandle(ExprFactoryScope::current(), expr);
        }

        ExprHandle literalHandleForTest(uint64_t value) {
            return literalHandle(ExprFactoryScope::current(), value);
        }

        ExprHandle makeAdd(ExprHandle a, ExprHandle b) {
            auto &factory = ExprFactoryScope::current();
            return binaryHandle(factory, importExprHandle(factory, a), BinaryOp::Add,
                                importExprHandle(factory, b));
        }

        struct SubstituteTest : public FixtureWithCode {
          protected:
            SubstituteTest()
                : acslContext(e.getASTContext()),
                  path(make_unique<Path>(acslContext, defaultPoint)),
                  mm(path->getMutMemoryState()) {}

            SourcePoint getSourcePoint(unsigned int id) {
                auto newFuncDecl = getFuncDecl(id);
                return SourcePoint::fromFuncDecl(newFuncDecl, e.getSourceManager(),
                                                 e.getLangOptions());
            }

          private:
            context::ACSLGContext acslContext;

          protected:
            unique_ptr<Path> path;
            MemoryModel &mm;
        };

    } // namespace

    TEST_F(SubstituteTest, VarWithMatchingFromIsReplacedFromLoopEntry) {
        auto var0Addr = makeVariableAddr(0);
        mm.write(FacadeAddrForTest{var0Addr}, FacadeExprForTest{internForTest(makeConstU64(42))});

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto point   = getSourcePoint(0);
        auto varNode = makeSymbolValue(0, point);
        auto result  = substitutePathForTest(factory, varNode, *path, point);

        ASSERT_EQ(result, literalHandle(factory, uint64_t{42}));
    }

    TEST_F(SubstituteTest, ScopedVarReplacementImportsThroughFactory) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto var0Addr = makeVariableAddr(0);
        mm.write(FacadeAddrForTest{var0Addr}, FacadeExprForTest{internForTest(makeConstU64(42))});

        auto point   = getSourcePoint(0);
        auto varNode = makeSymbolValue(0, point);
        auto result  = substitutePathForTest(factory, varNode, *path, point);

        EXPECT_EQ(result, literalHandle(factory, uint64_t{42}));
    }

    TEST_F(SubstituteTest, ExprFacadeSubstitutesPathThroughFactory) {
        auto var0Addr = makeVariableAddr(0);
        mm.write(FacadeAddrForTest{var0Addr}, FacadeExprForTest{internForTest(makeConstU64(42))});

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto point = getSourcePoint(0);
        auto varNode = makeSymbolValue(0, point);
        auto result  = FacadeExprForTest{factory, varNode}.substitutePath(*path, point);

        EXPECT_EQ(handle(result), literalHandle(factory, uint64_t{42}));
    }

    TEST_F(SubstituteTest, VarWithDifferentFromIsKeptUnchanged) {
        auto var0Addr = makeVariableAddr(0);
        mm.write(FacadeAddrForTest{var0Addr}, FacadeExprForTest{internForTest(makeConstU64(7))});

        auto point   = getSourcePoint(0);
        auto varNode = makeSymbolValue(0, point);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto exprBefore = importExprHandle(factory, varNode);
        auto result     = substitutePathForTest(factory, varNode, *path, getSourcePoint(42));

        ASSERT_TRUE(result.structurallyEqual(exprBefore));
    }

    TEST_F(SubstituteTest, CompositeExprIsSubstitutedRecursively) {
        // loop-entry：g1 -> 1, g2 -> 2
        mm.write(FacadeAddrForTest{makeVariableAddr(1)},
                 FacadeExprForTest{internForTest(makeConstU64(1))});
        mm.write(FacadeAddrForTest{makeVariableAddr(2)},
                 FacadeExprForTest{internForTest(makeConstU64(2))});

        auto point = getSourcePoint(0);

        // expr = Var(g1, point) + Var(g2, point)
        auto aVar = makeSymbolValue(1, point);
        auto bVar = makeSymbolValue(2, point);
        auto expr = makeAdd(std::move(aVar), std::move(bVar));

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto expected = makeAdd(literalHandle(factory, uint64_t{1}), literalHandle(factory, uint64_t{2}));
        auto result   = substitutePathForTest(factory, expr, *path, point);
        ASSERT_TRUE(result.structurallyEqual(expected));
    }

    TEST_F(SubstituteTest, PathSubstitutionHandleRebuildsCompositeExpression) {
        mm.write(FacadeAddrForTest{makeVariableAddr(1)},
                 FacadeExprForTest{internForTest(makeConstU64(1))});
        mm.write(FacadeAddrForTest{makeVariableAddr(2)},
                 FacadeExprForTest{internForTest(makeConstU64(2))});

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto point = getSourcePoint(0);
        auto aVar = makeSymbolValue(1, point);
        auto bVar = makeSymbolValue(2, point);
        auto expr = makeAdd(std::move(aVar), std::move(bVar));

        auto result   = substitutePathForTest(factory, expr, *path, point);
        auto expected = binaryHandle(factory, literalHandle(factory, uint64_t{1}),
                                       symbolic::BinaryOp::Add,
                                       literalHandle(factory, uint64_t{2}));

        EXPECT_EQ(result, expected);
    }

    TEST_F(SubstituteTest, SymbolAddrResolvedBaseAndOffsetApplied) {
        auto originAddr = makeVariableAddr(3);
        auto realAddr   = makeSimpleSymbolAddr(4);
        mm.write(FacadeAddrForTest{originAddr}, FacadeExprForTest{realAddr.asExpr()});

        // Var(g5) = 3
        mm.write(FacadeAddrForTest{makeVariableAddr(5)},
                 FacadeExprForTest{internForTest(makeConstU64(3))});

        auto point = getSourcePoint(0);

        // symAddr: base=origin(g3), offset=(Var(g5,point) + 4), from=point
        auto vVar   = makeSymbolValue(5, point);
        auto offset = makeAdd(vVar, literalHandle(ExprFactoryScope::current(), uint64_t{4}));
        auto sym = makeRangeAddr(/*origin id*/ 3, internForTest(offset), std::nullopt, point);

        // expect: realAddr (g4) + 7
        auto expected = simplifyForTest(symbolic::ExprFactoryScope::current(),
                                        makePointAddr(/*real id*/ 4, /*off*/ 7).asExpr());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto result = substitutePathForTest(factory, sym.asExpr(), *path, point);
        ASSERT_TRUE(simplifyForTest(factory, result).structurallyEqual(expected));
    }

    TEST_F(SubstituteTest, SymbolAddrUnresolvedReturnsImportedHandle) {
        auto sym = makeSimpleSymbolAddr(/*origin id*/ 6);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto result = substitutePathForTest(factory, sym.asExpr(), *path, defaultPoint);
        ASSERT_TRUE(result.structurallyEqual(sym.asExpr()));
    }

    TEST_F(SubstituteTest, NonSymbolAddrReturnsImportedHandle) {
        auto varAddr = makeVariableAddr(7);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto result = substitutePathForTest(factory, varAddr.asExpr(), *path, defaultPoint);
        ASSERT_TRUE(result.structurallyEqual(varAddr.asExpr()));
    }

    TEST_F(SubstituteTest, FromPointMismatchReturnsUnchangedSymbolAddr) {
        auto originAddr = makeVariableAddr(8);
        auto realAddr   = makeSimpleSymbolAddr(9);
        mm.write(FacadeAddrForTest{originAddr}, FacadeExprForTest{realAddr.asExpr()});

        auto point = getSourcePoint(0);
        auto offset = makeConstU64(1);
        auto sym = makeRangeAddr(/*origin id*/ 8, internForTest(offset), std::nullopt, point);

        auto otherPoint = getSourcePoint(1);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto result = substitutePathForTest(factory, sym.asExpr(), *path, otherPoint);
        ASSERT_TRUE(result.structurallyEqual(sym.asExpr()));
    }

    TEST_F(SubstituteTest, ScopedFromPointMismatchImportsUnchangedSymbolAddr) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto originAddr = makeVariableAddr(8);
        auto realAddr   = makeSimpleSymbolAddr(9);
        mm.write(FacadeAddrForTest{originAddr}, FacadeExprForTest{realAddr.asExpr()});

        auto point = getSourcePoint(0);
        auto offset = makeConstU64(1);
        auto sym = makeRangeAddr(/*origin id*/ 8, internForTest(offset), std::nullopt, point);

        auto otherPoint = getSourcePoint(1);
        auto result     = substitutePathForTest(factory, sym.asExpr(), *path, otherPoint);
        auto resultAddr = symbolic::detail::AddrHandle::tryFrom(result);
        ASSERT_TRUE(resultAddr);

        EXPECT_EQ(importAddressHandle(factory, *resultAddr), importAddressHandle(factory, sym));
    }

    TEST_F(SubstituteTest, ScopedNoBaseSymbolAddrSubstitutionUsesFactory) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto var = getVarDecl(0);
        auto point = getSourcePoint(0);
        auto symHandle = symbolAddressHandle(factory, var->getType(), std::nullopt, point);

        auto result     = substitutePathForTest(factory, symHandle.asExpr(), *path, point);
        auto resultAddr = symbolic::detail::AddrHandle::tryFrom(result);
        ASSERT_TRUE(resultAddr);

        EXPECT_EQ(importAddressHandle(factory, *resultAddr), symHandle);
    }

    TEST_F(SubstituteTest, ResolvedValueNotAddressShouldError) {
        auto originAddr = makeVariableAddr(10);
        mm.write(FacadeAddrForTest{originAddr}, FacadeExprForTest{internForTest(makeConstU64(5))});

        auto point = getSourcePoint(0);
        auto offset = makeConstU64(0);
        auto sym = makeRangeAddr(/*origin id*/ 10, internForTest(offset), std::nullopt, point);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        ASSERT_DEATH(substitutePathForTest(factory, sym.asExpr(), *path, point), "");
        SUCCEED();
    }

    class GetACSLTest : public FixtureWithCode {};

    // Test literal expressions for correct ACSL output.
    TEST_F(GetACSLTest, Literal_GetACSL) {
        ACSLConfig config;
        config.noStateLabelFunctionAt = true;

        symbolic::ExprFactory factory;

        // Boolean literal
        auto litTrue = literalHandle(factory, true);
        auto resTrue = litTrue.getACSL(config);
        ASSERT_TRUE(resTrue);
        EXPECT_EQ(resTrue.value().first, "true");
        EXPECT_TRUE(resTrue.value().second.empty());

        // Integer literal
        auto litInt = literalHandle(factory, 123);
        auto resInt = litInt.getACSL(config);
        ASSERT_TRUE(resInt);
        EXPECT_EQ(resInt.value().first, "123");
        EXPECT_TRUE(resInt.value().second.empty());
    }

    // Test binary addition and operator precedence/parentheses.
    TEST_F(GetACSLTest, BinaryOp_AdditionAndPrecedence) {
        ACSLConfig config;
        config.noStateLabelFunctionAt = true;
        auto &factory = symbolic::ExprFactoryScope::current();

        // Simple addition: 5 + 3
        auto exprSimple = binaryHandle(factory, literalHandle(factory, 5), BinaryOp::Add,
                                         literalHandle(factory, 3));
        auto resSimple  = exprSimple.getACSL(config);
        ASSERT_TRUE(resSimple);
        EXPECT_EQ(resSimple.value().first, "5 + 3");

        // Nested addition (left-child nested): (1 + 2) + 3 -> "1 + 2 + 3"
        auto innerLeft = binaryHandle(factory, literalHandle(factory, 1), BinaryOp::Add,
                                        literalHandle(factory, 2));
        auto exprLeft =
            binaryHandle(factory, innerLeft, BinaryOp::Add, literalHandle(factory, 3));
        auto resLeft = exprLeft.getACSL(config);
        ASSERT_TRUE(resLeft);
        EXPECT_EQ(resLeft.value().first, "1 + 2 + 3");

        // Nested addition (right-child nested): 1 + (2 + 3) -> "1 + (2 + 3)"
        auto innerRight = binaryHandle(factory, literalHandle(factory, 2), BinaryOp::Add,
                                         literalHandle(factory, 3));
        auto exprRight =
            binaryHandle(factory, literalHandle(factory, 1), BinaryOp::Add, innerRight);
        auto resRight = exprRight.getACSL(config);
        ASSERT_TRUE(resRight);
        EXPECT_EQ(resRight.value().first, "1 + (2 + 3)");
    }

    // Test unary operators: prefix and postfix increment.
    TEST_F(GetACSLTest, UnaryOp_PreAndPostIncrement) {
        ACSLConfig config;
        config.noStateLabelFunctionAt = true;

        // Use variable ID 0 and 1 to get actual names from the AST.
        auto var0 = getVarDecl(0);
        ASSERT_NE(var0, nullptr);
        std::string name0 = var0->getNameAsString();
        auto symVal0      = makeSymbolValue(0);
        auto &factory     = symbolic::ExprFactoryScope::current();

        // Prefix increment (e.g., ++x)
        auto preInc = unaryHandle(factory, UnaryOp::PreInc, importExprHandle(factory, symVal0));
        auto resPre = preInc.getACSL(config);
        ASSERT_TRUE(resPre);
        EXPECT_EQ(resPre.value().first, "++" + name0);

        auto var1 = getVarDecl(1);
        ASSERT_NE(var1, nullptr);
        std::string name1 = var1->getNameAsString();
        auto symVal1      = makeSymbolValue(1);

        // Postfix increment (e.g., x++)
        auto postInc = unaryHandle(factory, UnaryOp::PostInc, importExprHandle(factory, symVal1));
        auto resPost = postInc.getACSL(config);
        ASSERT_TRUE(resPost);
        EXPECT_EQ(resPost.value().first, name1 + "++");
    }

    // Test SymbolValue (pointer dereference): should print the variable name.
    TEST_F(GetACSLTest, SymbolValue_GetACSL) {
        ACSLConfig config;
        config.noStateLabelFunctionAt = true;

        auto var0 = getVarDecl(0);
        ASSERT_NE(var0, nullptr);
        std::string name0 = var0->getNameAsString();

        auto symVal = makeSymbolValue(0);
        auto res    = symVal.getACSL(config);
        ASSERT_TRUE(res);
        EXPECT_EQ(res.value().first, name0);
        EXPECT_TRUE(res.value().second.empty());
    }

    // Test SymbolAddress (pointer) ACSL and ACSLOfValue.
    TEST_F(GetACSLTest, SymbolAddress_GetACSL_And_GetACSLOfValue) {
        ACSLConfig config;
        config.noStateLabelFunctionAt = true;

        auto baseVar = getVarDecl(0);
        ASSERT_NE(baseVar, nullptr);
        std::string baseName = baseVar->getNameAsString();

        // Case 1: offset = 2
        auto addr2 = makeRangeAddr(0, literalHandleForTest(2), std::nullopt);
        // ACSL should be "baseName + 2"
        auto resACSL = addr2.asExpr().getACSL(config);
        ASSERT_TRUE(resACSL);
        EXPECT_EQ(resACSL.value().first, baseName + " + 2");
        EXPECT_TRUE(resACSL.value().second.empty());
        // ACSLOfValue should be "baseName[2]"
        auto resVal = addr2.getACSLOfValue(config);
        ASSERT_TRUE(resVal);
        EXPECT_EQ(resVal.value().first, baseName + "[2]");
        EXPECT_TRUE(resVal.value().second.empty());

        // Case 2: offset = 0 (no offset effectively)
        auto addr0 = makeRangeAddr(0, literalHandleForTest(0), std::nullopt);
        // ACSL should be just "baseName"
        auto resACSL0 = addr0.asExpr().getACSL(config);
        ASSERT_TRUE(resACSL0);
        EXPECT_EQ(resACSL0.value().first, baseName);
        EXPECT_TRUE(resACSL0.value().second.empty());
        // ACSLOfValue should be "*baseName"
        auto resVal0 = addr0.getACSLOfValue(config);
        ASSERT_TRUE(resVal0);
        EXPECT_EQ(resVal0.value().first, "*" + baseName);
        EXPECT_TRUE(resVal0.value().second.empty());
    }

    // Test ACSLOfValue with a range length.
    TEST_F(GetACSLTest, SymbolAddress_WithRangeLength) {
        ACSLConfig config;
        config.noStateLabelFunctionAt = true;

        auto baseVar = getVarDecl(0);
        ASSERT_NE(baseVar, nullptr);
        std::string baseName = baseVar->getNameAsString();

        // offset = 5, length = 3 => [5 .. 7]
        auto addrRange =
            makeRangeAddr(0, literalHandleForTest(5), literalHandleForTest(3));
        auto resRange = addrRange.getACSLOfValue(config);
        ASSERT_TRUE(resRange);
        EXPECT_EQ(resRange.value().first, baseName + "[5 .. 7]");
        EXPECT_TRUE(resRange.value().second.empty());
    }

    TEST_F(GetACSLTest, SymbolAddressRightBoundUsesFactoryScope) {
        auto addrRange =
            makeRangeAddr(0, literalHandleForTest(5), literalHandleForTest(3));

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        symbolic::detail::SymbolAddressView addrRangeView{addrRange};
        auto rightBound = addrRangeView.rightBound();
        ASSERT_TRUE(rightBound);

        auto expected =
            binaryHandle(factory, importExprHandle(factory, addrRangeView.offset()), BinaryOp::Add,
                         importExprHandle(factory, addrRangeView.length().value()));
        EXPECT_EQ(rightBound.value(), expected);
        EXPECT_EQ(addrRangeView.rightBound(), rightBound);

        symbolic::detail::BinaryExprView rightBoundView{rightBound.value()};
        EXPECT_EQ(rightBoundView.left(), importExprHandle(factory, addrRangeView.offset()));
        EXPECT_EQ(rightBoundView.right(),
                  importExprHandle(factory, addrRangeView.length().value()));
    }

    // Test usage of \\at(...) when predefinedLabels is set.
    TEST_F(GetACSLTest, SymbolAddress_predefinedLabels) {
        ACSLConfig config;
        // Create a SourcePoint and use it as old label
        const FunctionDecl *F = getFuncDecl(0);
        ASSERT_NE(F, nullptr);
        SourcePoint sp = SourcePoint::fromFuncDecl(F, e.getSourceManager(), e.getLangOptions());
        config.predefinedLabels = {{sp, "Old"}};

        auto baseVar = getVarDecl(0);
        ASSERT_NE(baseVar, nullptr);
        std::string baseName = baseVar->getNameAsString();

        // Attach the source point to the address
        auto addr = makeRangeAddr(0, literalHandleForTest(2), std::nullopt, sp);
        auto resACSL = addr.asExpr().getACSL(config);
        ASSERT_TRUE(resACSL);
        EXPECT_EQ(resACSL.value().first, "\\at(" + baseName + ", Old) + 2");
        EXPECT_TRUE(resACSL.value().second.empty());
        auto resVal = addr.getACSLOfValue(config);
        ASSERT_TRUE(resVal);
        EXPECT_EQ(resVal.value().first, "\\at(" + baseName + ", Old)[2]");
    }

    // Test filtering out SourcePoints from ACSL output and usedPoints.
    TEST_F(GetACSLTest, SymbolAddress_SourcePointWhitelistFiltersAt) {
        ACSLConfig config;

        const FunctionDecl *F0 = getFuncDecl(0);
        ASSERT_NE(F0, nullptr);
        const FunctionDecl *F1 = getFuncDecl(1);
        ASSERT_NE(F1, nullptr);

        SourcePoint allowed =
            SourcePoint::fromFuncDecl(F0, e.getSourceManager(), e.getLangOptions());
        SourcePoint filtered =
            SourcePoint::fromFuncDecl(F1, e.getSourceManager(), e.getLangOptions());

        config.predefinedLabels                   = {{filtered, "Old"}};
        config.sourcePointOutputFilter.whitelist  = std::unordered_set<SourcePoint>{allowed};

        auto baseVar = getVarDecl(0);
        ASSERT_NE(baseVar, nullptr);
        std::string baseName = baseVar->getNameAsString();

        auto addr = makeRangeAddr(0, literalHandleForTest(2), std::nullopt, filtered);

        auto resACSL = addr.asExpr().getACSL(config);
        ASSERT_TRUE(resACSL);
        EXPECT_EQ(resACSL.value().first, baseName + " + 2");
        EXPECT_TRUE(resACSL.value().second.empty());

        auto resVal = addr.getACSLOfValue(config);
        ASSERT_TRUE(resVal);
        EXPECT_EQ(resVal.value().first, baseName + "[2]");
        EXPECT_TRUE(resVal.value().second.empty());
    }

    namespace {
        class CollisionExpr final : public symbolic::detail::SymbolicExprNode {
          public:
            explicit CollisionExpr(int id)
                : SymbolicExprNode(ExprKind::K_UnknownExpr,
                                   symbolic::ExprType{symbolic::ExprScalarKind::Void, 0}),
                  id_(id) {}

            std::string dump() const override { return "collision:" + std::to_string(id_); }

            bool equal(const symbolic::detail::SymbolicExprNode &other) const override {
                auto *rhs = dynamic_cast<const CollisionExpr *>(&other);
                return rhs != nullptr && rhs->id_ == id_;
            }

            std::size_t hash() const override { return 42; }

            UsedSet collectUsedSymbols() const override { return {selfHandle()}; }

            bool isLinear() const override { return true; }
            int getMaxDegree() const override { return 1; }

            Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
                const symbolic::detail::ExprHandleIndexMap &expressionIndexMap) const override {
                return Parma_Polyhedra_Library::Variable(expressionIndexMap.at(selfHandle()));
            }

          private:
            ::acslg::utils::expected<std::string, ACSLError> doGetACSL(
                const ACSLConfig &,
                std::unordered_set<symbolic::SourcePoint> &,
                std::optional<symbolic::SourcePoint>,
                unsigned,
                bool) const override {
                return dump();
            }

            int id_;
        };

    } // namespace

    TEST(ExprFacadeTest, StripSizeofFactorPreservesNodeIdentity) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto value   = symbolic::Expr::rangeIndex("count");
        auto factor  = symbolic::LiteralExpr{std::uint64_t{8}};
        auto product = factor * value;

        auto stripped  = symbolic::strip_sizeof_factor(product, 8);
        auto literal   = symbolic::strip_sizeof_factor(factor, 8);
        auto unchanged = symbolic::strip_sizeof_factor(product, 4);

        EXPECT_EQ(stripped, value);
        EXPECT_EQ(literal, symbolic::LiteralExpr{std::uint64_t{1}});
        EXPECT_EQ(unchanged, product);
        EXPECT_EQ(&stripped.factory(), &factory);
    }

    TEST(ExprFactoryTest, ReusesEqualNodesButSeparatesHashCollisions) {
        symbolic::ExprFactory factory;

        auto a = symbolic::detail::ExprFactoryInternals::intern(
            factory, std::make_unique<CollisionExpr>(1));
        auto b = symbolic::detail::ExprFactoryInternals::intern(
            factory, std::make_unique<CollisionExpr>(1));
        auto c = symbolic::detail::ExprFactoryInternals::intern(
            factory, std::make_unique<CollisionExpr>(2));

        EXPECT_EQ(a, b);
        EXPECT_NE(a, c);
        EXPECT_EQ(symbolic::detail::ExprFactoryInternals::size(factory), 2u);
    }

    TEST(ExprFactoryTest, ValueSubstitutionSeparatesHashCollisions) {
        symbolic::ExprFactory factory;
        auto first = symbolic::detail::ExprFactoryInternals::intern(
            factory, std::make_unique<CollisionExpr>(1));
        auto second = symbolic::detail::ExprFactoryInternals::intern(
            factory, std::make_unique<CollisionExpr>(2));
        auto firstReplacement  = literalHandle(factory, int64_t{11});
        auto secondReplacement = literalHandle(factory, int64_t{22});

        symbolic::ExprSubstitutions substitutions;
        substitutions.insertOrAssign(FacadeExprForTest{factory, first},
                                     FacadeExprForTest{factory, firstReplacement});
        substitutions.insertOrAssign(FacadeExprForTest{factory, second},
                                     FacadeExprForTest{factory, secondReplacement});

        EXPECT_EQ(substitutions.size(), 2u);
        EXPECT_EQ(handle(FacadeExprForTest{factory, first}.substituteValues(substitutions)),
                  firstReplacement);
        EXPECT_EQ(handle(FacadeExprForTest{factory, second}.substituteValues(substitutions)),
                  secondReplacement);
    }

    TEST(ExprFactoryTest, SymbolCollectionAndLinearizationSeparateHashCollisions) {
        symbolic::ExprFactory factory;
        auto firstHandle = symbolic::detail::ExprFactoryInternals::intern(
            factory, std::make_unique<CollisionExpr>(1));
        auto secondHandle = symbolic::detail::ExprFactoryInternals::intern(
            factory, std::make_unique<CollisionExpr>(2));
        FacadeExprForTest first{factory, firstHandle};
        FacadeExprForTest second{factory, secondHandle};

        auto [symbols, expressionIndexes] = symbolic::collectUsedSymbols(first, second);

        ASSERT_EQ(symbols.size(), 2u);
        ASSERT_EQ(expressionIndexes.size(), 2u);
        EXPECT_TRUE(symbols.contains(first));
        EXPECT_TRUE(symbols.contains(second));
        ASSERT_NE(expressionIndexes.at(first), expressionIndexes.at(second));

        auto firstLinear  = first.toLinearExpr(expressionIndexes);
        auto secondLinear = second.toLinearExpr(expressionIndexes);
        auto firstVariable =
            Parma_Polyhedra_Library::Variable(expressionIndexes.at(first));
        auto secondVariable =
            Parma_Polyhedra_Library::Variable(expressionIndexes.at(second));
        EXPECT_EQ(firstLinear.coefficient(firstVariable), 1);
        EXPECT_EQ(firstLinear.coefficient(secondVariable), 0);
        EXPECT_EQ(secondLinear.coefficient(firstVariable), 0);
        EXPECT_EQ(secondLinear.coefficient(secondVariable), 1);
    }

    TEST(ExprFactoryTest, ImportRejectsUnsupportedDynamicExpressionTypes) {
        symbolic::ExprFactory source;
        auto unsupported = symbolic::detail::ExprFactoryInternals::intern(
            source, std::make_unique<CollisionExpr>(1));
        symbolic::ExprFactory target;

        EXPECT_DEATH((void)importExprHandle(target, unsupported), "");
    }

    TEST(ExprFactoryTest, TypedBuildersReuseEqualLiteralAndOperationNodes) {
        symbolic::ExprFactory factory;

        auto oneA = literalHandle(factory, 1);
        auto oneB = literalHandle(factory, 1);
        auto two  = literalHandle(factory, 2);

        EXPECT_EQ(oneA, oneB);
        EXPECT_NE(oneA, two);
        auto oneView = symbolic::detail::LiteralExprView::tryFrom(oneA);
        ASSERT_TRUE(oneView.has_value());
        EXPECT_EQ(oneView->value(), 1);

        auto sumA =
            binaryHandle(factory, oneA, symbolic::BinaryOp::Add, two);
        auto sumB =
            binaryHandle(factory, oneB, symbolic::BinaryOp::Add, literalHandle(factory, 2));
        auto diff = binaryHandle(factory, oneA, symbolic::BinaryOp::Subtract, two);

        EXPECT_EQ(sumA, sumB);
        EXPECT_NE(sumA, diff);
        auto sumView = symbolic::detail::BinaryExprView::tryFrom(sumA);
        ASSERT_TRUE(sumView.has_value());
        EXPECT_EQ(sumView->operation(), symbolic::BinaryOp::Add);
        EXPECT_EQ(sumView->left(), oneA);
        EXPECT_EQ(sumView->right(), two);

        auto negA = unaryHandle(factory, symbolic::UnaryOp::Minus, oneA);
        auto negB = unaryHandle(factory, symbolic::UnaryOp::Minus, oneB);
        EXPECT_EQ(negA, negB);
        auto negView = symbolic::detail::UnaryExprView::tryFrom(negA);
        ASSERT_TRUE(negView.has_value());
        EXPECT_EQ(negView->operation(), symbolic::UnaryOp::Minus);
        EXPECT_EQ(negView->operand(), oneA);

        EXPECT_FALSE(symbolic::detail::LiteralExprView::tryFrom(sumA).has_value());
        EXPECT_FALSE(symbolic::detail::UnaryExprView::tryFrom(oneA).has_value());
        EXPECT_FALSE(symbolic::detail::BinaryExprView::tryFrom(negA).has_value());
        EXPECT_DEATH((void)symbolic::detail::LiteralExprView{sumA}, "");
        EXPECT_DEATH((void)symbolic::detail::UnaryExprView{oneA}, "");
        EXPECT_DEATH((void)symbolic::detail::BinaryExprView{negA}, "");
    }

    TEST(ExprFactoryTest, WithValTypeDoesNotMutateFactorySharedOperation) {
        symbolic::ExprFactory factory;

        auto one = literalHandle(factory, 1);
        auto two = literalHandle(factory, 2);
        auto sum = binaryHandle(factory, one, symbolic::BinaryOp::Add, two);

        auto targetType = symbolic::ExprType{
            symbolic::ExprScalarKind::UInt, 64};
        auto typedSum = withTypeHandle(factory, sum, targetType);

        EXPECT_EQ(sum.getValType().kind, symbolic::ExprScalarKind::Int);
        EXPECT_EQ(sum.getValType().bitWidth, 32);
        EXPECT_EQ(typedSum.getValType().kind, symbolic::ExprScalarKind::UInt);
        EXPECT_EQ(typedSum.getValType().bitWidth, 64);

        symbolic::detail::BinaryExprView typedSumView{typedSum};
        EXPECT_EQ(typedSumView.left(), one);
        EXPECT_EQ(typedSumView.right(), two);

        EXPECT_NE(typedSum, sum);
        EXPECT_EQ(typedSum, withTypeHandle(factory, sum, targetType));
    }

    TEST(ExprFactoryTest, WithValTypeInternsTypedRebuilds) {
        symbolic::ExprFactory factory;

        auto one = literalHandle(factory, 1);
        auto targetType = symbolic::ExprType{
            symbolic::ExprScalarKind::UInt, 64};

        auto typedOne = withTypeHandle(factory, one, targetType);
        EXPECT_NE(typedOne, one);
        EXPECT_EQ(typedOne, withTypeHandle(factory, one, targetType));
        EXPECT_EQ(withTypeHandle(factory, typedOne, targetType), typedOne);
        EXPECT_EQ(typedOne.getValType(), targetType);

        auto large = literalHandle(factory, std::numeric_limits<std::uint64_t>::max());
        auto typedLarge = withTypeHandle(factory, large, targetType);
        EXPECT_TRUE(typedLarge.isLiteralExpr());
        EXPECT_EQ(typedLarge.getValType(), targetType);
        EXPECT_EQ(withTypeHandle(factory, typedLarge, large.getValType()), large);

        auto leafTargetType = symbolic::ExprType{
            symbolic::ExprScalarKind::Bool, 8};
        for (auto original : {unknownHandle(factory), rangeIndexHandle(factory, "i")}) {
            auto originalType = original.getValType();
            auto typed        = withTypeHandle(factory, original, leafTargetType);

            EXPECT_EQ(original.getValType(), originalType);
            EXPECT_EQ(typed.getValType(), leafTargetType);
            EXPECT_NE(typed, original);
            EXPECT_EQ(typed, withTypeHandle(factory, original, leafTargetType));
        }

        symbolic::ExprFactoryScope scope(factory);
        FacadeExprForTest facade{one};
        auto typedFacade = facade.withType(targetType);
        EXPECT_EQ(handle(typedFacade), typedOne);
        EXPECT_EQ(typedFacade.getValType(), targetType);
    }

    TEST(ExprFactoryTest, WithValTypeImportsCrossFactoryOperationChildren) {
        symbolic::ExprFactory source;
        auto sourceOne = literalHandle(source, 1);
        auto sourceTwo = literalHandle(source, 2);
        auto sourceSum = binaryHandle(source,
            sourceOne, symbolic::BinaryOp::Add, sourceTwo);

        symbolic::ExprFactory target;
        auto targetType = symbolic::ExprType{
            symbolic::ExprScalarKind::UInt, 64};
        auto typedSum = withTypeHandle(target, sourceSum, targetType);
        symbolic::detail::BinaryExprView typedView{typedSum};
        auto targetOne = literalHandle(target, 1);
        auto targetTwo = literalHandle(target, 2);

        EXPECT_EQ(typedSum.getValType(), targetType);
        EXPECT_EQ(typedView.left(), targetOne);
        EXPECT_EQ(typedView.right(), targetTwo);
        EXPECT_NE(typedView.left(), sourceOne);
        EXPECT_NE(typedView.right(), sourceTwo);
        EXPECT_EQ(typedSum, withTypeHandle(target, sourceSum, targetType));
    }

    TEST(ExprFactoryTest, WithValTypeImportsCrossFactoryAddressAndAggregateChildren) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int value = 0;
                return value;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory source;
        auto sourceBase = variableAddressHandle(source, var);
        auto sourceOffset = binaryHandle(source,
            literalHandle(source, 1), symbolic::BinaryOp::Add, literalHandle(source, 2));
        auto sourceLength = literalHandle(source, 4);
        auto sourceRange      = symbolAddressHandle(source, var->getType(), sourceBase, point,
                                                    sourceOffset, sourceLength);
        auto sourcePredicate = binaryHandle(source,
            rangeIndexHandle(source, "i"), symbolic::BinaryOp::LessThan,
            literalHandle(source, 4));
        auto sourceBody = literalHandle(source, 7);
        auto sourceSum        = makeSumOverRangeForTest(source, sourceRange, "i", point);
        auto sourceQuantifier = makeQuantifierOverRangeForTest(
            source, sourceRange, "i", symbolic::RangeQuantifier::ForAll, sourcePredicate);
        auto sourceMax = makeMaxMinOverRangeForTest(
            source, sourceRange, "i", symbolic::RangeExtremum::Max, sourceBody, point);

        symbolic::ExprFactory target;
        auto boolType = symbolic::ExprType{
            symbolic::ExprScalarKind::Bool, 8};
        auto uintType = symbolic::ExprType{
            symbolic::ExprScalarKind::UInt, 64};
        auto typedRange = withTypeHandle(target, sourceRange.asExpr(), boolType);
        auto typedSum = withTypeHandle(target, sourceSum, uintType);
        auto typedQuantifier = withTypeHandle(target, sourceQuantifier, uintType);
        auto typedMax = withTypeHandle(target, sourceMax, uintType);

        auto targetBase = variableAddressHandle(target, var);
        auto targetOffset = binaryHandle(target,
            literalHandle(target, 1), symbolic::BinaryOp::Add, literalHandle(target, 2));
        auto targetLength = literalHandle(target, 4);
        auto typedRangeView = symbolic::detail::SymbolAddressView::tryFrom(typedRange).value();
        ASSERT_TRUE(typedRangeView.from());
        ASSERT_TRUE(typedRangeView.length());
        EXPECT_EQ(*typedRangeView.from(), targetBase);
        EXPECT_EQ(typedRangeView.offset(), targetOffset);
        EXPECT_EQ(typedRangeView.length().value(), targetLength);

        auto targetRange = importAddressHandle(target, sourceRange);
        symbolic::detail::SumOverRangeView typedSumView{typedSum};
        symbolic::detail::QuantifierOverRangeView typedQuantifierView{typedQuantifier};
        symbolic::detail::MaxMinOverRangeView typedMaxView{typedMax};
        EXPECT_EQ(typedSumView.range().handle(), targetRange);
        EXPECT_EQ(typedQuantifierView.range().handle(), targetRange);
        EXPECT_EQ(typedQuantifierView.predicate(), importExprHandle(target, sourcePredicate));
        EXPECT_EQ(typedMaxView.range().handle(), targetRange);
        EXPECT_EQ(typedMaxView.body(), importExprHandle(target, sourceBody));
        EXPECT_EQ(typedSumView.indexName(), "i");
        EXPECT_EQ(typedQuantifierView.quantifier(), symbolic::RangeQuantifier::ForAll);
        EXPECT_EQ(typedMaxView.extremum(), symbolic::RangeExtremum::Max);

        EXPECT_FALSE(symbolic::detail::SumOverRangeView::tryFrom(typedQuantifier).has_value());
        EXPECT_FALSE(symbolic::detail::QuantifierOverRangeView::tryFrom(typedMax).has_value());
        EXPECT_FALSE(symbolic::detail::MaxMinOverRangeView::tryFrom(typedSum).has_value());
        EXPECT_DEATH((void)symbolic::detail::SumOverRangeView{typedMax}, "");
    }

    TEST(ExprFactoryTest, WithValTypeDoesNotMutateSharedSymbolOrStructure) {
        ASTExtractor e;
        e.init(R"c(
            struct Pair {
                int first;
                int second;
            };

            int f(void) {
                struct Pair value = {1, 2};
                return value.first;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto *record = var->getType()->getAsRecordDecl()->getDefinition();
        ASSERT_NE(record, nullptr);
        const auto &layout = record->getASTContext().getASTRecordLayout(record);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory source;
        auto sourceAddress = variableAddressHandle(source, var);
        auto sourceValue   = symbolValueHandle(source,
            symbolic::deriveType(var->getType()), sourceAddress, point);
        auto sourceStructure = structureHandle(source, record, layout, sourceAddress, point);
        auto valueType       = sourceValue.getValType();
        auto structureType   = sourceStructure.getValType();

        symbolic::ExprFactory target;
        auto targetType = symbolic::ExprType{
            symbolic::ExprScalarKind::UInt, 64};
        auto typedValue     = withTypeHandle(target, sourceValue, targetType);
        auto typedStructure = withTypeHandle(target, sourceStructure, targetType);

        EXPECT_EQ(sourceValue.getValType(), valueType);
        EXPECT_EQ(sourceStructure.getValType(), structureType);
        EXPECT_EQ(typedValue.getValType(), targetType);
        EXPECT_EQ(typedStructure.getValType(), targetType);
        EXPECT_EQ(typedValue, withTypeHandle(target, sourceValue, targetType));
        EXPECT_EQ(typedStructure, withTypeHandle(target, sourceStructure, targetType));

        symbolic::detail::SymbolValueView typedValueView{typedValue};
        EXPECT_EQ(typedValueView.from(), variableAddressHandle(target, var));

        symbolic::detail::StructureView sourceView{sourceStructure};
        symbolic::detail::StructureView typedView{typedStructure};
        ASSERT_EQ(typedView.size(), sourceView.size());
        for (size_t i = 0; i < typedView.size(); ++i)
            EXPECT_EQ(typedView.field(i), importExprHandle(target, sourceView.field(i)));
    }

    TEST(ExprFactoryTest, ValueSubstitutionFacadeImportsReplacement) {
        symbolic::ExprFactory factory;

        auto one = literalHandle(factory, int64_t{1});
        auto two = literalHandle(factory, int64_t{2});
        auto original = binaryHandle(factory, one, symbolic::BinaryOp::Add, two);
        auto replacement =
            binaryHandle(factory, two, symbolic::BinaryOp::Subtract, one);

        symbolic::ExprSubstitutions substitutions;
        substitutions.insertOrAssign(FacadeExprForTest{factory, original},
                                     FacadeExprForTest{factory, replacement});

        auto substituted = substituteValuesForTest(factory, original, substitutions);
        EXPECT_EQ(substituted, replacement);
    }

    TEST(ExprFactoryTest, ExprFacadeValueSubstitutionReturnsInternedReplacement) {
        symbolic::ExprFactory factory;

        auto one = literalHandle(factory, int64_t{1});
        auto two = literalHandle(factory, int64_t{2});
        auto original = binaryHandle(factory, one, symbolic::BinaryOp::Add, two);
        auto replacement =
            binaryHandle(factory, two, symbolic::BinaryOp::Subtract, one);

        symbolic::ExprSubstitutions substitutions;
        substitutions.insertOrAssign(FacadeExprForTest{factory, original},
                                     FacadeExprForTest{factory, replacement});

        auto substituted = FacadeExprForTest{factory, original}.substituteValues(substitutions);
        EXPECT_EQ(handle(substituted), replacement);
    }

    TEST(ExprFactoryTest, ExprFacadeValueSubstitutionImportsCrossFactoryReplacement) {
        symbolic::ExprFactory source;
        auto sourceReplacement = binaryHandle(source,
            literalHandle(source, int64_t{2}), symbolic::BinaryOp::Subtract, literalHandle(source, int64_t{1}));

        symbolic::ExprFactory target;
        auto original = literalHandle(target, int64_t{7});
        auto expected = binaryHandle(target, literalHandle(target, int64_t{2}), symbolic::BinaryOp::Subtract,
                                      literalHandle(target, int64_t{1}));
        symbolic::ExprSubstitutions substitutions;
        substitutions.insertOrAssign(FacadeExprForTest{target, original},
                                     FacadeExprForTest{source, sourceReplacement});

        auto substituted = FacadeExprForTest{target, original}.substituteValues(substitutions);

        EXPECT_EQ(&substituted.factory(), &target);
        EXPECT_EQ(handle(substituted), expected);
    }

    TEST(ExprFactoryTest, ValueSubstitutionFacadeRebuildsBinaryThroughFactory) {
        symbolic::ExprFactory factory;

        auto one = literalHandle(factory, int64_t{1});
        auto two = literalHandle(factory, int64_t{2});
        auto three = literalHandle(factory, int64_t{3});
        auto original = binaryHandle(factory, one, symbolic::BinaryOp::Add, two);
        auto expected = binaryHandle(factory, three, symbolic::BinaryOp::Add, two);

        symbolic::ExprSubstitutions substitutions;
        substitutions.insertOrAssign(FacadeExprForTest{factory, one},
                                     FacadeExprForTest{factory, three});

        auto substituted = substituteValuesForTest(factory, original, substitutions);
        EXPECT_EQ(substituted, expected);
    }

    TEST(ExprFactoryTest, ValueSubstitutionFacadeRebuildsAggregateThroughFactory) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;

        auto base = variableAddressHandle(factory, var);
        auto range = symbolAddressHandle(factory, var->getType(), base, point,
                                         literalHandle(factory, int64_t{0}),
                                         literalHandle(factory, int64_t{3}));
        auto one = literalHandle(factory, int64_t{1});
        auto two = literalHandle(factory, int64_t{2});
        auto three = literalHandle(factory, int64_t{3});
        auto pred = binaryHandle(factory, one, symbolic::BinaryOp::LessThan, two);
        auto expectedPred =
            binaryHandle(factory, three, symbolic::BinaryOp::LessThan, two);
        auto original = makeQuantifierOverRangeForTest(factory, range, "i",
                                                       symbolic::RangeQuantifier::ForAll, pred);
        auto expected = makeQuantifierOverRangeForTest(
            factory, range, "i", symbolic::RangeQuantifier::ForAll, expectedPred);

        symbolic::ExprSubstitutions substitutions;
        substitutions.insertOrAssign(FacadeExprForTest{factory, one},
                                     FacadeExprForTest{factory, three});

        auto substituted = substituteValuesForTest(factory, original, substitutions);
        EXPECT_EQ(substituted, expected);
    }

    TEST(ExprFactoryTest, ScopedSimplifiedLinearExprRebuildsThroughFactory) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto varAddr = variableAddressHandle(factory, var);
        auto x       = symbolValueHandle(factory, symbolic::deriveType(var->getType()),
                                           varAddr, point);
        auto two     = literalHandle(factory, 2);
        auto sum     = binaryHandle(factory, x, symbolic::BinaryOp::Add, two);

        auto simplified = simplifyForTest(factory, sum);
        symbolic::detail::BinaryExprView rebuilt{simplified};

        EXPECT_EQ(rebuilt.left(), importExprHandle(factory, rebuilt.left()));
        EXPECT_EQ(rebuilt.right(), importExprHandle(factory, rebuilt.right()));
        EXPECT_EQ(simplified, importExprHandle(factory, simplified));
    }

    TEST(ExprFactoryTest, ScopedSimplifiedNonLinearFallbackImportsThroughFactory) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto from = symbolic::Addr::variable(var);
        auto xHandle = handle(
            symbolic::Expr::symbolValue(symbolic::deriveType(var->getType()), from, point));
        auto legacyProduct = binaryHandle(factory,
            xHandle, symbolic::BinaryOp::Multiply, xHandle);

        auto simplified = simplifyForTest(factory, legacyProduct);
        symbolic::detail::BinaryExprView product{simplified};

        EXPECT_EQ(product.left(), importExprHandle(factory, product.left()));
        EXPECT_EQ(product.right(), importExprHandle(factory, product.right()));
        EXPECT_EQ(simplified, importExprHandle(factory, legacyProduct));
    }

    TEST(ExprFactoryTest, SimplifiedExprHandleReturnsInternedNode) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto expr = binaryHandle(factory, literalHandle(factory, int64_t{1}),
                                   symbolic::BinaryOp::Add,
                                   literalHandle(factory, int64_t{2}));

        auto simplified = simplifyForTest(factory, expr);

        EXPECT_EQ(simplified, literalHandle(factory, int64_t{3}));
    }

    TEST(ExprFactoryTest, SimplifiedBinaryFallbackPreservesOperationAndChildHandles) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto i = rangeIndexHandle(factory, "i");
        auto j = rangeIndexHandle(factory, "j");
        auto product = binaryHandle(factory,
            i, symbolic::BinaryOp::Multiply, j);

        auto simplified = simplifiedBinaryHandle(factory,
            i, symbolic::BinaryOp::Multiply, j);

        EXPECT_EQ(simplified, product);
        symbolic::detail::BinaryExprView view{simplified};
        EXPECT_EQ(view.left(), i);
        EXPECT_EQ(view.right(), j);
    }

    TEST(ExprFactoryTest, ScopedBooleanComparisonSimplificationImportsReturnedExpr) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto from = symbolic::Addr::variable(var);
        auto x = handle(symbolic::Expr::symbolValue(
            symbolic::deriveType(var->getType()), from, point));
        auto zero = literalHandle(factory, int64_t{0});
        auto predicate =
            binaryHandle(factory, x, symbolic::BinaryOp::Equal, zero);
        auto wrapped = binaryHandle(factory, predicate, symbolic::BinaryOp::Equal,
                                      literalHandle(factory, int64_t{1}));

        auto simplified = simplifyForTest(factory, wrapped);
        symbolic::detail::BinaryExprView returnedPredicate{simplified};

        EXPECT_EQ(returnedPredicate.operation(), symbolic::BinaryOp::Equal);
        EXPECT_EQ(returnedPredicate.left(), x);
        EXPECT_EQ(returnedPredicate.right(), zero);
        EXPECT_EQ(importExprHandle(factory, simplified),
                  binaryHandle(factory, x, symbolic::BinaryOp::Equal, zero));
    }

    TEST(ExprFactoryTest, ConstantEvalReturnsInternedLiteral) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto expr = binaryHandle(factory, literalHandle(factory, int64_t{1}),
                                   symbolic::BinaryOp::Add,
                                   literalHandle(factory, int64_t{2}));

        auto value = expr.tryEvalToConstant();

        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value.value(), 3);
        EXPECT_FALSE(rangeIndexHandle(factory, "i").tryEvalToConstant().has_value());

        auto simplified = simplifyForTest(factory, expr);
        EXPECT_EQ(importExprHandle(factory, simplified), literalHandle(factory, int64_t{3}));
    }

    TEST(ExprFactoryTest, ConstantEvalPreservesOperatorAndShortCircuitSemantics) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto eval = [](symbolic::detail::ExprHandle expr) { return expr.tryEvalToConstant(); };

        EXPECT_EQ(eval(unaryHandle(factory, symbolic::UnaryOp::Minus,
                                     literalHandle(factory, int64_t{5}))),
                  -5);
        EXPECT_EQ(eval(binaryHandle(factory, literalHandle(factory, int64_t{-1}),
                                      symbolic::BinaryOp::LessThan,
                                      literalHandle(factory, int64_t{1}))),
                  1);
        EXPECT_EQ(eval(binaryHandle(factory,
                      literalHandle(factory, std::numeric_limits<uint64_t>::max()),
                      symbolic::BinaryOp::Add,
                      literalHandle(factory, uint64_t{1}))),
                  0);
        EXPECT_FALSE(eval(binaryHandle(factory, literalHandle(factory, int64_t{1}),
                                         symbolic::BinaryOp::Divide,
                                         literalHandle(factory, int64_t{0})))
                         .has_value());

        auto unknown = unknownHandle(factory);
        EXPECT_EQ(eval(binaryHandle(factory, literalHandle(factory, false),
                                      symbolic::BinaryOp::LogicalAnd,
                                      unknown)),
                  0);
        EXPECT_EQ(eval(binaryHandle(factory, literalHandle(factory, true),
                                      symbolic::BinaryOp::LogicalOr,
                                      unknown)),
                  1);
        EXPECT_FALSE(eval(binaryHandle(factory, literalHandle(factory, true),
                                         symbolic::BinaryOp::LogicalAnd,
                                         unknown))
                         .has_value());
        EXPECT_FALSE(eval(binaryHandle(factory, literalHandle(factory, false),
                                         symbolic::BinaryOp::LogicalOr,
                                         unknown))
                         .has_value());
    }

    TEST(ExprFactoryTest, ImportsOperationTreesAcrossFactoriesIntoInternedDag) {
        symbolic::ExprFactory source;
        auto sourceUnary = unaryHandle(source, symbolic::UnaryOp::Minus,
                                        literalHandle(source, int64_t{1}));
        auto sourceTree = binaryHandle(source, sourceUnary, symbolic::BinaryOp::Add,
                                        literalHandle(source, int64_t{2}));

        symbolic::ExprFactory factory;
        auto imported = importExprHandle(factory, sourceTree);
        auto repeated = importExprHandle(factory, sourceTree);

        EXPECT_EQ(imported, repeated);
        auto one = literalHandle(factory, int64_t{1});
        auto two = literalHandle(factory, int64_t{2});

        symbolic::detail::BinaryExprView bin{imported};
        EXPECT_EQ(bin.right(), two);
        symbolic::detail::UnaryExprView unary{bin.left()};
        EXPECT_EQ(unary.operand(), one);

        symbolic::ExprFactoryScope scope(factory);
        FacadeExprForTest facade{sourceTree};
        EXPECT_EQ(handle(facade), imported);
        EXPECT_NE(handle(facade), sourceTree);
    }

    TEST(ExprFacadeTest, CollectUsedSymbolsMergesMultipleExpressions) {
        ASTExtractor e;
        e.init(R"c(
            int f(int x, int y) {
                return x + y;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        ASSERT_EQ(func->getNumParams(), 2u);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        auto xAddr = variableAddressHandle(factory, func->getParamDecl(0));
        auto yAddr = variableAddressHandle(factory, func->getParamDecl(1));
        auto xValue = symbolValueHandle(factory,
            symbolic::deriveType(func->getParamDecl(0)->getType()), xAddr, point);
        auto yValue = symbolValueHandle(factory,
            symbolic::deriveType(func->getParamDecl(1)->getType()), yAddr, point);
        FacadeExprForTest xExpr{factory, xValue};
        FacadeExprForTest yExpr{factory, yValue};

        auto [usedSymbols, expressionIndexes] = symbolic::collectUsedSymbols(xExpr, yExpr);

        ASSERT_EQ(usedSymbols.size(), 2u);
        ASSERT_EQ(expressionIndexes.size(), 2u);
        EXPECT_TRUE(usedSymbols.contains(xExpr));
        EXPECT_TRUE(usedSymbols.contains(yExpr));
        EXPECT_TRUE(expressionIndexes.contains(xExpr));
        EXPECT_TRUE(expressionIndexes.contains(yExpr));
    }

    TEST(AddrHandleTest, TryFromAcceptsAddressesAndRejectsValues) {
        ASTExtractor e;
        e.init(R"c(
            int f(int x) {
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);

        symbolic::ExprFactory factory;
        auto address = variableAddressHandle(factory, func->getParamDecl(0));
        auto converted = symbolic::detail::AddrHandle::tryFrom(address.asExpr());

        ASSERT_TRUE(converted.has_value());
        EXPECT_EQ(*converted, address);
        EXPECT_FALSE(symbolic::detail::AddrHandle::tryFrom(literalHandle(factory, 1)).has_value());
    }

    TEST(ExprFactoryTest, ImportsCrossFactoryUInt64LiteralWithoutValueNarrowing) {
        const auto large = std::numeric_limits<std::uint64_t>::max();
        symbolic::ExprFactory sourceFactory;
        auto source = literalHandle(sourceFactory, large);

        symbolic::ExprFactory factory;
        auto imported = importExprHandle(factory, source);
        auto expected = literalHandle(factory, large);

        EXPECT_EQ(imported, expected);
        EXPECT_TRUE(imported.isLiteralExpr());
        EXPECT_EQ(imported.getValType().kind, symbolic::ExprScalarKind::UInt);
        EXPECT_EQ(imported.getValType().bitWidth, 64);
    }

    TEST(ExprFactoryTest, UnknownBuilderReusesUnknownNode) {
        symbolic::ExprFactory factory;

        auto a = unknownHandle(factory);
        auto b = unknownHandle(factory);

        EXPECT_EQ(a, b);
        EXPECT_TRUE(a.isUnknown());
    }

    TEST(ExprFactoryTest, RangeIndexBuilderAndImportReuseNode) {
        symbolic::ExprFactory factory;

        auto k = rangeIndexHandle(factory, "k");
        auto i = rangeIndexHandle(factory, "i");

        EXPECT_EQ(k, i);
        EXPECT_TRUE(k.isRangeIndex());
        EXPECT_FALSE(literalHandle(factory, 0).isRangeIndex());
        EXPECT_FALSE(k.isStructure());
        EXPECT_FALSE(k.isSymbolValue());
        EXPECT_FALSE(k.isSymbolAddress());
        EXPECT_FALSE(k.isVariableAddress());
        EXPECT_FALSE(k.isFieldAddress());

        symbolic::ExprFactory sourceFactory;
        auto source = rangeIndexHandle(sourceFactory, "j");
        auto imported = importExprHandle(factory, source);

        EXPECT_EQ(imported, k);
    }

    TEST(ExprFactoryTest, ScopedRangeIndexSubstitutionPreservesFactoryChildren) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());
        symbolic::SymbolAddrBaseInfo rangeBase{point, var->getType()};

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto one = literalHandle(factory, int64_t{1});
        auto two = literalHandle(factory, int64_t{2});
        auto replacement =
            binaryHandle(factory, one, symbolic::BinaryOp::Add, two);

        auto rangeIndexNode = rangeIndexHandle(factory, "i");
        auto substituted    = FacadeExprForTest{factory, rangeIndexNode}.substituteRangeIndex(
                 rangeBase, FacadeExprForTest{factory, replacement});
        symbolic::detail::BinaryExprView node{handle(substituted)};

        EXPECT_EQ(node.left(), one);
        EXPECT_EQ(node.right(), two);

        auto varAddr = variableAddressHandle(factory, var);
        auto rangeIndex = rangeIndexHandle(factory, "i");
        auto indexedFrom =
            symbolAddressHandle(factory, var->getType(), varAddr, point, rangeIndex, rangeIndex);
        auto indexedValue = symbolValueHandle(factory,
            symbolic::deriveType(var->getType()), indexedFrom, point);
        auto indexedRangeBase = symbolic::detail::SymbolAddressView{indexedFrom}.baseInfo();
        auto substitutedValue =
            substituteRangeIndexForTest(factory, indexedValue, indexedRangeBase, replacement);
        auto expectedFrom =
            symbolAddressHandle(factory, var->getType(), varAddr, point, replacement, replacement);
        EXPECT_EQ(substitutedValue,
                  symbolValueHandle(factory,
                      symbolic::deriveType(var->getType()), expectedFrom, point));
    }

    TEST(ExprFactoryTest, ScopedLeafNoOpSubstitutionImportsThroughFactory) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());
        symbolic::SymbolAddrBaseInfo rangeBase{point, var->getType()};

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto literal = literalHandle(factory, int64_t{7});
        auto index = literalHandle(factory, int64_t{0});
        auto substitutedLiteral = substituteRangeIndexForTest(factory, literal, rangeBase, index);
        EXPECT_EQ(substitutedLiteral, literal);

        symbolic::ExprSubstitutions emptySubstitutions;
        auto valueSubstitutedLiteral =
            substituteValuesForTest(factory, literal, emptySubstitutions);
        EXPECT_EQ(valueSubstitutedLiteral, literal);

        auto varAddr = variableAddressHandle(factory, var);
        auto substitutedAddr =
            substituteRangeIndexForTest(factory, varAddr.asExpr(), rangeBase, index);
        EXPECT_EQ(substitutedAddr, varAddr.asExpr());
    }

    TEST(ExprFactoryTest, ImportsAggregateChildrenAsHandles) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto makeRange = [&]() {
            auto from  = symbolic::Addr::variable(var);
            auto range = symbolic::Addr::symbol(var->getType(), from, point)
                             .withLength(symbolic::LiteralExpr{factory, int64_t{3}});
            return handle(range);
        };

        auto makePred = [&]() {
            return binaryHandle(factory, rangeIndexHandle(factory, "i"),
                                  symbolic::BinaryOp::LessThan,
                                  literalHandle(factory, 3));
        };

        auto sumRangeHandle = makeRange();
        auto sumHandle      = makeSumOverRangeForTest(factory, sumRangeHandle, "i", point);
        symbolic::detail::SumOverRangeView sum{sumHandle};
        auto quantifierRangeHandle = makeRange();
        auto predicate = makePred();
        auto quantifierHandle      = makeQuantifierOverRangeForTest(
                 factory, quantifierRangeHandle, "i", symbolic::RangeQuantifier::ForAll, predicate);
        symbolic::detail::QuantifierOverRangeView quantifier{quantifierHandle};
        auto maxRangeHandle = makeRange();
        auto maxHandle      = makeMaxMinOverRangeForTest(factory, maxRangeHandle, "i",
                                                         symbolic::RangeExtremum::Max, point);
        symbolic::detail::MaxMinOverRangeView max{maxHandle};

        auto importedSum = importExprHandle(factory, handle(sum));
        auto sumRange    = importExprHandle(factory, sum.range().handle().asExpr());
        symbolic::detail::SumOverRangeView sumNode{importedSum};
        EXPECT_EQ(sumNode.range().handle().asExpr(), sumRange);
        EXPECT_EQ(importedSum, importExprHandle(factory, handle(sum)));

        auto importedQuantifier = importExprHandle(factory, handle(quantifier));
        auto quantifierRange    = importExprHandle(factory, quantifier.range().handle().asExpr());
        auto quantifierPred     = importExprHandle(factory, quantifier.predicate());
        symbolic::detail::QuantifierOverRangeView quantifierNode{importedQuantifier};
        EXPECT_EQ(quantifierNode.range().handle().asExpr(), quantifierRange);
        EXPECT_EQ(quantifierNode.predicate(), quantifierPred);
        EXPECT_EQ(importedQuantifier, importExprHandle(factory, handle(quantifier)));

        auto importedMax = importExprHandle(factory, handle(max));
        auto maxRange    = importExprHandle(factory, max.range().handle().asExpr());
        auto maxBody     = importExprHandle(factory, max.body());
        symbolic::detail::MaxMinOverRangeView maxNode{importedMax};
        EXPECT_EQ(maxNode.range().handle().asExpr(), maxRange);
        EXPECT_EQ(maxNode.body(), maxBody);
        EXPECT_EQ(importedMax, importExprHandle(factory, handle(max)));
    }

    TEST(SumOverRangeRebuildTest, RangeRetainsHandleAcrossImportAndSubstitution) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto rangeHandle = symbolAddressHandle(factory, var->getType(),
                                               variableAddressHandle(factory, var), point);
        rangeHandle      = withOffsetHandle(factory, rangeHandle, rangeIndexHandle(factory, "i"));
        rangeHandle    = withLengthHandle(factory, rangeHandle, literalHandle(factory, int64_t{3}));
        auto rangeBase = symbolic::detail::SymbolAddressView{rangeHandle}.baseInfo();
        auto sumHandle = makeSumOverRangeForTest(factory, rangeHandle, "i", point);
        symbolic::detail::SumOverRangeView sum{sumHandle};

        auto imported = importExprHandle(factory, handle(sum));
        EXPECT_EQ(imported, handle(sum));
        EXPECT_EQ(symbolic::detail::SumOverRangeView{imported}.range().handle(), rangeHandle);

        auto index = literalHandle(factory, int64_t{1});
        auto substituted = substituteRangeIndexForTest(factory, handle(sum), rangeBase, index);
        EXPECT_NE(substituted, handle(sum));
    }

    TEST(QuantifierOverRangeRebuildTest, PredicateRetainsHandleAcrossImportAndSubstitution) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto rangeHandle = symbolAddressHandle(factory, var->getType(),
                                               variableAddressHandle(factory, var), point);
        rangeHandle    = withLengthHandle(factory, rangeHandle, literalHandle(factory, int64_t{3}));
        auto rangeBase = symbolic::detail::SymbolAddressView{rangeHandle}.baseInfo();
        auto pred = binaryHandle(factory, rangeIndexHandle(factory, "i"),
                                   symbolic::BinaryOp::LessThan,
                                   literalHandle(factory, int64_t{3}));
        auto quantifierHandle = makeQuantifierOverRangeForTest(
            factory, rangeHandle, "i", symbolic::RangeQuantifier::ForAll, pred);
        symbolic::detail::QuantifierOverRangeView quantifier{quantifierHandle};

        auto imported = importExprHandle(factory, handle(quantifier));
        EXPECT_EQ(imported, handle(quantifier));
        symbolic::detail::QuantifierOverRangeView importedQuantifier{imported};
        EXPECT_EQ(importedQuantifier.range().handle(), rangeHandle);
        EXPECT_EQ(importedQuantifier.predicate(),
                  importExprHandle(factory, quantifier.predicate()));

        auto index = literalHandle(factory, int64_t{1});
        auto substituted =
            substituteRangeIndexForTest(factory, handle(quantifier), rangeBase, index);
        EXPECT_NE(substituted, handle(quantifier));
    }

    TEST(MaxMinOverRangeRebuildTest, BodyRetainsHandleAcrossImportAndSubstitution) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto rangeHandle = symbolAddressHandle(factory, var->getType(),
                                               variableAddressHandle(factory, var), point);
        rangeHandle    = withLengthHandle(factory, rangeHandle, literalHandle(factory, int64_t{3}));
        auto rangeBase = symbolic::detail::SymbolAddressView{rangeHandle}.baseInfo();
        auto maxHandle = makeMaxMinOverRangeForTest(factory, rangeHandle, "i",
                                                    symbolic::RangeExtremum::Max, point);
        symbolic::detail::MaxMinOverRangeView max{maxHandle};

        auto indexedRange = withOffsetHandle(factory, rangeHandle, rangeIndexHandle(factory, "i"));
        indexedRange      = withoutLengthHandle(factory, indexedRange);
        auto expectedBody = handle(symbolic::Expr::symbol(
            rangeHandle.getPointeeType(), FacadeAddrForTest{factory, indexedRange}, point));
        EXPECT_EQ(max.body(), expectedBody);

        auto imported = importExprHandle(factory, handle(max));
        EXPECT_EQ(imported, handle(max));
        symbolic::detail::MaxMinOverRangeView importedMax{imported};
        EXPECT_EQ(importedMax.range().handle(), rangeHandle);
        EXPECT_EQ(importedMax.body(), importExprHandle(factory, max.body()));

        auto index = literalHandle(factory, int64_t{1});
        auto substituted = substituteRangeIndexForTest(factory, handle(max), rangeBase, index);
        EXPECT_NE(substituted, handle(max));
    }

    TEST(AggregateRebuildTest, FacadeRejectsSymbolAddressWithoutLength) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        auto address = symbolAddressHandle(factory, var->getType(),
                                           variableAddressHandle(factory, var), point);

        ASSERT_DEATH((void)makeSumOverRangeForTest(factory, address, "i", point), "");
    }

    TEST(AggregateRebuildTest, ScopedSubstitutionReturnsHandleBackedChildren) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto makeRange = [&]() {
            auto rangeHandle = symbolAddressHandle(factory, var->getType(),
                                                   variableAddressHandle(factory, var), point);
            rangeHandle = withOffsetHandle(factory, rangeHandle, rangeIndexHandle(factory, "i"));
            rangeHandle =
                withLengthHandle(factory, rangeHandle, literalHandle(factory, int64_t{3}));
            return rangeHandle;
        };

        auto one   = literalHandle(factory, int64_t{1});
        auto three = literalHandle(factory, int64_t{3});

        auto sumRange = makeRange();
        auto rangeBase = symbolic::detail::SymbolAddressView{sumRange}.baseInfo();
        auto sumHandle      = makeSumOverRangeForTest(factory, sumRange, "i", point);
        auto substitutedSum = substituteRangeIndexForTest(factory, sumHandle, rangeBase, one);
        auto sumRangeView = symbolic::detail::SumOverRangeView{substitutedSum}.range();
        EXPECT_EQ(sumRangeView.offset(), one);
        ASSERT_TRUE(sumRangeView.length());
        EXPECT_EQ(sumRangeView.length().value(), three);

        auto quantRange = makeRange();
        auto quantifierHandle = makeQuantifierOverRangeForTest(
            factory, quantRange, "i", symbolic::RangeQuantifier::ForAll, rangeIndexHandle(factory, "i"));
        auto substitutedQuantifier =
            substituteRangeIndexForTest(factory, quantifierHandle, rangeBase, one);
        EXPECT_EQ(symbolic::detail::QuantifierOverRangeView{substitutedQuantifier}.predicate(), one);

        auto maxRange = makeRange();
        auto maxHandle = makeMaxMinOverRangeForTest(
            factory, maxRange, "i", symbolic::RangeExtremum::Max, rangeIndexHandle(factory, "i"), point);
        auto substitutedMax = substituteRangeIndexForTest(factory, maxHandle, rangeBase, one);
        EXPECT_EQ(symbolic::detail::MaxMinOverRangeView{substitutedMax}.body(), one);
    }

    TEST(AggregateRebuildTest, ScopedFacadesReturnHandleBackedChildren) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto makeRange = [&]() {
            return symbolAddressHandle(factory, var->getType(), variableAddressHandle(factory, var),
                                       point, rangeIndexHandle(factory, "i"),
                                       literalHandle(factory, int64_t{3}));
        };

        auto sumRange = makeRange();
        auto sum      = makeSumOverRangeForTest(factory, sumRange, "i", point);
        symbolic::detail::SumOverRangeView sumView{sum};
        EXPECT_TRUE(sum.isOverRange());
        EXPECT_EQ(importAddressHandle(factory, sumView.range().handle()), sumView.range().handle());

        auto quantifier = makeQuantifierOverRangeForTest(
            factory, makeRange(), "i", symbolic::RangeQuantifier::ForAll, rangeIndexHandle(factory, "i"));
        symbolic::detail::QuantifierOverRangeView quantifierView{quantifier};
        EXPECT_TRUE(quantifier.isOverRange());
        EXPECT_EQ(importAddressHandle(factory, quantifierView.range().handle()),
                  quantifierView.range().handle());
        EXPECT_EQ(importExprHandle(factory, quantifierView.predicate()),
                  quantifierView.predicate());

        auto max = makeMaxMinOverRangeForTest(factory, makeRange(), "i",
                                              symbolic::RangeExtremum::Max, point);
        symbolic::detail::MaxMinOverRangeView maxView{max};
        EXPECT_TRUE(max.isOverRange());
        EXPECT_EQ(importAddressHandle(factory, maxView.range().handle()), maxView.range().handle());
        EXPECT_EQ(importExprHandle(factory, maxView.body()), maxView.body());
        EXPECT_FALSE(literalHandle(factory, 0).isOverRange());
    }

    TEST(ExprFacadeTest, LiteralAndOperatorsUseCurrentFactory) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        symbolic::LiteralExpr x{10};
        symbolic::LiteralExpr y{20};

        symbolic::Expr sum = x + y;
        symbolic::Expr sameSum = symbolic::LiteralExpr{10} + symbolic::LiteralExpr{20};
        symbolic::Expr product = x * y;

        EXPECT_EQ(sum, sameSum);
        EXPECT_EQ(handle(sum), handle(sameSum));
        EXPECT_NE(sum, product);
        auto sumExpr = symbolic::BinaryExpr::tryFrom(sum);
        ASSERT_TRUE(sumExpr);
        EXPECT_EQ(sumExpr->operation(), symbolic::BinaryOp::Add);
        EXPECT_EQ(&sumExpr->left().factory(), &factory);
        EXPECT_EQ(sumExpr->left(), x);
        EXPECT_EQ(&sumExpr->right().factory(), &factory);
        EXPECT_EQ(sumExpr->right(), y);

        auto productExpr = symbolic::BinaryExpr::tryFrom(product);
        ASSERT_TRUE(productExpr);
        EXPECT_EQ(productExpr->operation(), symbolic::BinaryOp::Multiply);
        EXPECT_EQ(x.value(), 10);

        auto negated = -x;
        auto unaryExpr = symbolic::UnaryExpr::tryFrom(negated);
        ASSERT_TRUE(unaryExpr);
        EXPECT_EQ(unaryExpr->operation(), symbolic::UnaryOp::Minus);
        EXPECT_EQ(&unaryExpr->operand().factory(), &factory);
        EXPECT_EQ(unaryExpr->operand(), x);

        EXPECT_FALSE(symbolic::LiteralExpr::tryFrom(sum));
        EXPECT_FALSE(symbolic::UnaryExpr::tryFrom(x));
        EXPECT_FALSE(symbolic::BinaryExpr::tryFrom(negated));
        EXPECT_DEATH((void)symbolic::LiteralExpr{sum}, "");
        EXPECT_DEATH((void)symbolic::UnaryExpr{x}, "");
        EXPECT_DEATH((void)symbolic::BinaryExpr{negated}, "");
    }

    TEST(ExprFacadeTest, OperatorsRejectDifferentFactories) {
        symbolic::ExprFactory leftFactory;
        symbolic::ExprFactory rightFactory;

        symbolic::Expr left = [&] {
            symbolic::ExprFactoryScope scope(leftFactory);
            return FacadeExprForTest{symbolic::LiteralExpr{1}};
        }();
        symbolic::Expr right = [&] {
            symbolic::ExprFactoryScope scope(rightFactory);
            return FacadeExprForTest{symbolic::LiteralExpr{2}};
        }();

        ASSERT_DEATH({ (void)(left + right); }, "");
    }

    TEST(ExprFacadeTest, StructuralEqualityWorksAcrossFactories) {
        symbolic::ExprFactory leftFactory;
        symbolic::ExprFactory rightFactory;

        FacadeExprForTest left{leftFactory, literalHandle(leftFactory, 7)};
        FacadeExprForTest right{rightFactory, literalHandle(rightFactory, 7)};
        FacadeExprForTest different{rightFactory, literalHandle(rightFactory, 8)};

        EXPECT_FALSE(left == right);
        EXPECT_TRUE(left.structurallyEqual(right));
        EXPECT_FALSE(left.structurallyEqual(different));
    }

    TEST(ExprFacadeTest, LinearQueriesAndSymbolCollectionStayOnFacade) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        symbolic::Expr sum   = symbolic::LiteralExpr{3} + symbolic::LiteralExpr{4};
        symbolic::Expr other = symbolic::LiteralExpr{5};

        auto [facadeSymbols, facadeIndexes] = symbolic::collectUsedSymbols(sum, other);
        EXPECT_TRUE(facadeSymbols.empty());
        EXPECT_TRUE(facadeIndexes.empty());
        EXPECT_EQ(sum.getMaxDegree(), 0);

        const symbolic::ExprIndexMap noSymbols;
        auto linear = sum.toLinearExpr(noSymbols);
        EXPECT_TRUE(linear.all_homogeneous_terms_are_zero());
        EXPECT_EQ(linear.inhomogeneous_term().get_si(), 7);
    }

    TEST(ExprFacadeTest, PredicateHelpersUseCurrentFactory) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        symbolic::LiteralExpr one{1};
        symbolic::LiteralExpr two{2};

        auto equalA = one.equalTo(two);
        auto equalB = symbolic::LiteralExpr{1}.equalTo(symbolic::LiteralExpr{2});
        auto less = one.lessThan(two);
        auto greaterEqual = two.greaterEqual(one);
        auto conjunction = equalA.logicalAnd(less);
        auto disjunction = less.logicalOr(greaterEqual);
        auto negated = equalA.logicalNot();
        auto negatedAgain = equalB.logicalNot();

        EXPECT_EQ(equalA, equalB);
        EXPECT_EQ(negated, negatedAgain);

        EXPECT_EQ(symbolic::detail::BinaryExprView{handle(equalA)}.operation(),
                  symbolic::BinaryOp::Equal);
        EXPECT_EQ(symbolic::detail::BinaryExprView{handle(less)}.operation(),
                  symbolic::BinaryOp::LessThan);
        EXPECT_EQ(symbolic::detail::BinaryExprView{handle(greaterEqual)}.operation(),
                  symbolic::BinaryOp::GreaterEqual);
        EXPECT_EQ(symbolic::detail::BinaryExprView{handle(conjunction)}.operation(),
                  symbolic::BinaryOp::LogicalAnd);
        EXPECT_EQ(symbolic::detail::BinaryExprView{handle(disjunction)}.operation(),
                  symbolic::BinaryOp::LogicalOr);
        EXPECT_EQ(symbolic::detail::UnaryExprView{handle(negated)}.operation(),
                  symbolic::UnaryOp::LogicalNot);
    }

    TEST(ExprFacadeTest, SimplifiedReturnsFactoryBackedFacade) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        symbolic::Expr sum = symbolic::LiteralExpr{1} + symbolic::LiteralExpr{2};
        symbolic::Expr simplified = sum.simplified();

        EXPECT_EQ(&simplified.factory(), &factory);
        EXPECT_EQ(handle(simplified), literalHandle(factory, int64_t{3}));
    }

    TEST(ExprFacadeTest, UnaryOperatorsUseCurrentFactory) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        symbolic::LiteralExpr one{1};
        auto negated = -one;
        auto notOne = !one;

        EXPECT_EQ(negated, -symbolic::LiteralExpr{1});
        EXPECT_EQ(notOne, !symbolic::LiteralExpr{1});
        EXPECT_EQ(symbolic::detail::UnaryExprView{handle(negated)}.operation(),
                  symbolic::UnaryOp::Minus);
        EXPECT_EQ(symbolic::detail::UnaryExprView{handle(notOne)}.operation(),
                  symbolic::UnaryOp::LogicalNot);
    }

    TEST(ExprFacadeTest, LeafHelpersUseFactoryBackedFacades) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto unknown = symbolic::Expr::unknown();
        auto index = symbolic::Expr::rangeIndex("i");
        auto varAddr = symbolic::Addr::variable(var);
        auto symbolAddr = symbolic::Addr::symbol(var->getType(), varAddr, point);
        symbolic::LiteralExpr length{3};
        auto indexedSymbolAddr = symbolic::Addr::symbol(var->getType(), varAddr, point,
                                                        index, length);
        auto unbasedRangeAddr = symbolic::Addr::symbol(var->getType(), point, index, length);
        auto symbolValue = symbolic::Expr::symbolValue(
            symbolic::ExprType{symbolic::ExprScalarKind::Int, 32},
            varAddr, point);

        EXPECT_EQ(handle(unknown), unknownHandle(factory));
        EXPECT_TRUE(unknown.isUnknown());
        EXPECT_EQ(handle(index), rangeIndexHandle(factory, "i"));
        EXPECT_TRUE(index.isRangeIndex());
        EXPECT_EQ(handle(varAddr), variableAddressHandle(factory, var));
        EXPECT_TRUE(varAddr.isVariableAddress());
        EXPECT_EQ(handle(symbolAddr),
                  symbolAddressHandle(factory, var->getType(), handle(varAddr), point));
        EXPECT_TRUE(symbolAddr.isSymbolAddress());
        EXPECT_EQ(handle(indexedSymbolAddr),
                  symbolAddressHandle(factory, var->getType(), handle(varAddr), point,
                                      handle(index), handle(length)));
        EXPECT_EQ(handle(unbasedRangeAddr),
                  symbolAddressHandle(factory, var->getType(), std::nullopt, point, handle(index),
                                      handle(length)));
        auto rangeFromExpr = indexedSymbolAddr.asExpr().tryAsAddress();
        ASSERT_TRUE(rangeFromExpr);
        EXPECT_EQ(&rangeFromExpr->factory(), &factory);
        EXPECT_EQ(handle(*rangeFromExpr), handle(indexedSymbolAddr));
        EXPECT_FALSE(unknown.tryAsAddress());
        EXPECT_EQ(handle(symbolValue),
                  symbolValueHandle(factory, symbolic::ExprType{
                                          symbolic::ExprScalarKind::Int, 32},
                                      handle(varAddr), point));
        EXPECT_TRUE(symbolValue.isSymbolValue());
        symbolic::SymbolValueExpr symbolValueExpr{symbolValue};
        EXPECT_EQ(&symbolValueExpr.factory(), &factory);
        EXPECT_EQ(symbolValueExpr.from(), varAddr);
        EXPECT_EQ(symbolValueExpr.fromPoint(), point);
        EXPECT_EQ(symbolValueExpr.fromRoot().value().get(), var);
        EXPECT_FALSE(symbolic::SymbolValueExpr::tryFrom(unknown).has_value());

        auto sourceAddress = symbolValue.sourceAddress();
        ASSERT_TRUE(sourceAddress);
        EXPECT_EQ(handle(*sourceAddress), handle(varAddr));
        EXPECT_TRUE(symbolValue.isFrom(varAddr, point));
        EXPECT_FALSE(unknown.sourceAddress());
        EXPECT_FALSE(unknown.isFrom(varAddr, point));
    }

    TEST(ExprFacadeTest, AggregateFacadesBuildInternedNodes) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto base = symbolic::Addr::variable(var);
        auto index = symbolic::Expr::rangeIndex("i");
        symbolic::LiteralExpr length{3};
        auto range = symbolic::Addr::symbol(var->getType(), base, point, index, length);

        symbolic::SumOverRangeExpr sum{range, "i", point};
        symbolic::QuantifierOverRangeExpr quantifier{
            range, "i", symbolic::RangeQuantifier::ForAll, index};
        symbolic::MaxMinOverRangeExpr maximum{
            range, "i", symbolic::RangeExtremum::Max, point};

        EXPECT_EQ(handle(sum), makeSumOverRangeForTest(factory, handle(range), "i", point));
        EXPECT_EQ(handle(quantifier), makeQuantifierOverRangeForTest(
                                           factory, handle(range), "i",
                                           symbolic::RangeQuantifier::ForAll, handle(index)));
        EXPECT_EQ(handle(maximum),
                  makeMaxMinOverRangeForTest(factory, handle(range), "i",
                                             symbolic::RangeExtremum::Max, point));
        EXPECT_TRUE(sum.isOverRange());
        EXPECT_TRUE(quantifier.isOverRange());
        EXPECT_TRUE(maximum.isOverRange());
        EXPECT_EQ(&sum.range().factory(), &factory);
        EXPECT_EQ(handle(sum.range()), handle(range));
        EXPECT_EQ(sum.indexName(), "i");
        EXPECT_EQ(sum.fromPoint(), point);
        EXPECT_EQ(handle(quantifier.range()), handle(range));
        EXPECT_EQ(quantifier.indexName(), "i");
        EXPECT_EQ(quantifier.quantifier(), symbolic::RangeQuantifier::ForAll);
        EXPECT_EQ(quantifier.predicate(), index);
        EXPECT_EQ(handle(maximum.range()), handle(range));
        EXPECT_EQ(maximum.indexName(), "i");
        EXPECT_EQ(maximum.extremum(), symbolic::RangeExtremum::Max);
        EXPECT_EQ(maximum.fromPoint(), point);

        auto narrowedSum = symbolic::SumOverRangeExpr::tryFrom(FacadeExprForTest{sum});
        ASSERT_TRUE(narrowedSum);
        EXPECT_EQ(handle(narrowedSum->range()), handle(range));
        EXPECT_FALSE(symbolic::QuantifierOverRangeExpr::tryFrom(FacadeExprForTest{sum}));
        EXPECT_DEATH((void)symbolic::MaxMinOverRangeExpr{FacadeExprForTest{sum}}, "");
    }

    TEST(ExprFacadeTest, AggregateFacadesImportCrossFactoryChildren) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory source;
        auto sourceRange =
            symbolAddressHandle(source, var->getType(), variableAddressHandle(source, var), point,
                                rangeIndexHandle(source, "i"), literalHandle(source, int64_t{3}));
        auto sourcePredicate = binaryHandle(source, rangeIndexHandle(source, "i"), symbolic::BinaryOp::GreaterThan,
                                             literalHandle(source, 0));
        auto sourceBody = binaryHandle(source, rangeIndexHandle(source, "i"), symbolic::BinaryOp::Add,
                                        literalHandle(source, 1));

        symbolic::ExprFactory target;
        symbolic::QuantifierOverRangeExpr quantifier{FacadeAddrForTest{target, sourceRange}, "i",
                                                     symbolic::RangeQuantifier::ForAll,
                                                     FacadeExprForTest{target, sourcePredicate}};
        symbolic::MaxMinOverRangeExpr maximum{FacadeAddrForTest{target, sourceRange}, "i",
                                              symbolic::RangeExtremum::Max,
                                              FacadeExprForTest{target, sourceBody}, point};

        EXPECT_EQ(handle(quantifier.range()), importAddressHandle(target, sourceRange));
        EXPECT_EQ(handle(quantifier.predicate()), importExprHandle(target, sourcePredicate));
        EXPECT_EQ(handle(maximum.range()), importAddressHandle(target, sourceRange));
        EXPECT_EQ(handle(maximum.body()), importExprHandle(target, sourceBody));
        EXPECT_NE(handle(quantifier.predicate()), sourcePredicate);
        EXPECT_NE(handle(maximum.body()), sourceBody);
    }

    TEST(ExprFacadeTest, InternalBridgeImportsOwnedAndCrossFactoryHandles) {
        symbolic::ExprFactory source;
        auto sourceExpr =
            binaryHandle(source, literalHandle(source, 1), symbolic::BinaryOp::Add, literalHandle(source, 2));

        symbolic::ExprFactory target;
        auto targetExpr =
            binaryHandle(target, literalHandle(target, 1), symbolic::BinaryOp::Add, literalHandle(target, 2));

        FacadeExprForTest owned{target, targetExpr};
        FacadeExprForTest imported{target, sourceExpr};
        FacadeExprForTest sourceFacade{source, sourceExpr};
        auto facadeImported = sourceFacade.importedInto(target);

        EXPECT_EQ(&owned.factory(), &target);
        EXPECT_EQ(&imported.factory(), &target);
        EXPECT_EQ(handle(owned), targetExpr);
        EXPECT_EQ(handle(imported), targetExpr);
        EXPECT_EQ(facadeImported, imported);
        EXPECT_EQ(&facadeImported.factory(), &target);
        EXPECT_NE(handle(imported), sourceExpr);
    }

    TEST(AddrFacadeTest, ImportsCrossFactoryAddressThroughCurrentFactory) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto nodeHandle = variableAddressHandle(factory, var);
        FacadeAddrForTest addr{nodeHandle};
        symbolic::ExprFactory sourceFactory;
        auto source = variableAddressHandle(sourceFactory, var);
        FacadeAddrForTest sourceFacade{sourceFactory, source};
        FacadeAddrForTest imported{source};
        FacadeAddrForTest explicitlyImported{factory, source};
        auto facadeImported = sourceFacade.importedInto(factory);
        auto boxedImported = symbolic::AddressBox{sourceFacade}.importedInto(factory);

        EXPECT_EQ(&addr.factory(), &factory);
        EXPECT_EQ(handle(addr), nodeHandle);
        EXPECT_EQ(handle(imported), nodeHandle);
        EXPECT_EQ(handle(explicitlyImported), nodeHandle);
        EXPECT_EQ(facadeImported, addr);
        EXPECT_EQ(&facadeImported.factory(), &factory);
        EXPECT_EQ(boxedImported, addr);
        EXPECT_EQ(&boxedImported.factory(), &factory);
        EXPECT_EQ(addr, imported);
        EXPECT_EQ(addr, explicitlyImported);
        EXPECT_TRUE(addr.isVariableAddress());
        EXPECT_EQ(addr.getFromRoot(), var);
        EXPECT_EQ(addr.getDimension(), 0);
        EXPECT_EQ(handle(addr.asExpr()), nodeHandle.asExpr());
    }

    TEST(AddrFacadeTest, IdentityIncludesFactory) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);

        symbolic::ExprFactory leftFactory;
        symbolic::ExprFactory rightFactory;

        symbolic::Addr left = [&] {
            symbolic::ExprFactoryScope scope(leftFactory);
            return FacadeAddrForTest{variableAddressHandle(leftFactory, var)};
        }();
        symbolic::Addr right = [&] {
            symbolic::ExprFactoryScope scope(rightFactory);
            return FacadeAddrForTest{variableAddressHandle(rightFactory, var)};
        }();

        EXPECT_FALSE(left == right);
        EXPECT_TRUE(left.structurallyEqual(right));
    }

    TEST(AddrFacadeTest, RebuildHelpersUseOwningFactory) {
        ASTExtractor e;
        e.init(R"c(
            struct S {
                int a;
                int b;
            };

            void f(void) {
                struct S s;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto *record = e.findFirstDecl<RecordDecl>();
        ASSERT_NE(record, nullptr);
        ASSERT_TRUE(record->isCompleteDefinition());
        record = record->getDefinition();
        auto *firstField = *record->field_begin();
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        FacadeAddrForTest varAddr{variableAddressHandle(factory, var)};
        FacadeAddrForTest base{symbolAddressHandle(
            factory, var->getType(), std::optional<symbolic::detail::AddrHandle>{handle(varAddr)},
            point)};
        symbolic::LiteralExpr offset{4};
        symbolic::LiteralExpr length{3};
        FacadeExprForTest extra{unknownHandle(factory)};

        auto shifted = base.withOffset(offset);
        auto ranged = shifted.withLength(length);
        auto addedOffset = base.withAddedOffset(extra);
        auto subtractedOffset = base.withSubtractedOffset(extra);
        auto addedLength = base.withAddedLength(extra);
        auto scalar = ranged.withoutLength();
        auto field = varAddr.field(firstField->getType(), record, 0);

        EXPECT_EQ(handle(shifted), withOffsetHandle(factory, handle(base), handle(offset)));
        EXPECT_EQ(handle(ranged), withLengthHandle(factory, handle(shifted), handle(length)));
        EXPECT_EQ(handle(addedOffset), withAddedOffsetHandle(factory, handle(base), handle(extra)));
        EXPECT_EQ(handle(subtractedOffset),
                  withSubtractedOffsetHandle(factory, handle(base), handle(extra)));
        EXPECT_EQ(handle(addedLength), withAddedLengthHandle(factory, handle(base), handle(extra)));
        EXPECT_EQ(handle(scalar), withoutLengthHandle(factory, handle(ranged)));
        EXPECT_EQ(handle(field),
                  fieldAddressHandle(factory, firstField->getType(), record, handle(varAddr), 0));
    }

    TEST(AddrFacadeTest, SymbolAddressFacadeOwnsChildFacades) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int values[8];
                return values[0];
            }
        )c");

        auto *func = e.findFunc("f");
        auto *var  = e.findFirstDecl<VarDecl>();
        ASSERT_NE(func, nullptr);
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto origin = symbolic::Addr::variable(var);
        symbolic::LiteralExpr offset{2};
        symbolic::LiteralExpr length{4};
        auto range = symbolic::Addr::symbol(var->getType(), origin, point, offset, length);

        auto address = symbolic::SymbolAddress::tryFrom(range);
        ASSERT_TRUE(address);
        ASSERT_TRUE(address->from());
        ASSERT_TRUE(address->length());
        ASSERT_TRUE(address->rightBound());
        EXPECT_EQ(&address->factory(), &factory);
        EXPECT_EQ(address->pointeeType(), var->getType());
        EXPECT_EQ(*address->from(), origin);
        EXPECT_EQ(address->fromPoint(), point);
        EXPECT_EQ(address->offset(), offset);
        EXPECT_EQ(*address->length(), length);
        EXPECT_EQ(address->rightBound()->tryEvalAsConstant(), 6);
        EXPECT_EQ(address->baseInfo().fromAddress(factory), origin);

        symbolic::ExprFactory targetFactory;
        auto importedOrigin = address->baseInfo().fromAddress(targetFactory);
        ASSERT_TRUE(importedOrigin);
        EXPECT_EQ(&importedOrigin->factory(), &targetFactory);
        EXPECT_TRUE(importedOrigin->structurallyEqual(origin));
        EXPECT_EQ(address->baseInfo(), address->baseInfo());
        EXPECT_FALSE(symbolic::SymbolAddress::tryFrom(origin));
    }

    TEST(AddrFacadeTest, RebuildHelpersRejectDifferentFactories) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory leftFactory;
        symbolic::ExprFactory rightFactory;

        symbolic::Addr base = [&] {
            symbolic::ExprFactoryScope scope(leftFactory);
            return FacadeAddrForTest{
                symbolAddressHandle(leftFactory, var->getType(),
                                    std::optional<symbolic::detail::AddrHandle>{
                                        variableAddressHandle(leftFactory, var)},
                                    point)};
        }();
        symbolic::Expr offset = [&] {
            symbolic::ExprFactoryScope scope(rightFactory);
            return FacadeExprForTest{symbolic::LiteralExpr{4}};
        }();

        ASSERT_DEATH({ (void)base.withOffset(offset); }, "");
        ASSERT_DEATH({ (void)symbolic::Addr::symbol(var->getType(), base, point, offset); }, "");
    }

    TEST(ExprFactoryTest, AddressBuildersReuseEqualAddressNodes) {
        ASTExtractor e;
        e.init(R"c(
            struct S {
                int a;
                int b;
            };

            void f(void) {
                struct S s;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto *record = e.findFirstDecl<RecordDecl>();
        ASSERT_NE(record, nullptr);
        ASSERT_TRUE(record->isCompleteDefinition());
        record = record->getDefinition();
        ASSERT_FALSE(record->fields().empty());
        auto *firstField = *record->field_begin();

        symbolic::ExprFactory factory;
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        auto varAddrA = variableAddressHandle(factory, var);
        auto varAddrB = variableAddressHandle(factory, var);
        EXPECT_EQ(varAddrA, varAddrB);
        EXPECT_TRUE(varAddrA.isVariableAddress());
        symbolic::VariableAddress variable{FacadeAddrForTest{factory, varAddrA}};
        EXPECT_EQ(&variable.factory(), &factory);
        EXPECT_EQ(variable.declaration().get(), var);
        EXPECT_EQ(handle(variable), varAddrA);

        auto handleBox = symbolic::detail::FacadeAccess::makeAddressBox(varAddrA);
        symbolic::AddressBox copiedHandleBox{handleBox};
        EXPECT_EQ(handle(handleBox), varAddrA);
        EXPECT_EQ(handle(copiedHandleBox), varAddrA);

        auto symbolValue = symbolValueHandle(factory,
            symbolic::ExprType{symbolic::ExprScalarKind::Int, 32},
            varAddrA, point);
        EXPECT_EQ(symbolic::detail::SymbolValueView{symbolValue}.from(), varAddrA);

        auto defaultSymAddr =
            symbolAddressHandle(factory, firstField->getType(),
                                std::optional<symbolic::detail::AddrHandle>{varAddrA}, point);
        symbolic::detail::SymbolAddressView defaultSymAddrView{defaultSymAddr};
        ASSERT_TRUE(defaultSymAddrView.from());
        EXPECT_EQ(*defaultSymAddrView.from(), varAddrA);
        auto defaultBase = defaultSymAddrView.baseInfo();
        auto defaultBaseFrom = defaultBase.fromAddress(factory);
        ASSERT_TRUE(defaultBaseFrom);
        EXPECT_EQ(&defaultBaseFrom->factory(), &factory);
        EXPECT_EQ(handle(*defaultBaseFrom), varAddrA);
        auto copiedBase = defaultBase;
        auto copiedBaseFrom = copiedBase.fromAddress(factory);
        ASSERT_TRUE(copiedBaseFrom);
        EXPECT_EQ(&copiedBaseFrom->factory(), &factory);
        EXPECT_EQ(handle(*copiedBaseFrom), varAddrA);
        EXPECT_EQ(defaultSymAddrView.offset(),
                  literalHandle(factory, static_cast<int64_t>(
                      symbolic::detail::SymbolAddressView::ZERO_OFFSET)));

        auto offset = literalHandle(factory, 4);
        auto length = literalHandle(factory, 2);
        auto symAddrA = symbolAddressHandle(
            factory, firstField->getType(), std::optional<symbolic::detail::AddrHandle>{varAddrA},
            point, std::optional<symbolic::detail::ExprHandle>{offset},
            std::optional<symbolic::detail::ExprHandle>{length});
        auto symAddrB = symbolAddressHandle(
            factory, firstField->getType(), std::optional<symbolic::detail::AddrHandle>{varAddrB},
            point, std::optional<symbolic::detail::ExprHandle>{literalHandle(factory, 4)},
            std::optional<symbolic::detail::ExprHandle>{literalHandle(factory, 2)});
        EXPECT_EQ(symAddrA, symAddrB);
        EXPECT_TRUE(symAddrA.isSymbolAddress());
        symbolic::detail::SymbolAddressView symbolView{symAddrA};
        EXPECT_EQ(symbolView.from(), varAddrA);
        EXPECT_EQ(symbolView.fromPoint(), point);
        EXPECT_EQ(symbolView.offset(), offset);
        EXPECT_EQ(symbolView.length(), length);
        EXPECT_EQ(symbolView.pointeeType(), firstField->getType());
        EXPECT_EQ(symbolView.fromRoot().value().get(), var);
        EXPECT_EQ(symbolView.dimension(), 1);

        auto fieldAddrA = fieldAddressHandle(factory, firstField->getType(), record, varAddrA, 0);
        auto fieldAddrB = fieldAddressHandle(factory, firstField->getType(), record, varAddrB, 0);
        EXPECT_EQ(fieldAddrA, fieldAddrB);
        EXPECT_TRUE(fieldAddrA.isFieldAddress());
        symbolic::FieldAddress field{FacadeAddrForTest{factory, fieldAddrA}};
        EXPECT_EQ(&field.factory(), &factory);
        EXPECT_EQ(field.definition().get(), record);
        EXPECT_EQ(handle(field.base()), varAddrA);
        EXPECT_EQ(&field.base().factory(), &factory);
        EXPECT_EQ(field.fieldIndex(), 0u);

        EXPECT_FALSE(symbolic::VariableAddress::tryFrom(FacadeAddrForTest{factory, fieldAddrA}));
        EXPECT_FALSE(symbolic::FieldAddress::tryFrom(FacadeAddrForTest{factory, symAddrA}));
        EXPECT_FALSE(symbolic::detail::SymbolAddressView::tryFrom(varAddrA).has_value());
    }

    TEST(ExprFactoryTest, ImportsAddressAndSymbolValueGraphsIntoTargetFactory) {
        ASTExtractor e;
        e.init(R"c(
            struct S { int field; };
            void f(void) { struct S value; }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto *record = e.findFirstDecl<RecordDecl>();
        ASSERT_NE(record, nullptr);
        record = record->getDefinition();
        ASSERT_NE(record, nullptr);
        auto *field = *record->field_begin();
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory source;
        auto sourceVariable = variableAddressHandle(source, var);
        auto sourceField =
            fieldAddressHandle(source, field->getType(), record, sourceVariable, field->getFieldIndex());
        auto sourceValue = symbolValueHandle(source, symbolic::deriveType(field->getType()),
                                              sourceField, point);

        symbolic::ExprFactory target;
        auto importedVariable = importAddressHandle(target, sourceVariable);
        auto importedField    = importAddressHandle(target, sourceField);
        auto importedValue    = importExprHandle(target, sourceValue);

        auto expectedVariable = variableAddressHandle(target, var);
        auto expectedField =
            fieldAddressHandle(target, field->getType(), record, expectedVariable, field->getFieldIndex());
        auto expectedValue = symbolValueHandle(target, symbolic::deriveType(field->getType()),
                                                expectedField, point);

        EXPECT_EQ(importedVariable, expectedVariable);
        EXPECT_EQ(importedField, expectedField);
        EXPECT_EQ(importedValue, expectedValue);
        EXPECT_EQ(symbolic::detail::FieldAddressView{importedField}.base(), expectedVariable);
        EXPECT_EQ(symbolic::detail::SymbolValueView{importedValue}.from(), expectedField);
        EXPECT_NE(importedVariable, sourceVariable);
        EXPECT_NE(importedField, sourceField);
        EXPECT_NE(importedValue, sourceValue);
    }

    TEST(ExprFactoryTest, StructureBuilderInitializesFieldHandles) {
        ASTExtractor e;
        e.init(R"c(
            struct S {
                int a;
                int *p;
                int arr[2];
            };

            void f(void) {
                struct S s;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto *record = e.findFirstDecl<RecordDecl>();
        ASSERT_NE(record, nullptr);
        ASSERT_TRUE(record->isCompleteDefinition());
        record = record->getDefinition();
        auto &layout = record->getASTContext().getASTRecordLayout(record);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto varAddr = variableAddressHandle(factory, var);
        auto structure = structureHandle(factory, record, layout, varAddr, point);
        symbolic::StructureExpr structureExpr{FacadeExprForTest{factory, structure}};
        EXPECT_TRUE(structureExpr.isStructure());
        EXPECT_TRUE(symbolic::StructureExpr::tryFrom(structureExpr).has_value());
        EXPECT_FALSE(symbolic::StructureExpr::tryFrom(
            FacadeExprForTest{factory, literalHandle(factory, 0)}));
        EXPECT_EQ(&structureExpr.factory(), &factory);
        EXPECT_EQ(structureExpr.info().definition_.get(), record);
        EXPECT_EQ(structureExpr.fromPoint(), point);

        auto structureFrom = structureExpr.sourceAddress();
        ASSERT_TRUE(structureFrom);
        EXPECT_EQ(handle(*structureFrom), varAddr);

        ASSERT_EQ(structureExpr.size(), 3u);
        auto field0 = structureExpr.field(0);
        auto field1 = structureExpr.field(1);
        auto field2 = structureExpr.field(2);

        EXPECT_TRUE(field0.isSymbolValue());
        auto field1Address = field1.tryAsAddress();
        ASSERT_TRUE(field1Address);
        EXPECT_TRUE(symbolic::SymbolAddress::tryFrom(*field1Address));
        auto field2Address = field2.tryAsAddress();
        ASSERT_TRUE(field2Address);
        auto arrayAddr = symbolic::SymbolAddress::tryFrom(*field2Address);
        ASSERT_TRUE(arrayAddr);
        ASSERT_TRUE(arrayAddr->length());

        EXPECT_EQ(handle(field0), importExprHandle(factory, handle(field0)));
        EXPECT_EQ(handle(field1), importExprHandle(factory, handle(field1)));
        EXPECT_EQ(handle(field2), importExprHandle(factory, handle(field2)));
        EXPECT_EQ(handle(*arrayAddr->length()), literalHandle(factory, uint64_t{2}));

    }

    TEST(ExprFacadeTest, SymbolBuildersCoverScalarPointerArrayAndStructure) {
        ASTExtractor e;
        e.init(R"c(
            struct S {
                int a;
            };

            void f(void) {
                int scalar;
                int *ptr;
                int arr[2];
                struct S st;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *scalar = e.findNthDecl<VarDecl>(1);
        auto *ptr = e.findNthDecl<VarDecl>(2);
        auto *arr = e.findNthDecl<VarDecl>(3);
        auto *st = e.findNthDecl<VarDecl>(4);
        ASSERT_NE(scalar, nullptr);
        ASSERT_NE(ptr, nullptr);
        ASSERT_NE(arr, nullptr);
        ASSERT_NE(st, nullptr);
        ASSERT_TRUE(scalar->getType()->isIntegerType());
        ASSERT_TRUE(ptr->getType()->isPointerType());
        ASSERT_TRUE(arr->getType()->isArrayType());
        ASSERT_TRUE(st->getType()->isStructureType());

        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto scalarAddr    = FacadeAddrForTest{factory, variableAddressHandle(factory, scalar)};
        auto ptrAddr       = FacadeAddrForTest{factory, variableAddressHandle(factory, ptr)};
        auto arrAddr       = FacadeAddrForTest{factory, variableAddressHandle(factory, arr)};
        auto structureAddr = FacadeAddrForTest{factory, variableAddressHandle(factory, st)};
        auto scalarSym = handle(symbolic::Expr::symbol(scalar->getType(), scalarAddr, point));
        auto ptrSym    = handle(symbolic::Expr::symbol(ptr->getType(), ptrAddr, point));
        auto unbasedPtrSym = handle(symbolic::Expr::symbol(ptr->getType(), point));
        auto arrSym         = handle(symbolic::Expr::symbol(arr->getType(), arrAddr, point));
        auto stSym          = handle(symbolic::Expr::symbol(st->getType(), structureAddr, point));

        EXPECT_EQ(scalarSym,
                  symbolValueHandle(factory, symbolic::deriveType(scalar->getType()),
                                      variableAddressHandle(factory, scalar), point));
        auto pointerType = llvm::cast<PointerType>(ptr->getType());
        EXPECT_EQ(ptrSym, symbolAddressHandle(factory, pointerType->getPointeeType(),
                                              variableAddressHandle(factory, ptr), point)
                              .asExpr());
        EXPECT_EQ(unbasedPtrSym,
                  handle(symbolic::Addr::symbol(factory, pointerType->getPointeeType(), point)
                             .asExpr()));
        auto arrayType = llvm::cast<ArrayType>(arr->getType());
        EXPECT_EQ(arrSym, symbolAddressHandle(factory, arrayType->getElementType(),
                                              variableAddressHandle(factory, arr), point)
                              .asExpr());

        auto *record = st->getType()->getAsRecordDecl();
        ASSERT_NE(record, nullptr);
        ASSERT_TRUE(record->isCompleteDefinition());
        record = record->getDefinition();
        auto &layout = record->getASTContext().getASTRecordLayout(record);
        EXPECT_EQ(stSym,
                  structureHandle(factory, record, layout, variableAddressHandle(factory, st), point));
    }

    TEST(ExprFactoryTest, StructureBuilderUsesFactoryFields) {
        ASTExtractor e;
        e.init(R"c(
            struct Inner {
                int z;
            };

            struct Outer {
                int a;
                int arr[3];
                struct Inner inner;
            };

            void f(void) {
                struct Outer st;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *st = e.findFirstDecl<VarDecl>();
        ASSERT_NE(st, nullptr);
        ASSERT_TRUE(st->getType()->isStructureType());
        auto *record = st->getType()->getAsRecordDecl();
        ASSERT_NE(record, nullptr);
        ASSERT_TRUE(record->isCompleteDefinition());
        record = record->getDefinition();
        auto &layout = record->getASTContext().getASTRecordLayout(record);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto structureNode =
            structureHandle(factory, record, layout, variableAddressHandle(factory, st), point);
        symbolic::detail::StructureView structure{structureNode};

        std::vector<const FieldDecl *> fields;
        for (const auto *field : record->fields())
            fields.push_back(field);
        ASSERT_EQ(fields.size(), 3u);

        auto fromHandle = variableAddressHandle(factory, st);
        auto field0Addr = fieldAddressHandle(factory, fields[0]->getType(), record, fromHandle, 0);
        auto expectedField0 =
            symbolValueHandle(factory, symbolic::deriveType(fields[0]->getType()), field0Addr, point);
        EXPECT_EQ(structure.field(0), expectedField0);

        auto field1Addr = fieldAddressHandle(factory, fields[1]->getType(), record, fromHandle, 1);
        auto arrayType = llvm::cast<ArrayType>(fields[1]->getType());
        auto expectedField1 =
            symbolAddressHandle(factory, arrayType->getElementType(), field1Addr, point,
                                std::nullopt, literalHandle(factory, uint64_t{3}));
        EXPECT_EQ(structure.field(1), expectedField1.asExpr());

        auto field2Addr = fieldAddressHandle(factory, fields[2]->getType(), record, fromHandle, 2);
        auto *nestedRecord = fields[2]->getType()->getAsRecordDecl();
        ASSERT_NE(nestedRecord, nullptr);
        ASSERT_TRUE(nestedRecord->isCompleteDefinition());
        nestedRecord = nestedRecord->getDefinition();
        auto &nestedLayout =
            nestedRecord->getASTContext().getASTRecordLayout(nestedRecord);
        auto expectedField2 =
            structureHandle(factory, nestedRecord, nestedLayout, field2Addr, point);
        EXPECT_EQ(structure.field(2), expectedField2);
    }

    TEST(ExprFactoryTest, StructureBuilderInitializesUnknownFieldHandles) {
        ASTExtractor e;
        e.init(R"c(
            struct Inner {
                int z;
            };

            struct Outer {
                int a;
                int arr[2];
                struct Inner inner;
            };

            int f(void) {
                struct Outer st;
                return 0;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto *record = var->getType()->getAsRecordDecl();
        ASSERT_NE(record, nullptr);
        ASSERT_TRUE(record->isCompleteDefinition());
        record = record->getDefinition();
        auto &layout = record->getASTContext().getASTRecordLayout(record);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto sizeBefore = symbolic::detail::ExprFactoryInternals::size(factory);
        auto structureExpr = structureHandle(factory, record, layout, variableAddressHandle(factory, var), point);
        EXPECT_GT(symbolic::detail::ExprFactoryInternals::size(factory), sizeBefore);

        auto expected = structureHandle(factory, record, layout, variableAddressHandle(factory, var), point);
        EXPECT_EQ(structureExpr, expected);
        symbolic::detail::StructureView structure{structureExpr};
        symbolic::detail::StructureView expectedStructure{expected};
        for (size_t i = 0; i < expectedStructure.size(); ++i)
            EXPECT_EQ(structure.field(i), expectedStructure.field(i));
    }

    TEST(ExprFactoryTest, ScopedAddressSubstitutionRebuildsThroughFactory) {
        ASTExtractor e;
        e.init(R"c(
            struct S {
                int a;
            };

            void f(void) {
                int x;
                struct S s;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *x = e.findNthDecl<VarDecl>(1);
        auto *s = e.findNthDecl<VarDecl>(2);
        auto *record = e.findFirstDecl<RecordDecl>();
        ASSERT_NE(x, nullptr);
        ASSERT_NE(s, nullptr);
        ASSERT_NE(record, nullptr);
        ASSERT_TRUE(record->isCompleteDefinition());
        record = record->getDefinition();
        ASSERT_FALSE(record->fields().empty());
        auto *firstField = *record->field_begin();
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto varAddr = variableAddressHandle(factory, x);
        auto rangeIndex = rangeIndexHandle(factory, "i");
        auto indexedAddr =
            symbolAddressHandle(factory, x->getType(), varAddr, point, rangeIndex, rangeIndex);
        auto index = literalHandle(factory, int64_t{4});
        auto rangeBase = symbolic::detail::SymbolAddressView{indexedAddr}.baseInfo();

        auto substitutedAddr =
            substituteRangeIndexForTest(factory, indexedAddr.asExpr(), rangeBase, index);
        EXPECT_EQ(
            substitutedAddr,
            symbolAddressHandle(factory, x->getType(), varAddr, point, index, index).asExpr());

        auto fieldAddr = fieldAddressHandle(factory,
            firstField->getType(), record, variableAddressHandle(factory, s), 0);
        auto substitutedField =
            substituteRangeIndexForTest(factory, fieldAddr.asExpr(), rangeBase, index);
        EXPECT_EQ(substitutedField, fieldAddr.asExpr());

        auto indexedStructAddr =
            symbolAddressHandle(factory, s->getType(), variableAddressHandle(factory, s), point,
                                rangeIndex, rangeIndex);
        auto indexedFieldAddr =
            fieldAddressHandle(factory, firstField->getType(), record, indexedStructAddr, 0);
        auto indexedRangeBase = symbolic::detail::SymbolAddressView{indexedStructAddr}.baseInfo();
        auto substitutedIndexedField = substituteRangeIndexForTest(
            factory, indexedFieldAddr.asExpr(), indexedRangeBase, index);
        auto expectedStructAddr = symbolAddressHandle(
            factory, s->getType(), variableAddressHandle(factory, s), point, index, index);
        EXPECT_EQ(substitutedIndexedField,
                  fieldAddressHandle(factory, firstField->getType(), record, expectedStructAddr, 0)
                      .asExpr());
    }

    TEST(ExprFactoryTest, ScopedTryEvalSymbolAddressImportsThroughFactory) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory setupFactory;
        auto source = handle(symbolic::Addr::symbol(setupFactory, var->getType(), point)
                                 .withOffset(symbolic::LiteralExpr{setupFactory, int64_t{4}}));

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto sizeBefore = symbolic::detail::ExprFactoryInternals::size(factory);
        auto evaluated  = evaluateAddressForTest(factory, source.asExpr());
        ASSERT_TRUE(evaluated);
        EXPECT_GT(symbolic::detail::ExprFactoryInternals::size(factory), sizeBefore);
        EXPECT_EQ(evaluated.value(),
                  symbolAddressHandle(factory, var->getType(), std::nullopt, point,
                                      literalHandle(factory, int64_t{4})));
    }

    TEST(ExprFactoryTest, TryEvalSymbolAddressBinaryOffsetUsesHandleSimplification) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory setupFactory;
        auto legacyAddr = handle(
            symbolic::Addr::symbol(setupFactory, var->getType(), point));
        auto legacyAdd = binaryHandle(setupFactory,
            legacyAddr.asExpr(), symbolic::BinaryOp::Add,
            literalHandle(setupFactory, int64_t{4}));

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto expected = symbolAddressHandle(factory, var->getType(), std::nullopt, point,
                                            literalHandle(factory, int64_t{4}));

        auto evaluatedHandle = evaluateAddressForTest(factory, legacyAdd);
        ASSERT_TRUE(evaluatedHandle);
        EXPECT_EQ(*evaluatedHandle, expected);
        EXPECT_EQ(symbolic::detail::SymbolAddressView{evaluatedHandle.value()}.offset(),
                  literalHandle(factory, int64_t{4}));

    }

    TEST(ExprFactoryTest, TryEvalSymbolAddressPreservesDirectionalArithmeticRules) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto address = symbolAddressHandle(factory, var->getType(), std::nullopt, point);
        auto offset  = literalHandle(factory, int64_t{4});

        auto commutedAdd = binaryHandle(factory,
            offset, symbolic::BinaryOp::Add, address.asExpr());
        auto evaluated = evaluateAddressForTest(factory, commutedAdd);
        ASSERT_TRUE(evaluated);
        EXPECT_EQ(*evaluated,
                  symbolAddressHandle(factory, var->getType(), std::nullopt, point, offset));

        auto invalidSubtract = binaryHandle(factory,
            offset, symbolic::BinaryOp::Subtract, address.asExpr());
        EXPECT_FALSE(evaluateAddressForTest(factory, invalidSubtract));

        auto twoAddresses = binaryHandle(factory,
            address.asExpr(), symbolic::BinaryOp::Add, address.asExpr());
        EXPECT_FALSE(evaluateAddressForTest(factory, twoAddresses));
    }

    TEST(ExprFactoryTest, AddressRebuildsReuseInternedRangeChildren) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto varAddr = variableAddressHandle(factory, var);
        auto zero    = literalHandle(factory, 0);
        auto length  = literalHandle(factory, 3);
        auto offset  = literalHandle(factory, 4);
        auto extra   = unknownHandle(factory);

        auto base = symbolAddressHandle(
            factory, var->getType(), std::optional<symbolic::detail::AddrHandle>{varAddr}, point,
            std::optional<symbolic::detail::ExprHandle>{zero}, std::nullopt);

        auto ranged = withLengthHandle(factory, base, length);
        EXPECT_EQ(ranged, withLengthHandle(factory, base, length));
        symbolic::detail::SymbolAddressView rangedView{ranged};
        EXPECT_EQ(rangedView.offset(), zero);
        ASSERT_TRUE(rangedView.length());
        EXPECT_EQ(rangedView.length().value(), length);

        auto shifted = withOffsetHandle(factory, ranged, offset);
        EXPECT_EQ(shifted, withOffsetHandle(factory, ranged, offset));
        symbolic::detail::SymbolAddressView shiftedView{shifted};
        EXPECT_EQ(shiftedView.offset(), offset);
        ASSERT_TRUE(shiftedView.length());
        EXPECT_EQ(shiftedView.length().value(), length);

        auto scalarAddr = withoutLengthHandle(factory, shifted);
        EXPECT_EQ(scalarAddr, withoutLengthHandle(factory, shifted));
        symbolic::detail::SymbolAddressView scalarView{scalarAddr};
        EXPECT_EQ(scalarView.offset(), offset);
        EXPECT_FALSE(scalarView.length());

        auto addedOffset = withAddedOffsetHandle(factory, base, extra);
        EXPECT_EQ(addedOffset, withAddedOffsetHandle(factory, base, extra));
        symbolic::detail::SymbolAddressView addedOffsetView{addedOffset};
        EXPECT_EQ(addedOffsetView.offset(),
                  simplifiedBinaryHandle(factory, zero, symbolic::BinaryOp::Add, extra));

        auto subtractedOffset = withSubtractedOffsetHandle(factory, base, extra);
        EXPECT_EQ(subtractedOffset, withSubtractedOffsetHandle(factory, base, extra));
        symbolic::detail::SymbolAddressView subtractedOffsetView{subtractedOffset};
        EXPECT_EQ(subtractedOffsetView.offset(),
                  simplifiedBinaryHandle(factory, zero, symbolic::BinaryOp::Subtract, extra));

        auto addedLength = withAddedLengthHandle(factory, base, extra);
        EXPECT_EQ(addedLength, withAddedLengthHandle(factory, base, extra));
        auto expectedAddedLength = simplifiedBinaryHandle(factory,
            literalHandle(factory, 1), symbolic::BinaryOp::Add, extra);
        symbolic::detail::SymbolAddressView addedLengthView{addedLength};
        ASSERT_TRUE(addedLengthView.length());
        EXPECT_EQ(addedLengthView.length().value(), expectedAddedLength);

        auto extendedLength = withAddedLengthHandle(factory, ranged, extra);
        EXPECT_EQ(extendedLength, withAddedLengthHandle(factory, ranged, extra));
        auto expectedExtendedLength =
            simplifiedBinaryHandle(factory, length, symbolic::BinaryOp::Add, extra);
        symbolic::detail::SymbolAddressView extendedLengthView{extendedLength};
        ASSERT_TRUE(extendedLengthView.length());
        EXPECT_EQ(extendedLengthView.length().value(), expectedExtendedLength);
    }

    TEST(ExprFactoryTest, ImportsCrossFactorySymbolAddressRangeChildrenAsHandles) {
        ASTExtractor e;
        e.init(R"c(
            int f(void) {
                int x = 0;
                return x;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory setupFactory;
        auto source = symbolAddressHandle(
            setupFactory, var->getType(), variableAddressHandle(setupFactory, var), point,
            literalHandle(setupFactory, int64_t{4}), literalHandle(setupFactory, int64_t{2}));
        symbolic::detail::SymbolAddressView sourceView{source};

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto importedA       = importExprHandle(factory, source.asExpr());
        auto importedB       = importExprHandle(factory, source.asExpr());
        auto importedAddress = importAddressHandle(factory, source);
        EXPECT_EQ(importedA, importedB);
        EXPECT_EQ(importedAddress.asExpr(), importedA);

        auto importedView = symbolic::detail::SymbolAddressView::tryFrom(importedA).value();
        auto importedOffset = importExprHandle(factory, sourceView.offset());
        ASSERT_TRUE(importedView.length());
        ASSERT_TRUE(sourceView.length());
        auto importedLength = importExprHandle(factory, sourceView.length().value());

        EXPECT_EQ(importedView.offset(), importedOffset);
        EXPECT_EQ(importedView.length().value(), importedLength);
    }

    TEST(ExprFactoryTest, ImportsLegacyStructureFieldsAsHandles) {
        ASTExtractor e;
        e.init(R"c(
            struct S {
                int a;
                int b;
            };

            void f(void) {
                struct S s;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory setupFactory;
        auto legacyExpr = [&]() {
            symbolic::ExprFactoryScope setupScope(setupFactory);
            return makeStructureWithFacade(setupFactory, var->getType(), var, point);
        }();
        symbolic::detail::StructureView legacyStructure{legacyExpr};

        symbolic::ExprFactory factory;
        auto importedA = importExprHandle(factory, handle(legacyStructure));
        auto importedB = importExprHandle(factory, handle(legacyStructure));
        EXPECT_EQ(importedA, importedB);

        symbolic::detail::StructureView importedStructure{importedA};
        auto importedField0 = importExprHandle(factory, legacyStructure.field(0));
        auto importedField1 = importExprHandle(factory, legacyStructure.field(1));

        EXPECT_EQ(importedStructure.field(0), importedField0);
        EXPECT_EQ(importedStructure.field(1), importedField1);
    }

    TEST(StructureRebuildTest, FieldUpdateDoesNotMutateOriginalStructure) {
        ASTExtractor e;
        e.init(R"c(
            struct S {
                int a;
                int b;
            };

            int f(void) {
                struct S s;
                return 0;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto *record = var->getType()->getAsRecordDecl()->getDefinition();
        auto &layout = record->getASTContext().getASTRecordLayout(record);
        auto structure = structureHandle(factory, record, layout, variableAddressHandle(factory, var), point);
        symbolic::detail::StructureView original{structure};
        auto originalField0 = importExprHandle(factory, original.field(0));
        auto originalField1 = importExprHandle(factory, original.field(1));
        auto replacement = literalHandle(factory, 42);

        auto updated = withFieldHandle(factory, structure, 0, replacement);
        EXPECT_EQ(updated, withFieldHandle(factory, structure, 0, replacement));

        FacadeExprForTest structureExpr{factory, structure};
        symbolic::LiteralExpr replacementExpr{factory, 42};
        auto facadeUpdated = structureExpr.withField(0, replacementExpr);
        EXPECT_EQ(handle(facadeUpdated), updated);
        EXPECT_EQ(&facadeUpdated.factory(), &factory);

        symbolic::detail::StructureView updatedView{updated};
        EXPECT_EQ(original.field(0), originalField0);
        EXPECT_EQ(original.field(1), originalField1);
        EXPECT_EQ(updatedView.field(0), replacement);
        EXPECT_EQ(updatedView.field(1), originalField1);
    }

    TEST(StructureRebuildTest, ScopedValueSubstitutionRebuildsFieldsThroughFactory) {
        ASTExtractor e;
        e.init(R"c(
            struct S {
                int a;
                int b;
            };

            int f(void) {
                struct S s;
                return 0;
            }
        )c");

        auto *func = e.findFunc("f");
        ASSERT_NE(func, nullptr);
        auto *var = e.findFirstDecl<VarDecl>();
        ASSERT_NE(var, nullptr);
        auto point =
            symbolic::SourcePoint::fromFuncDecl(func, e.getSourceManager(), e.getLangOptions());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto structureExpr = makeStructureWithFacade(factory, var->getType(), var, point);
        symbolic::detail::StructureView structure{structureExpr};
        auto replacement = literalHandle(factory, 42);

        symbolic::ExprSubstitutions substitutions;
        substitutions.insertOrAssign(FacadeExprForTest{factory, structure.field(0)},
                                     FacadeExprForTest{factory, replacement});

        auto substituted = substituteValuesForTest(factory, handle(structure), substitutions);
        symbolic::detail::StructureView substitutedStructure{substituted};

        EXPECT_EQ(substitutedStructure.field(0), replacement);
        EXPECT_EQ(substitutedStructure.field(1), importExprHandle(factory, structure.field(1)));
    }
} // namespace acslg::test::unit::analyzer
