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
#include "Symbolic/detail/factoryInternals.h"
#include "Symbolic/expr.h"
#include "testHelper.h"

using namespace clang;
using namespace std;

using ::testing::HasSubstr;

namespace acslg::test::unit::analyzer {
    using namespace acslg::analyzer;
    using namespace utils;

    static_assert(std::is_constructible_v<symbolic::LiteralExpr,
                                          symbolic::ExprFactory &,
                                          int64_t>);
    static_assert(std::is_copy_constructible_v<symbolic::LiteralExprView>);
    static_assert(std::is_copy_constructible_v<symbolic::UnaryExprView>);
    static_assert(std::is_copy_constructible_v<symbolic::BinaryExprView>);
    static_assert(std::is_copy_constructible_v<symbolic::SumOverRangeView>);
    static_assert(std::is_copy_constructible_v<symbolic::QuantifierOverRangeView>);
    static_assert(std::is_copy_constructible_v<symbolic::MaxMinOverRangeView>);
    static_assert(std::is_copy_constructible_v<symbolic::StructureView>);
    static_assert(std::is_copy_constructible_v<symbolic::SymbolValueView>);
    static_assert(!std::is_convertible_v<symbolic::AddressBox &, symbolic::Address &>);
    static_assert(!std::is_convertible_v<const symbolic::AddressBox &, const symbolic::Address &>);
    static_assert(std::is_same_v<decltype(&symbolic::simplifiedExprHandle),
                                 symbolic::ExprHandle (*)(symbolic::ExprFactory &,
                                                          symbolic::ExprHandle)>);
    static_assert(std::is_same_v<decltype(&symbolic::tryEvalAsSymbolAddrHandle),
                                 std::optional<symbolic::AddrHandle> (*)(
                                     symbolic::ExprFactory &, symbolic::ExprHandle)>);
    static_assert(!std::is_constructible_v<symbolic::Expr, const symbolic::SymbolicExpr &>);
    static_assert(!std::is_constructible_v<symbolic::Addr, const symbolic::Address &>);

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

        symbolic::ExprHandle makeStructureWithFacade(
            symbolic::ExprFactory &factory,
            clang::QualType type,
            ::acslg::utils::not_null<const clang::VarDecl *> var,
            symbolic::SourcePoint point) {
            auto *record = type->getAsRecordDecl()->getDefinition();
            auto &layout = record->getASTContext().getASTRecordLayout(record);
            auto from    = symbolic::Addr::variable(var);
            return factory.structure(record, layout, from.handle(), point);
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
        ExprHandle makeConstU64(uint64_t value) {
            return ExprFactoryScope::current().literal(value);
        }

        template <class ExprPtr> ExprHandle internForTest(const ExprPtr &expr) {
            return ExprFactoryScope::current().importExpr(expr);
        }

        ExprHandle literalHandleForTest(uint64_t value) {
            return ExprFactoryScope::current().literal(value);
        }

        ExprHandle makeAdd(ExprHandle a, ExprHandle b) {
            auto &factory = ExprFactoryScope::current();
            return factory.binary(factory.importExpr(a), BinaryOp::Add,
                                  factory.importExpr(b));
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
        mm.write(var0Addr, internForTest(makeConstU64(42)));

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto point   = getSourcePoint(0);
        auto varNode = makeSymbolValue(0, point);
        auto result =
            symbolic::getSubstitutedExprHandle(factory, varNode, *path, point);

        ASSERT_EQ(result.get().get(), factory.literal(uint64_t{42}).get().get());
    }

    TEST_F(SubstituteTest, ScopedVarReplacementImportsThroughFactory) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto var0Addr = makeVariableAddr(0);
        mm.write(var0Addr, internForTest(makeConstU64(42)));

        auto point   = getSourcePoint(0);
        auto varNode = makeSymbolValue(0, point);
        auto result =
            symbolic::getSubstitutedExprHandle(factory, varNode, *path, point);

        EXPECT_EQ(result.get().get(), factory.literal(uint64_t{42}).get().get());
    }

    TEST_F(SubstituteTest, PathSubstitutionHandleReplacesSymbolValueThroughFactory) {
        auto var0Addr = makeVariableAddr(0);
        mm.write(var0Addr, internForTest(makeConstU64(42)));

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto point = getSourcePoint(0);
        auto varNode = makeSymbolValue(0, point);
        auto result =
            symbolic::getSubstitutedExprHandle(factory, varNode, *path, point);

        EXPECT_EQ(result.get().get(), factory.literal(uint64_t{42}).get().get());
    }

    TEST_F(SubstituteTest, VarWithDifferentFromIsKeptUnchanged) {
        auto var0Addr = makeVariableAddr(0);
        mm.write(var0Addr, internForTest(makeConstU64(7)));

        auto point   = getSourcePoint(0);
        auto varNode = makeSymbolValue(0, point);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto exprBefore = factory.importExpr(varNode);
        auto result =
            symbolic::getSubstitutedExprHandle(factory, varNode, *path, getSourcePoint(42));

        ASSERT_EQ(*result, *exprBefore);
    }

    TEST_F(SubstituteTest, CompositeExprIsSubstitutedRecursively) {
        // loop-entry：g1 -> 1, g2 -> 2
        mm.write(makeVariableAddr(1), internForTest(makeConstU64(1)));
        mm.write(makeVariableAddr(2), internForTest(makeConstU64(2)));

        auto point = getSourcePoint(0);

        // expr = Var(g1, point) + Var(g2, point)
        auto aVar = makeSymbolValue(1, point);
        auto bVar = makeSymbolValue(2, point);
        auto expr = makeAdd(std::move(aVar), std::move(bVar));

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto expected = makeAdd(factory.literal(uint64_t{1}), factory.literal(uint64_t{2}));
        auto result = symbolic::getSubstitutedExprHandle(factory, expr, *path, point);
        ASSERT_EQ(*result, *expected);
    }

    TEST_F(SubstituteTest, PathSubstitutionHandleRebuildsCompositeExpression) {
        mm.write(makeVariableAddr(1), internForTest(makeConstU64(1)));
        mm.write(makeVariableAddr(2), internForTest(makeConstU64(2)));

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto point = getSourcePoint(0);
        auto aVar = makeSymbolValue(1, point);
        auto bVar = makeSymbolValue(2, point);
        auto expr = makeAdd(std::move(aVar), std::move(bVar));

        auto result = symbolic::getSubstitutedExprHandle(factory, expr, *path, point);
        auto expected = factory.binary(factory.literal(uint64_t{1}),
                                       symbolic::BinaryOp::Add,
                                       factory.literal(uint64_t{2}));

        EXPECT_EQ(result.get().get(), expected.get().get());
    }

    TEST_F(SubstituteTest, SymbolAddrResolvedBaseAndOffsetApplied) {
        auto originAddr = makeVariableAddr(3);
        auto realAddr   = makeSimpleSymbolAddr(4);
        mm.write(originAddr, realAddr.asExpr());

        // Var(g5) = 3
        mm.write(makeVariableAddr(5), internForTest(makeConstU64(3)));

        auto point = getSourcePoint(0);

        // symAddr: base=origin(g3), offset=(Var(g5,point) + 4), from=point
        auto vVar   = makeSymbolValue(5, point);
        auto offset = makeAdd(vVar, symbolic::ExprFactoryScope::current().literal(uint64_t{4}));
        auto sym = makeRangeAddr(/*origin id*/ 3, internForTest(offset), std::nullopt, point);

        // expect: realAddr (g4) + 7
        auto expected = makePointAddr(/*real id*/ 4, /*off*/ 7)->simplifiedExpr();

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto result = symbolic::getSubstitutedExprHandle(factory, sym.asExpr(), *path, point);
        ASSERT_EQ(*result->simplifiedExpr(), *expected);
    }

    TEST_F(SubstituteTest, SymbolAddrUnresolvedReturnsImportedHandle) {
        auto sym = makeSimpleSymbolAddr(/*origin id*/ 6);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto result =
            symbolic::getSubstitutedExprHandle(factory, sym.asExpr(), *path, defaultPoint);
        ASSERT_EQ(*result, *sym);
    }

    TEST_F(SubstituteTest, NonSymbolAddrReturnsImportedHandle) {
        auto varAddr = makeVariableAddr(7);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto result = symbolic::getSubstitutedExprHandle(factory, varAddr.asExpr(), *path,
                                                         defaultPoint);
        ASSERT_EQ(*result, *varAddr);
    }

    TEST_F(SubstituteTest, FromPointMismatchReturnsUnchangedSymbolAddr) {
        auto originAddr = makeVariableAddr(8);
        auto realAddr   = makeSimpleSymbolAddr(9);
        mm.write(originAddr, realAddr.asExpr());

        auto point = getSourcePoint(0);
        auto offset = makeConstU64(1);
        auto sym = makeRangeAddr(/*origin id*/ 8, internForTest(offset), std::nullopt, point);

        auto otherPoint = getSourcePoint(1);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto result = symbolic::getSubstitutedExprHandle(factory, sym.asExpr(), *path, otherPoint);
        ASSERT_EQ(*result, *sym);
    }

    TEST_F(SubstituteTest, ScopedFromPointMismatchImportsUnchangedSymbolAddr) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto originAddr = makeVariableAddr(8);
        auto realAddr   = makeSimpleSymbolAddr(9);
        mm.write(originAddr, realAddr.asExpr());

        auto point = getSourcePoint(0);
        auto offset = makeConstU64(1);
        auto sym = makeRangeAddr(/*origin id*/ 8, internForTest(offset), std::nullopt, point);

        auto otherPoint = getSourcePoint(1);
        auto result = symbolic::getSubstitutedExprHandle(factory, sym.asExpr(), *path, otherPoint);
        auto *resultAddr = symbolic::cast<symbolic::Address>(result.get().get());

        EXPECT_EQ(factory.importAddress(symbolic::AddrHandle{resultAddr}),
                  factory.importAddress(sym));
    }

    TEST_F(SubstituteTest, ScopedNoBaseSymbolAddrSubstitutionUsesFactory) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto var = getVarDecl(0);
        auto point = getSourcePoint(0);
        auto symHandle = factory.symbolAddress(var->getType(), std::nullopt, point);

        auto result =
            symbolic::getSubstitutedExprHandle(factory, symHandle.asExpr(), *path, point);
        auto *resultAddr = symbolic::cast<symbolic::Address>(result.get().get());

        EXPECT_EQ(factory.importAddress(symbolic::AddrHandle{resultAddr}), symHandle);
    }

    TEST_F(SubstituteTest, ResolvedValueNotAddressShouldError) {
        auto originAddr = makeVariableAddr(10);
        mm.write(originAddr, internForTest(makeConstU64(5)));

        auto point = getSourcePoint(0);
        auto offset = makeConstU64(0);
        auto sym = makeRangeAddr(/*origin id*/ 10, internForTest(offset), std::nullopt, point);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        ASSERT_DEATH(
            symbolic::getSubstitutedExprHandle(factory, sym.asExpr(), *path, point), "");
        SUCCEED();
    }

    class GetACSLTest : public FixtureWithCode {};

    // Test literal expressions for correct ACSL output.
    TEST_F(GetACSLTest, Literal_GetACSL) {
        SymbolicExpr::GetACSLConfig config;
        config.noStateLabelFunctionAt = true;

        symbolic::ExprFactory factory;

        // Boolean literal
        auto litTrue = factory.literal(true);
        auto resTrue = litTrue.getACSL(config);
        ASSERT_TRUE(resTrue);
        EXPECT_EQ(resTrue.value().first, "true");
        EXPECT_TRUE(resTrue.value().second.empty());

        // Integer literal
        auto litInt = factory.literal(123);
        auto resInt = litInt.getACSL(config);
        ASSERT_TRUE(resInt);
        EXPECT_EQ(resInt.value().first, "123");
        EXPECT_TRUE(resInt.value().second.empty());
    }

    // Test binary addition and operator precedence/parentheses.
    TEST_F(GetACSLTest, BinaryOp_AdditionAndPrecedence) {
        SymbolicExpr::GetACSLConfig config;
        config.noStateLabelFunctionAt = true;
        auto &factory = symbolic::ExprFactoryScope::current();

        // Simple addition: 5 + 3
        auto exprSimple = factory.binary(factory.literal(5), BinaryOp::Add,
                                         factory.literal(3));
        auto resSimple  = exprSimple->getACSL(config);
        ASSERT_TRUE(resSimple);
        EXPECT_EQ(resSimple.value().first, "5 + 3");

        // Nested addition (left-child nested): (1 + 2) + 3 -> "1 + 2 + 3"
        auto innerLeft = factory.binary(factory.literal(1), BinaryOp::Add,
                                        factory.literal(2));
        auto exprLeft =
            factory.binary(innerLeft, BinaryOp::Add, factory.literal(3));
        auto resLeft = exprLeft->getACSL(config);
        ASSERT_TRUE(resLeft);
        EXPECT_EQ(resLeft.value().first, "1 + 2 + 3");

        // Nested addition (right-child nested): 1 + (2 + 3) -> "1 + (2 + 3)"
        auto innerRight = factory.binary(factory.literal(2), BinaryOp::Add,
                                         factory.literal(3));
        auto exprRight =
            factory.binary(factory.literal(1), BinaryOp::Add, innerRight);
        auto resRight = exprRight->getACSL(config);
        ASSERT_TRUE(resRight);
        EXPECT_EQ(resRight.value().first, "1 + (2 + 3)");
    }

    // Test unary operators: prefix and postfix increment.
    TEST_F(GetACSLTest, UnaryOp_PreAndPostIncrement) {
        SymbolicExpr::GetACSLConfig config;
        config.noStateLabelFunctionAt = true;

        // Use variable ID 0 and 1 to get actual names from the AST.
        auto var0 = getVarDecl(0);
        ASSERT_NE(var0, nullptr);
        std::string name0 = var0->getNameAsString();
        auto symVal0      = makeSymbolValue(0);
        auto &factory     = symbolic::ExprFactoryScope::current();

        // Prefix increment (e.g., ++x)
        auto preInc = factory.unary(UnaryOp::PreInc,
                                    factory.importExpr(symVal0));
        auto resPre = preInc->getACSL(config);
        ASSERT_TRUE(resPre);
        EXPECT_EQ(resPre.value().first, "++" + name0);

        auto var1 = getVarDecl(1);
        ASSERT_NE(var1, nullptr);
        std::string name1 = var1->getNameAsString();
        auto symVal1      = makeSymbolValue(1);

        // Postfix increment (e.g., x++)
        auto postInc = factory.unary(UnaryOp::PostInc,
                                     factory.importExpr(symVal1));
        auto resPost = postInc->getACSL(config);
        ASSERT_TRUE(resPost);
        EXPECT_EQ(resPost.value().first, name1 + "++");
    }

    // Test SymbolValue (pointer dereference): should print the variable name.
    TEST_F(GetACSLTest, SymbolValue_GetACSL) {
        SymbolicExpr::GetACSLConfig config;
        config.noStateLabelFunctionAt = true;

        auto var0 = getVarDecl(0);
        ASSERT_NE(var0, nullptr);
        std::string name0 = var0->getNameAsString();

        auto symVal = makeSymbolValue(0);
        auto res    = symVal->getACSL(config);
        ASSERT_TRUE(res);
        EXPECT_EQ(res.value().first, name0);
        EXPECT_TRUE(res.value().second.empty());
    }

    // Test SymbolAddress (pointer) ACSL and ACSLOfValue.
    TEST_F(GetACSLTest, SymbolAddress_GetACSL_And_GetACSLOfValue) {
        SymbolicExpr::GetACSLConfig config;
        config.noStateLabelFunctionAt = true;

        auto baseVar = getVarDecl(0);
        ASSERT_NE(baseVar, nullptr);
        std::string baseName = baseVar->getNameAsString();

        // Case 1: offset = 2
        auto addr2 = makeRangeAddr(0, literalHandleForTest(2), std::nullopt);
        // ACSL should be "baseName + 2"
        auto resACSL = addr2->getACSL(config);
        ASSERT_TRUE(resACSL);
        EXPECT_EQ(resACSL.value().first, baseName + " + 2");
        EXPECT_TRUE(resACSL.value().second.empty());
        // ACSLOfValue should be "baseName[2]"
        auto resVal = addr2->getACSLOfValue(config);
        ASSERT_TRUE(resVal);
        EXPECT_EQ(resVal.value().first, baseName + "[2]");
        EXPECT_TRUE(resVal.value().second.empty());

        // Case 2: offset = 0 (no offset effectively)
        auto addr0 = makeRangeAddr(0, literalHandleForTest(0), std::nullopt);
        // ACSL should be just "baseName"
        auto resACSL0 = addr0->getACSL(config);
        ASSERT_TRUE(resACSL0);
        EXPECT_EQ(resACSL0.value().first, baseName);
        EXPECT_TRUE(resACSL0.value().second.empty());
        // ACSLOfValue should be "*baseName"
        auto resVal0 = addr0->getACSLOfValue(config);
        ASSERT_TRUE(resVal0);
        EXPECT_EQ(resVal0.value().first, "*" + baseName);
        EXPECT_TRUE(resVal0.value().second.empty());
    }

    // Test ACSLOfValue with a range length.
    TEST_F(GetACSLTest, SymbolAddress_WithRangeLength) {
        SymbolicExpr::GetACSLConfig config;
        config.noStateLabelFunctionAt = true;

        auto baseVar = getVarDecl(0);
        ASSERT_NE(baseVar, nullptr);
        std::string baseName = baseVar->getNameAsString();

        // offset = 5, length = 3 => [5 .. 7]
        auto addrRange =
            makeRangeAddr(0, literalHandleForTest(5), literalHandleForTest(3));
        auto resRange  = addrRange->getACSLOfValue(config);
        ASSERT_TRUE(resRange);
        EXPECT_EQ(resRange.value().first, baseName + "[5 .. 7]");
        EXPECT_TRUE(resRange.value().second.empty());
    }

    TEST_F(GetACSLTest, SymbolAddressRightBoundUsesFactoryScope) {
        auto addrRange =
            makeRangeAddr(0, literalHandleForTest(5), literalHandleForTest(3));

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        symbolic::SymbolAddressView addrRangeView{addrRange};
        auto rightBound = addrRangeView.rightBound();
        ASSERT_TRUE(rightBound);

        auto expected = factory.binary(factory.importExpr(addrRangeView.offset()),
                                       BinaryOp::Add,
                                       factory.importExpr(addrRangeView.length().value()));
        EXPECT_EQ(rightBound.value(), expected);
        EXPECT_EQ(addrRangeView.rightBound(), rightBound);

        symbolic::BinaryExprView rightBoundView{rightBound.value()};
        EXPECT_EQ(rightBoundView.left(), factory.importExpr(addrRangeView.offset()));
        EXPECT_EQ(rightBoundView.right(),
                  factory.importExpr(addrRangeView.length().value()));
    }

    // Test usage of \\at(...) when predefinedLabels is set.
    TEST_F(GetACSLTest, SymbolAddress_predefinedLabels) {
        SymbolicExpr::GetACSLConfig config;
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
        auto resACSL = addr->getACSL(config);
        ASSERT_TRUE(resACSL);
        EXPECT_EQ(resACSL.value().first, "\\at(" + baseName + ", Old) + 2");
        EXPECT_TRUE(resACSL.value().second.empty());
        auto resVal = addr->getACSLOfValue(config);
        ASSERT_TRUE(resVal);
        EXPECT_EQ(resVal.value().first, "\\at(" + baseName + ", Old)[2]");
    }

    // Test filtering out SourcePoints from ACSL output and usedPoints.
    TEST_F(GetACSLTest, SymbolAddress_SourcePointWhitelistFiltersAt) {
        SymbolicExpr::GetACSLConfig config;

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

        auto resACSL = addr->getACSL(config);
        ASSERT_TRUE(resACSL);
        EXPECT_EQ(resACSL.value().first, baseName + " + 2");
        EXPECT_TRUE(resACSL.value().second.empty());

        auto resVal = addr->getACSLOfValue(config);
        ASSERT_TRUE(resVal);
        EXPECT_EQ(resVal.value().first, baseName + "[2]");
        EXPECT_TRUE(resVal.value().second.empty());
    }

    namespace {
        class CollisionExpr final : public symbolic::SymbolicExpr {
          public:
            explicit CollisionExpr(int id)
                : SymbolicExpr(ExprKind::K_UnknownExpr, Type{ScalarKind::Void, 0}), id_(id) {}

            std::string dump() const override { return "collision:" + std::to_string(id_); }

            bool equal(const symbolic::SymbolicExpr &other) const override {
                auto *rhs = symbolic::dyn_cast<const CollisionExpr>(&other);
                return rhs != nullptr && rhs->id_ == id_;
            }

            std::size_t hash() const override { return 42; }

            bool isLinear() const override { return false; }
            int getMaxDegree() const override { return -1; }

          private:
            ::acslg::utils::expected<std::string, GetACSLError> doGetACSL(
                const GetACSLConfig &,
                std::unordered_set<symbolic::SourcePoint> &,
                std::optional<symbolic::SourcePoint>,
                unsigned,
                bool) const override {
                return dump();
            }

            int id_;
        };

    } // namespace

    TEST(ExprFactoryTest, StripSizeofFactorPreservesHandleIdentity) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto value = factory.rangeIndex("count");
        auto product = factory.binary(factory.literal(std::uint64_t{8}),
                                      symbolic::BinaryOp::Multiply, value);

        auto stripped = symbolic::strip_sizeof_factor(factory, product, 8);
        auto literal = symbolic::strip_sizeof_factor(
            factory, factory.literal(std::uint64_t{8}), 8);
        auto unchanged = symbolic::strip_sizeof_factor(factory, product, 4);

        EXPECT_EQ(stripped.get().get(), value.get().get());
        EXPECT_EQ(literal.get().get(), factory.literal(std::uint64_t{1}).get().get());
        EXPECT_EQ(unchanged.get().get(), product.get().get());
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
        EXPECT_EQ(factory.size(), 2u);
    }

    TEST(ExprFactoryTest, ImportRejectsUnsupportedDynamicExpressionTypes) {
        symbolic::ExprFactory factory;
        CollisionExpr unsupported{1};

        EXPECT_DEATH((void)factory.importExpr(symbolic::ExprHandle{&unsupported}), "");
    }

    TEST(ExprFactoryTest, TypedBuildersReuseEqualLiteralAndOperationNodes) {
        symbolic::ExprFactory factory;

        auto oneA = factory.literal(1);
        auto oneB = factory.literal(1);
        auto two  = factory.literal(2);

        EXPECT_EQ(oneA, oneB);
        EXPECT_NE(oneA, two);
        auto oneView = symbolic::LiteralExprView::tryFrom(oneA);
        ASSERT_TRUE(oneView.has_value());
        EXPECT_EQ(oneView->value(), 1);

        auto sumA =
            factory.binary(oneA, symbolic::BinaryOp::Add, two);
        auto sumB =
            factory.binary(oneB, symbolic::BinaryOp::Add, factory.literal(2));
        auto diff = factory.binary(oneA, symbolic::BinaryOp::Subtract, two);

        EXPECT_EQ(sumA, sumB);
        EXPECT_NE(sumA, diff);
        auto sumView = symbolic::BinaryExprView::tryFrom(sumA);
        ASSERT_TRUE(sumView.has_value());
        EXPECT_EQ(sumView->operation(), symbolic::BinaryOp::Add);
        EXPECT_EQ(sumView->left(), oneA);
        EXPECT_EQ(sumView->right(), two);

        auto negA = factory.unary(symbolic::UnaryOp::Minus, oneA);
        auto negB = factory.unary(symbolic::UnaryOp::Minus, oneB);
        EXPECT_EQ(negA, negB);
        auto negView = symbolic::UnaryExprView::tryFrom(negA);
        ASSERT_TRUE(negView.has_value());
        EXPECT_EQ(negView->operation(), symbolic::UnaryOp::Minus);
        EXPECT_EQ(negView->operand(), oneA);

        EXPECT_FALSE(symbolic::LiteralExprView::tryFrom(sumA).has_value());
        EXPECT_FALSE(symbolic::UnaryExprView::tryFrom(oneA).has_value());
        EXPECT_FALSE(symbolic::BinaryExprView::tryFrom(negA).has_value());
        EXPECT_DEATH((void)symbolic::LiteralExprView{sumA}, "");
        EXPECT_DEATH((void)symbolic::UnaryExprView{oneA}, "");
        EXPECT_DEATH((void)symbolic::BinaryExprView{negA}, "");
    }

    TEST(ExprFactoryTest, WithValTypeDoesNotMutateFactorySharedOperation) {
        symbolic::ExprFactory factory;

        auto one = factory.literal(1);
        auto two = factory.literal(2);
        auto sum = factory.binary(one, symbolic::BinaryOp::Add, two);

        auto targetType = symbolic::SymbolicExpr::Type{
            symbolic::SymbolicExpr::ScalarKind::UInt, 64};
        auto typedSum = factory.withValType(sum, targetType);

        EXPECT_EQ(sum->getValType().kind, symbolic::SymbolicExpr::ScalarKind::Int);
        EXPECT_EQ(sum->getValType().bitWidth, 32);
        EXPECT_EQ(typedSum->getValType().kind, symbolic::SymbolicExpr::ScalarKind::UInt);
        EXPECT_EQ(typedSum->getValType().bitWidth, 64);

        symbolic::BinaryExprView typedSumView{typedSum};
        EXPECT_EQ(typedSumView.left(), one);
        EXPECT_EQ(typedSumView.right(), two);

        EXPECT_NE(typedSum, sum);
        EXPECT_EQ(typedSum, factory.withValType(sum, targetType));
    }

    TEST(ExprFactoryTest, WithValTypeInternsTypedRebuilds) {
        symbolic::ExprFactory factory;

        auto one = factory.literal(1);
        auto targetType = symbolic::SymbolicExpr::Type{
            symbolic::SymbolicExpr::ScalarKind::UInt, 64};

        auto typedOne = factory.withValType(one, targetType);
        EXPECT_NE(typedOne, one);
        EXPECT_EQ(typedOne, factory.withValType(one, targetType));
        EXPECT_EQ(factory.withValType(typedOne, targetType), typedOne);
        EXPECT_EQ(typedOne->getValType(), targetType);

        auto large = factory.literal(std::numeric_limits<std::uint64_t>::max());
        auto typedLarge = factory.withValType(large, targetType);
        EXPECT_TRUE(typedLarge->isLiteralExpr());
        EXPECT_EQ(typedLarge->getValType(), targetType);
        EXPECT_EQ(factory.withValType(typedLarge, large.getValType()), large);

        auto leafTargetType = symbolic::SymbolicExpr::Type{
            symbolic::SymbolicExpr::ScalarKind::Bool, 8};
        for (auto original : {factory.unknown(), factory.rangeIndex("i")}) {
            auto originalType = original->getValType();
            auto typed        = factory.withValType(original, leafTargetType);

            EXPECT_EQ(original->getValType(), originalType);
            EXPECT_EQ(typed->getValType(), leafTargetType);
            EXPECT_NE(typed, original);
            EXPECT_EQ(typed, factory.withValType(original, leafTargetType));
        }

        symbolic::ExprFactoryScope scope(factory);
        symbolic::Expr facade{one};
        auto typedFacade = facade.withType(targetType);
        EXPECT_EQ(typedFacade.handle(), typedOne);
        EXPECT_EQ(typedFacade.getValType(), targetType);
    }

    TEST(ExprFactoryTest, WithValTypeImportsCrossFactoryOperationChildren) {
        symbolic::ExprFactory source;
        auto sourceOne = source.literal(1);
        auto sourceTwo = source.literal(2);
        auto sourceSum = source.binary(
            sourceOne, symbolic::BinaryOp::Add, sourceTwo);

        symbolic::ExprFactory target;
        auto targetType = symbolic::SymbolicExpr::Type{
            symbolic::SymbolicExpr::ScalarKind::UInt, 64};
        auto typedSum = target.withValType(sourceSum, targetType);
        symbolic::BinaryExprView typedView{typedSum};
        auto targetOne = target.literal(1);
        auto targetTwo = target.literal(2);

        EXPECT_EQ(typedSum->getValType(), targetType);
        EXPECT_EQ(typedView.left(), targetOne);
        EXPECT_EQ(typedView.right(), targetTwo);
        EXPECT_NE(typedView.left().get(), sourceOne.get());
        EXPECT_NE(typedView.right().get(), sourceTwo.get());
        EXPECT_EQ(typedSum, target.withValType(sourceSum, targetType));
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
        auto sourceBase = source.variableAddress(var);
        auto sourceOffset = source.binary(
            source.literal(1), symbolic::BinaryOp::Add, source.literal(2));
        auto sourceLength = source.literal(4);
        auto sourceRange = source.symbolAddress(
            var->getType(), sourceBase, point, sourceOffset, sourceLength);
        auto sourcePredicate = source.binary(
            source.rangeIndex("i"), symbolic::BinaryOp::LessThan,
            source.literal(4));
        auto sourceBody = source.literal(7);
        auto sourceSum = symbolic::makeSumOverRangeHandle(
            source, sourceRange, "i", point);
        auto sourceQuantifier = symbolic::makeQuantifierOverRangeHandle(
            source, sourceRange, "i", symbolic::RangeQuantifier::ForAll,
            sourcePredicate);
        auto sourceMax = symbolic::makeMaxMinOverRangeHandle(
            source, sourceRange, "i", symbolic::RangeExtremum::Max,
            sourceBody, point);

        symbolic::ExprFactory target;
        auto boolType = symbolic::SymbolicExpr::Type{
            symbolic::SymbolicExpr::ScalarKind::Bool, 8};
        auto uintType = symbolic::SymbolicExpr::Type{
            symbolic::SymbolicExpr::ScalarKind::UInt, 64};
        auto typedRange = target.withValType(sourceRange.asExpr(), boolType);
        auto typedSum = target.withValType(sourceSum, uintType);
        auto typedQuantifier = target.withValType(sourceQuantifier, uintType);
        auto typedMax = target.withValType(sourceMax, uintType);

        auto targetBase = target.variableAddress(var);
        auto targetOffset = target.binary(
            target.literal(1), symbolic::BinaryOp::Add, target.literal(2));
        auto targetLength = target.literal(4);
        auto typedRangeView = symbolic::SymbolAddressView::tryFrom(typedRange).value();
        ASSERT_TRUE(typedRangeView.from());
        ASSERT_TRUE(typedRangeView.length());
        EXPECT_EQ(*typedRangeView.from(), targetBase);
        EXPECT_EQ(typedRangeView.offset().get(), targetOffset.get().get());
        EXPECT_EQ(typedRangeView.length().value().get(), targetLength.get().get());

        auto targetRange = target.importAddress(sourceRange);
        symbolic::SumOverRangeView typedSumView{typedSum};
        symbolic::QuantifierOverRangeView typedQuantifierView{typedQuantifier};
        symbolic::MaxMinOverRangeView typedMaxView{typedMax};
        EXPECT_EQ(typedSumView.range().handle(), targetRange);
        EXPECT_EQ(typedQuantifierView.range().handle(), targetRange);
        EXPECT_EQ(typedQuantifierView.predicate(), target.importExpr(sourcePredicate));
        EXPECT_EQ(typedMaxView.range().handle(), targetRange);
        EXPECT_EQ(typedMaxView.body(), target.importExpr(sourceBody));
        EXPECT_EQ(typedSumView.indexName(), "i");
        EXPECT_EQ(typedQuantifierView.quantifier(), symbolic::RangeQuantifier::ForAll);
        EXPECT_EQ(typedMaxView.extremum(), symbolic::RangeExtremum::Max);

        EXPECT_FALSE(symbolic::SumOverRangeView::tryFrom(typedQuantifier).has_value());
        EXPECT_FALSE(symbolic::QuantifierOverRangeView::tryFrom(typedMax).has_value());
        EXPECT_FALSE(symbolic::MaxMinOverRangeView::tryFrom(typedSum).has_value());
        EXPECT_DEATH((void)symbolic::SumOverRangeView{typedMax}, "");
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
        auto sourceAddress = source.variableAddress(var);
        auto sourceValue   = source.symbolValue(
            symbolic::deriveType(var->getType()), sourceAddress, point);
        auto sourceStructure = source.structure(record, layout, sourceAddress, point);
        auto valueType       = sourceValue->getValType();
        auto structureType   = sourceStructure->getValType();

        symbolic::ExprFactory target;
        auto targetType = symbolic::SymbolicExpr::Type{
            symbolic::SymbolicExpr::ScalarKind::UInt, 64};
        auto typedValue     = target.withValType(sourceValue, targetType);
        auto typedStructure = target.withValType(sourceStructure, targetType);

        EXPECT_EQ(sourceValue->getValType(), valueType);
        EXPECT_EQ(sourceStructure->getValType(), structureType);
        EXPECT_EQ(typedValue->getValType(), targetType);
        EXPECT_EQ(typedStructure->getValType(), targetType);
        EXPECT_EQ(typedValue, target.withValType(sourceValue, targetType));
        EXPECT_EQ(typedStructure, target.withValType(sourceStructure, targetType));

        symbolic::SymbolValueView typedValueView{typedValue};
        EXPECT_EQ(typedValueView.from(), target.variableAddress(var));

        symbolic::StructureView sourceView{sourceStructure};
        symbolic::StructureView typedView{typedStructure};
        ASSERT_EQ(typedView.size(), sourceView.size());
        for (size_t i = 0; i < typedView.size(); ++i)
            EXPECT_EQ(typedView.field(i), target.importExpr(sourceView.field(i)));
    }

    TEST(ExprFactoryTest, ValueSubstitutionHandleMapImportsReplacement) {
        symbolic::ExprFactory factory;

        auto one = factory.literal(int64_t{1});
        auto two = factory.literal(int64_t{2});
        auto original = factory.binary(one, symbolic::BinaryOp::Add, two);
        auto replacement =
            factory.binary(two, symbolic::BinaryOp::Subtract, one);

        symbolic::HashExprHandleMap substitutions;
        substitutions.emplace(original.hash(), replacement);

        auto substituted =
            symbolic::getSubstitutedValueHandle(factory, original, substitutions);
        EXPECT_EQ(substituted.get().get(), replacement.get().get());
    }

    TEST(ExprFactoryTest, ValueSubstitutionHandleMapReturnsInternedReplacement) {
        symbolic::ExprFactory factory;

        auto one = factory.literal(int64_t{1});
        auto two = factory.literal(int64_t{2});
        auto original = factory.binary(one, symbolic::BinaryOp::Add, two);
        auto replacement =
            factory.binary(two, symbolic::BinaryOp::Subtract, one);

        symbolic::HashExprHandleMap substitutions;
        substitutions.emplace(original.hash(), replacement);

        auto substituted =
            symbolic::getSubstitutedValueHandle(factory, original, substitutions);
        EXPECT_EQ(substituted.get().get(), replacement.get().get());
    }

    TEST(ExprFactoryTest, ValueSubstitutionHandleMapRebuildsBinaryThroughFactory) {
        symbolic::ExprFactory factory;

        auto one = factory.literal(int64_t{1});
        auto two = factory.literal(int64_t{2});
        auto three = factory.literal(int64_t{3});
        auto original = factory.binary(one, symbolic::BinaryOp::Add, two);
        auto expected = factory.binary(three, symbolic::BinaryOp::Add, two);

        symbolic::HashExprHandleMap substitutions;
        substitutions.emplace(one.hash(), three);

        auto substituted =
            symbolic::getSubstitutedValueHandle(factory, original, substitutions);
        EXPECT_EQ(substituted.get().get(), expected.get().get());
    }

    TEST(ExprFactoryTest, ValueSubstitutionHandleMapRebuildsAggregateThroughFactory) {
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

        auto base = factory.variableAddress(var);
        auto range = factory.symbolAddress(var->getType(), base, point,
                                           factory.literal(int64_t{0}),
                                           factory.literal(int64_t{3}));
        auto one = factory.literal(int64_t{1});
        auto two = factory.literal(int64_t{2});
        auto three = factory.literal(int64_t{3});
        auto pred = factory.binary(one, symbolic::BinaryOp::LessThan, two);
        auto expectedPred =
            factory.binary(three, symbolic::BinaryOp::LessThan, two);
        auto original = symbolic::makeQuantifierOverRangeHandle(
            factory, range, "i",
            symbolic::RangeQuantifier::ForAll, pred);
        auto expected = symbolic::makeQuantifierOverRangeHandle(
            factory, range, "i",
            symbolic::RangeQuantifier::ForAll, expectedPred);

        symbolic::HashExprHandleMap substitutions;
        substitutions.emplace(one.hash(), three);

        auto substituted =
            symbolic::getSubstitutedValueHandle(factory, original, substitutions);
        EXPECT_EQ(substituted.get().get(), expected.get().get());
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
        auto varAddr = factory.variableAddress(var);
        auto x       = factory.symbolValue(symbolic::deriveType(var->getType()),
                                           varAddr, point);
        auto two     = factory.literal(2);
        auto sum     = factory.binary(x, symbolic::BinaryOp::Add, two);

        auto simplified = sum->simplifiedExpr();
        symbolic::BinaryExprView rebuilt{simplified};

        EXPECT_EQ(rebuilt.left(), factory.importExpr(rebuilt.left()));
        EXPECT_EQ(rebuilt.right(), factory.importExpr(rebuilt.right()));
        EXPECT_EQ(simplified, factory.importExpr(simplified));
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
        auto xHandle =
            symbolic::Expr::symbolValue(symbolic::deriveType(var->getType()), from, point)
                .handle();
        auto legacyProduct = factory.binary(
            xHandle, symbolic::BinaryOp::Multiply, xHandle);

        auto simplified = legacyProduct->simplifiedExpr();
        symbolic::BinaryExprView product{simplified};

        EXPECT_EQ(product.left(), factory.importExpr(product.left()));
        EXPECT_EQ(product.right(), factory.importExpr(product.right()));
        EXPECT_EQ(simplified, factory.importExpr(legacyProduct));
    }

    TEST(ExprFactoryTest, SimplifiedExprHandleReturnsInternedNode) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto expr = factory.binary(factory.literal(int64_t{1}),
                                   symbolic::BinaryOp::Add,
                                   factory.literal(int64_t{2}));

        auto simplified = symbolic::simplifiedExprHandle(factory, expr);

        EXPECT_EQ(simplified, factory.literal(int64_t{3}));
    }

    TEST(ExprFactoryTest, SimplifiedBinaryFallbackPreservesOperationAndChildHandles) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto i = factory.rangeIndex("i");
        auto j = factory.rangeIndex("j");
        auto product = factory.binary(
            i, symbolic::BinaryOp::Multiply, j);

        auto simplified = factory.simplifiedBinary(
            i, symbolic::BinaryOp::Multiply, j);

        EXPECT_EQ(simplified, product);
        symbolic::BinaryExprView view{simplified};
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
        auto x = symbolic::Expr::symbolValue(
                     symbolic::deriveType(var->getType()), from, point)
                     .handle();
        auto zero = factory.literal(int64_t{0});
        auto predicate =
            factory.binary(x, symbolic::BinaryOp::Equal, zero);
        auto wrapped = factory.binary(predicate, symbolic::BinaryOp::Equal,
                                      factory.literal(int64_t{1}));

        auto simplified = wrapped->simplifiedExpr();
        symbolic::BinaryExprView returnedPredicate{simplified};

        EXPECT_EQ(returnedPredicate.operation(), symbolic::BinaryOp::Equal);
        EXPECT_EQ(returnedPredicate.left(), x);
        EXPECT_EQ(returnedPredicate.right(), zero);
        EXPECT_EQ(factory.importExpr(simplified),
                  factory.binary(x, symbolic::BinaryOp::Equal, zero));
    }

    TEST(ExprFactoryTest, ConstantEvalReturnsInternedLiteral) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto expr = factory.binary(factory.literal(int64_t{1}),
                                   symbolic::BinaryOp::Add,
                                   factory.literal(int64_t{2}));

        auto value = expr->tryEvalToConstant();

        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value.value(), 3);
        EXPECT_FALSE(factory.rangeIndex("i")->tryEvalToConstant().has_value());

        auto simplified = expr->simplifiedExpr();
        EXPECT_EQ(factory.importExpr(simplified), factory.literal(int64_t{3}));
    }

    TEST(ExprFactoryTest, ConstantEvalPreservesOperatorAndShortCircuitSemantics) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto eval = [](symbolic::ExprHandle expr) { return expr->tryEvalToConstant(); };

        EXPECT_EQ(eval(factory.unary(symbolic::UnaryOp::Minus,
                                     factory.literal(int64_t{5}))),
                  -5);
        EXPECT_EQ(eval(factory.binary(factory.literal(int64_t{-1}),
                                      symbolic::BinaryOp::LessThan,
                                      factory.literal(int64_t{1}))),
                  1);
        EXPECT_EQ(eval(factory.binary(
                      factory.literal(std::numeric_limits<uint64_t>::max()),
                      symbolic::BinaryOp::Add,
                      factory.literal(uint64_t{1}))),
                  0);
        EXPECT_FALSE(eval(factory.binary(factory.literal(int64_t{1}),
                                         symbolic::BinaryOp::Divide,
                                         factory.literal(int64_t{0})))
                         .has_value());

        auto unknown = factory.unknown();
        EXPECT_EQ(eval(factory.binary(factory.literal(false),
                                      symbolic::BinaryOp::LogicalAnd,
                                      unknown)),
                  0);
        EXPECT_EQ(eval(factory.binary(factory.literal(true),
                                      symbolic::BinaryOp::LogicalOr,
                                      unknown)),
                  1);
        EXPECT_FALSE(eval(factory.binary(factory.literal(true),
                                         symbolic::BinaryOp::LogicalAnd,
                                         unknown))
                         .has_value());
        EXPECT_FALSE(eval(factory.binary(factory.literal(false),
                                         symbolic::BinaryOp::LogicalOr,
                                         unknown))
                         .has_value());
    }

    TEST(ExprFactoryTest, ImportsOperationTreesAcrossFactoriesIntoInternedDag) {
        symbolic::ExprFactory source;
        auto sourceUnary = source.unary(symbolic::UnaryOp::Minus,
                                        source.literal(int64_t{1}));
        auto sourceTree = source.binary(sourceUnary, symbolic::BinaryOp::Add,
                                        source.literal(int64_t{2}));

        symbolic::ExprFactory factory;
        auto imported = factory.importExpr(sourceTree);
        auto repeated = factory.importExpr(sourceTree);

        EXPECT_EQ(imported, repeated);
        auto one = factory.literal(int64_t{1});
        auto two = factory.literal(int64_t{2});

        symbolic::BinaryExprView bin{imported};
        EXPECT_EQ(bin.right(), two);
        symbolic::UnaryExprView unary{bin.left()};
        EXPECT_EQ(unary.operand(), one);

        symbolic::ExprFactoryScope scope(factory);
        symbolic::Expr facade{imported};
        EXPECT_EQ(facade.handle(), imported);
    }

    TEST(ExprFactoryTest, ImportsCrossFactoryUInt64LiteralWithoutValueNarrowing) {
        const auto large = std::numeric_limits<std::uint64_t>::max();
        symbolic::ExprFactory sourceFactory;
        auto source = sourceFactory.literal(large);

        symbolic::ExprFactory factory;
        auto imported = factory.importExpr(source);
        auto expected = factory.literal(large);

        EXPECT_EQ(imported, expected);
        EXPECT_EQ(imported.get().get(), expected.get().get());
        EXPECT_TRUE(imported->isLiteralExpr());
        EXPECT_EQ(imported->getValType().kind, symbolic::SymbolicExpr::ScalarKind::UInt);
        EXPECT_EQ(imported->getValType().bitWidth, 64);
    }

    TEST(ExprFactoryTest, UnknownBuilderReusesUnknownNode) {
        symbolic::ExprFactory factory;

        auto a = factory.unknown();
        auto b = factory.unknown();

        EXPECT_EQ(a, b);
        EXPECT_TRUE(a->isUnknown());
    }

    TEST(ExprFactoryTest, RangeIndexBuilderAndImportReuseNode) {
        symbolic::ExprFactory factory;

        auto k = factory.rangeIndex("k");
        auto i = factory.rangeIndex("i");

        EXPECT_EQ(k, i);
        EXPECT_TRUE(k->isRangeIndex());
        EXPECT_FALSE(factory.literal(0)->isRangeIndex());
        EXPECT_FALSE(k->isStructure());
        EXPECT_FALSE(k->isSymbolValue());
        EXPECT_FALSE(k->isSymbolAddress());
        EXPECT_FALSE(k->isVariableAddress());
        EXPECT_FALSE(k->isFieldAddress());

        symbolic::ExprFactory sourceFactory;
        auto source = sourceFactory.rangeIndex("j");
        auto imported = factory.importExpr(source);

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
        symbolic::SymbolAddrBaseInfo rangeBase{std::nullopt, point, var->getType()};

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto one = factory.literal(int64_t{1});
        auto two = factory.literal(int64_t{2});
        auto replacement =
            factory.binary(one, symbolic::BinaryOp::Add, two);

        auto rangeIndexHandle = factory.rangeIndex("i");
        auto substituted =
            symbolic::getRangeIndexSubstitutedHandle(factory, rangeIndexHandle, rangeBase,
                                                      replacement);
        symbolic::BinaryExprView node{substituted};

        EXPECT_EQ(node.left(), one);
        EXPECT_EQ(node.right(), two);

        auto varAddr = factory.variableAddress(var);
        auto rangeIndex = factory.rangeIndex("i");
        auto indexedFrom =
            factory.symbolAddress(var->getType(), varAddr, point, rangeIndex, rangeIndex);
        auto indexedValue = factory.symbolValue(
            symbolic::deriveType(var->getType()), indexedFrom, point);
        auto indexedRangeBase = symbolic::SymbolAddressView{indexedFrom}.baseInfo();
        auto substitutedValue = symbolic::getRangeIndexSubstitutedHandle(
            factory, indexedValue, indexedRangeBase, replacement);
        auto expectedFrom =
            factory.symbolAddress(var->getType(), varAddr, point, replacement, replacement);
        EXPECT_EQ(substitutedValue.get().get(),
                  factory
                      .symbolValue(symbolic::deriveType(var->getType()), expectedFrom, point)
                      .get()
                      .get());
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
        symbolic::SymbolAddrBaseInfo rangeBase{std::nullopt, point, var->getType()};

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto literal = factory.literal(int64_t{7});
        auto index = factory.literal(int64_t{0});
        auto substitutedLiteral =
            symbolic::getRangeIndexSubstitutedHandle(factory, literal, rangeBase, index);
        EXPECT_EQ(substitutedLiteral.get().get(), literal.get().get());

        symbolic::HashExprHandleMap emptySubstitutions;
        auto valueSubstitutedLiteral =
            symbolic::getSubstitutedValueHandle(factory, literal, emptySubstitutions);
        EXPECT_EQ(valueSubstitutedLiteral.get().get(), literal.get().get());

        auto varAddr = factory.variableAddress(var);
        auto substitutedAddr =
            symbolic::getRangeIndexSubstitutedHandle(factory, varAddr.asExpr(), rangeBase, index);
        EXPECT_EQ(substitutedAddr.get().get(), varAddr.asExpr().get().get());
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
            return range.handle();
        };

        auto makePred = [&]() {
            return factory.binary(factory.rangeIndex("i"),
                                  symbolic::BinaryOp::LessThan,
                                  factory.literal(3));
        };

        auto sumRangeHandle = makeRange();
        auto sumHandle = symbolic::makeSumOverRangeHandle(
            factory, sumRangeHandle, "i", point);
        symbolic::SumOverRangeView sum{sumHandle};
        auto quantifierRangeHandle = makeRange();
        auto predicate = makePred();
        auto quantifierHandle = symbolic::makeQuantifierOverRangeHandle(
            factory, quantifierRangeHandle, "i",
            symbolic::RangeQuantifier::ForAll, predicate);
        symbolic::QuantifierOverRangeView quantifier{quantifierHandle};
        auto maxRangeHandle = makeRange();
        auto maxHandle = symbolic::makeMaxMinOverRangeHandle(
            factory, maxRangeHandle, "i",
            symbolic::RangeExtremum::Max, point);
        symbolic::MaxMinOverRangeView max{maxHandle};

        auto importedSum = factory.importExpr(sum.handle());
        auto sumRange    = factory.importExpr(sum.range().handle().asExpr());
        symbolic::SumOverRangeView sumNode{importedSum};
        EXPECT_EQ(sumNode.range().handle().asExpr(), sumRange);
        EXPECT_EQ(importedSum, factory.importExpr(sum.handle()));

        auto importedQuantifier = factory.importExpr(quantifier.handle());
        auto quantifierRange = factory.importExpr(quantifier.range().handle().asExpr());
        auto quantifierPred     = factory.importExpr(quantifier.predicate());
        symbolic::QuantifierOverRangeView quantifierNode{importedQuantifier};
        EXPECT_EQ(quantifierNode.range().handle().asExpr(), quantifierRange);
        EXPECT_EQ(quantifierNode.predicate(), quantifierPred);
        EXPECT_EQ(importedQuantifier, factory.importExpr(quantifier.handle()));

        auto importedMax = factory.importExpr(max.handle());
        auto maxRange    = factory.importExpr(max.range().handle().asExpr());
        auto maxBody     = factory.importExpr(max.body());
        symbolic::MaxMinOverRangeView maxNode{importedMax};
        EXPECT_EQ(maxNode.range().handle().asExpr(), maxRange);
        EXPECT_EQ(maxNode.body(), maxBody);
        EXPECT_EQ(importedMax, factory.importExpr(max.handle()));
    }

    TEST(SumOverRangeRebuildTest, RangeUsesExprChildAcrossImportAndSubstitution) {
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
        auto rangeHandle = factory.symbolAddress(
            var->getType(), factory.variableAddress(var), point);
        rangeHandle = factory.withOffset(rangeHandle, factory.rangeIndex("i"));
        rangeHandle = factory.withLength(rangeHandle, factory.literal(int64_t{3}));
        auto rangeBase = symbolic::SymbolAddressView{rangeHandle}.baseInfo();
        auto sumHandle = symbolic::makeSumOverRangeHandle(
            factory, rangeHandle, "i", point);
        symbolic::SumOverRangeView sum{sumHandle};

        auto imported = factory.importExpr(sum.handle());
        EXPECT_EQ(imported, sum.handle());
        EXPECT_EQ(symbolic::SumOverRangeView{imported}.range().handle(), rangeHandle);

        auto index = factory.literal(int64_t{1});
        auto substituted =
            symbolic::getRangeIndexSubstitutedHandle(factory, sum.handle(), rangeBase, index);
        EXPECT_NE(substituted, sum.handle());
    }

    TEST(QuantifierOverRangeRebuildTest, PredicateUsesExprChildAcrossImportAndSubstitution) {
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
        auto rangeHandle = factory.symbolAddress(
            var->getType(), factory.variableAddress(var), point);
        rangeHandle = factory.withLength(rangeHandle, factory.literal(int64_t{3}));
        auto rangeBase = symbolic::SymbolAddressView{rangeHandle}.baseInfo();
        auto pred = factory.binary(factory.rangeIndex("i"),
                                   symbolic::BinaryOp::LessThan,
                                   factory.literal(int64_t{3}));
        auto quantifierHandle = symbolic::makeQuantifierOverRangeHandle(
            factory, rangeHandle, "i",
            symbolic::RangeQuantifier::ForAll, pred);
        symbolic::QuantifierOverRangeView quantifier{quantifierHandle};

        auto imported = factory.importExpr(quantifier.handle());
        EXPECT_EQ(imported, quantifier.handle());
        symbolic::QuantifierOverRangeView importedQuantifier{imported};
        EXPECT_EQ(importedQuantifier.range().handle(), rangeHandle);
        EXPECT_EQ(importedQuantifier.predicate(), factory.importExpr(quantifier.predicate()));

        auto index = factory.literal(int64_t{1});
        auto substituted =
            symbolic::getRangeIndexSubstitutedHandle(factory, quantifier.handle(), rangeBase,
                                                      index);
        EXPECT_NE(substituted, quantifier.handle());
    }

    TEST(MaxMinOverRangeRebuildTest, BodyUsesExprChildAcrossImportAndSubstitution) {
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
        auto rangeHandle = factory.symbolAddress(
            var->getType(), factory.variableAddress(var), point);
        rangeHandle = factory.withLength(rangeHandle, factory.literal(int64_t{3}));
        auto rangeBase = symbolic::SymbolAddressView{rangeHandle}.baseInfo();
        auto maxHandle = symbolic::makeMaxMinOverRangeHandle(
            factory, rangeHandle, "i", symbolic::RangeExtremum::Max, point);
        symbolic::MaxMinOverRangeView max{maxHandle};

        auto indexedRange = factory.withOffset(rangeHandle, factory.rangeIndex("i"));
        indexedRange      = factory.withoutLength(indexedRange);
        auto expectedBody =
            symbolic::getSymbol(rangeHandle->getPointeeType(), indexedRange, point);
        EXPECT_EQ(max.body(), expectedBody);

        auto imported = factory.importExpr(max.handle());
        EXPECT_EQ(imported, max.handle());
        symbolic::MaxMinOverRangeView importedMax{imported};
        EXPECT_EQ(importedMax.range().handle(), rangeHandle);
        EXPECT_EQ(importedMax.body(), factory.importExpr(max.body()));

        auto index = factory.literal(int64_t{1});
        auto substituted =
            symbolic::getRangeIndexSubstitutedHandle(factory, max.handle(), rangeBase, index);
        EXPECT_NE(substituted, max.handle());
    }

    TEST(AggregateRebuildTest, HelperRejectsSymbolAddressWithoutLength) {
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
        auto address = factory.symbolAddress(var->getType(), factory.variableAddress(var), point);

        ASSERT_DEATH(
            (void)symbolic::makeSumOverRangeHandle(factory, address, "i", point), "");
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
            auto rangeHandle = factory.symbolAddress(
                var->getType(), factory.variableAddress(var), point);
            rangeHandle = factory.withOffset(rangeHandle, factory.rangeIndex("i"));
            rangeHandle = factory.withLength(rangeHandle, factory.literal(int64_t{3}));
            return rangeHandle;
        };

        auto one   = factory.literal(int64_t{1});
        auto three = factory.literal(int64_t{3});

        auto sumRange = makeRange();
        auto rangeBase = symbolic::SymbolAddressView{sumRange}.baseInfo();
        auto sumHandle = symbolic::makeSumOverRangeHandle(
            factory, sumRange, "i", point);
        auto substitutedSum =
            symbolic::getRangeIndexSubstitutedHandle(factory, sumHandle, rangeBase, one);
        auto sumRangeView = symbolic::SumOverRangeView{substitutedSum}.range();
        EXPECT_EQ(sumRangeView.offset(), one);
        ASSERT_TRUE(sumRangeView.length());
        EXPECT_EQ(sumRangeView.length().value(), three);

        auto quantRange = makeRange();
        auto quantifierHandle = symbolic::makeQuantifierOverRangeHandle(
            factory, quantRange, "i",
            symbolic::RangeQuantifier::ForAll,
            factory.rangeIndex("i"));
        auto substitutedQuantifier =
            symbolic::getRangeIndexSubstitutedHandle(factory, quantifierHandle, rangeBase, one);
        EXPECT_EQ(symbolic::QuantifierOverRangeView{substitutedQuantifier}.predicate(), one);

        auto maxRange = makeRange();
        auto maxHandle = symbolic::makeMaxMinOverRangeHandle(
            factory, maxRange, "i", symbolic::RangeExtremum::Max,
            factory.rangeIndex("i"), point);
        auto substitutedMax =
            symbolic::getRangeIndexSubstitutedHandle(factory, maxHandle, rangeBase, one);
        EXPECT_EQ(symbolic::MaxMinOverRangeView{substitutedMax}.body(), one);
    }

    TEST(AggregateRebuildTest, ScopedFactoryHelpersReturnHandleBackedChildren) {
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
            return factory.symbolAddress(var->getType(), factory.variableAddress(var), point,
                                         factory.rangeIndex("i"),
                                         factory.literal(int64_t{3}));
        };

        auto sumRange = makeRange();
        auto sum = symbolic::makeSumOverRangeHandle(
            factory, sumRange, "i", point);
        symbolic::SumOverRangeView sumView{sum};
        EXPECT_TRUE(sum->isOverRange());
        EXPECT_EQ(factory.importAddress(sumView.range().handle()), sumView.range().handle());

        auto quantifier = symbolic::makeQuantifierOverRangeHandle(
            factory, makeRange(), "i",
            symbolic::RangeQuantifier::ForAll,
            factory.rangeIndex("i"));
        symbolic::QuantifierOverRangeView quantifierView{quantifier};
        EXPECT_TRUE(quantifier->isOverRange());
        EXPECT_EQ(factory.importAddress(quantifierView.range().handle()),
                  quantifierView.range().handle());
        EXPECT_EQ(factory.importExpr(quantifierView.predicate()), quantifierView.predicate());

        auto max = symbolic::makeMaxMinOverRangeHandle(
            factory, makeRange(), "i",
            symbolic::RangeExtremum::Max, point);
        symbolic::MaxMinOverRangeView maxView{max};
        EXPECT_TRUE(max->isOverRange());
        EXPECT_EQ(factory.importAddress(maxView.range().handle()), maxView.range().handle());
        EXPECT_EQ(factory.importExpr(maxView.body()), maxView.body());
        EXPECT_FALSE(factory.literal(0)->isOverRange());
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
        EXPECT_EQ(sum.handle().get().get(), sameSum.handle().get().get());
        EXPECT_NE(sum, product);
        EXPECT_EQ(symbolic::BinaryExprView{sum.handle()}.operation(), symbolic::BinaryOp::Add);
        EXPECT_EQ(symbolic::BinaryExprView{product.handle()}.operation(),
                  symbolic::BinaryOp::Multiply);
        EXPECT_EQ(symbolic::LiteralExprView{x.handle()}.value(), 10);
    }

    TEST(ExprFacadeTest, OperatorsRejectDifferentFactories) {
        symbolic::ExprFactory leftFactory;
        symbolic::ExprFactory rightFactory;

        symbolic::Expr left = [&] {
            symbolic::ExprFactoryScope scope(leftFactory);
            return symbolic::Expr{symbolic::LiteralExpr{1}};
        }();
        symbolic::Expr right = [&] {
            symbolic::ExprFactoryScope scope(rightFactory);
            return symbolic::Expr{symbolic::LiteralExpr{2}};
        }();

        ASSERT_DEATH({ (void)(left + right); }, "");
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

        EXPECT_EQ(symbolic::BinaryExprView{equalA.handle()}.operation(),
                  symbolic::BinaryOp::Equal);
        EXPECT_EQ(symbolic::BinaryExprView{less.handle()}.operation(),
                  symbolic::BinaryOp::LessThan);
        EXPECT_EQ(symbolic::BinaryExprView{greaterEqual.handle()}.operation(),
                  symbolic::BinaryOp::GreaterEqual);
        EXPECT_EQ(symbolic::BinaryExprView{conjunction.handle()}.operation(),
                  symbolic::BinaryOp::LogicalAnd);
        EXPECT_EQ(symbolic::BinaryExprView{disjunction.handle()}.operation(),
                  symbolic::BinaryOp::LogicalOr);
        EXPECT_EQ(symbolic::UnaryExprView{negated.handle()}.operation(),
                  symbolic::UnaryOp::LogicalNot);
    }

    TEST(ExprFacadeTest, SimplifiedReturnsFactoryBackedFacade) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        symbolic::Expr sum = symbolic::LiteralExpr{1} + symbolic::LiteralExpr{2};
        symbolic::Expr simplified = sum.simplified();

        EXPECT_EQ(&simplified.factory(), &factory);
        EXPECT_EQ(simplified.handle(), factory.literal(int64_t{3}));
    }

    TEST(ExprFacadeTest, UnaryOperatorsUseCurrentFactory) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        symbolic::LiteralExpr one{1};
        auto negated = -one;
        auto notOne = !one;

        EXPECT_EQ(negated, -symbolic::LiteralExpr{1});
        EXPECT_EQ(notOne, !symbolic::LiteralExpr{1});
        EXPECT_EQ(symbolic::UnaryExprView{negated.handle()}.operation(), symbolic::UnaryOp::Minus);
        EXPECT_EQ(symbolic::UnaryExprView{notOne.handle()}.operation(),
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
            symbolic::SymbolicExpr::Type{symbolic::SymbolicExpr::ScalarKind::Int, 32},
            varAddr, point);

        EXPECT_EQ(unknown.handle(), factory.unknown());
        EXPECT_TRUE(unknown->isUnknown());
        EXPECT_EQ(index.handle(), factory.rangeIndex("i"));
        EXPECT_TRUE(index->isRangeIndex());
        EXPECT_EQ(varAddr.handle(), factory.variableAddress(var));
        EXPECT_TRUE(varAddr->isVariableAddress());
        EXPECT_EQ(symbolAddr.handle(), factory.symbolAddress(var->getType(), varAddr.handle(), point));
        EXPECT_TRUE(symbolAddr->isSymbolAddress());
        EXPECT_EQ(indexedSymbolAddr.handle(),
                  factory.symbolAddress(var->getType(), varAddr.handle(), point,
                                        index.handle(), length.handle()));
        EXPECT_EQ(unbasedRangeAddr.handle(),
                  factory.symbolAddress(var->getType(), std::nullopt, point,
                                        index.handle(), length.handle()));
        EXPECT_EQ(symbolValue.handle(),
                  factory.symbolValue(symbolic::SymbolicExpr::Type{
                                          symbolic::SymbolicExpr::ScalarKind::Int, 32},
                                      varAddr.handle(), point));
        EXPECT_TRUE(symbolValue->isSymbolValue());
        symbolic::SymbolValueView symbolValueView{symbolValue.handle()};
        EXPECT_EQ(symbolValueView.from(), varAddr.handle());
        EXPECT_EQ(symbolValueView.fromPoint(), point);
        EXPECT_EQ(symbolValueView.fromRoot().value().get(), var);
        EXPECT_FALSE(symbolic::SymbolValueView::tryFrom(unknown.handle()).has_value());
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

        auto handle = factory.variableAddress(var);
        symbolic::Addr addr{handle};
        symbolic::ExprFactory sourceFactory;
        auto source = sourceFactory.variableAddress(var);
        symbolic::Addr imported{factory.importAddress(source)};

        EXPECT_EQ(&addr.factory(), &factory);
        EXPECT_EQ(addr.handle(), handle);
        EXPECT_EQ(imported.handle(), handle);
        EXPECT_EQ(addr, imported);
        EXPECT_TRUE(addr->isVariableAddress());
        EXPECT_EQ(addr.asExpr().handle(), handle.asExpr());
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
            return symbolic::Addr{leftFactory.variableAddress(var)};
        }();
        symbolic::Addr right = [&] {
            symbolic::ExprFactoryScope scope(rightFactory);
            return symbolic::Addr{rightFactory.variableAddress(var)};
        }();

        EXPECT_FALSE(left == right);
        EXPECT_TRUE(left.handle().get()->equal(*right.handle().get()));
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

        symbolic::Addr varAddr{factory.variableAddress(var)};
        symbolic::Addr base{factory.symbolAddress(
            var->getType(), std::optional<symbolic::AddrHandle>{varAddr.handle()}, point)};
        symbolic::LiteralExpr offset{4};
        symbolic::LiteralExpr length{3};
        symbolic::Expr extra{factory.unknown()};

        auto shifted = base.withOffset(offset);
        auto ranged = shifted.withLength(length);
        auto addedOffset = base.withAddedOffset(extra);
        auto subtractedOffset = base.withSubtractedOffset(extra);
        auto addedLength = base.withAddedLength(extra);
        auto scalar = ranged.withoutLength();
        auto field = varAddr.field(firstField->getType(), record, 0);

        EXPECT_EQ(shifted.handle(), factory.withOffset(base.handle(), offset.handle()));
        EXPECT_EQ(ranged.handle(), factory.withLength(shifted.handle(), length.handle()));
        EXPECT_EQ(addedOffset.handle(), factory.withAddedOffset(base.handle(), extra.handle()));
        EXPECT_EQ(subtractedOffset.handle(),
                  factory.withSubtractedOffset(base.handle(), extra.handle()));
        EXPECT_EQ(addedLength.handle(), factory.withAddedLength(base.handle(), extra.handle()));
        EXPECT_EQ(scalar.handle(), factory.withoutLength(ranged.handle()));
        EXPECT_EQ(field.handle(),
                  factory.fieldAddress(firstField->getType(), record, varAddr.handle(), 0));
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
            return symbolic::Addr{leftFactory.symbolAddress(
                var->getType(),
                std::optional<symbolic::AddrHandle>{leftFactory.variableAddress(var)}, point)};
        }();
        symbolic::Expr offset = [&] {
            symbolic::ExprFactoryScope scope(rightFactory);
            return symbolic::Expr{symbolic::LiteralExpr{4}};
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

        auto varAddrA = factory.variableAddress(var);
        auto varAddrB = factory.variableAddress(var);
        EXPECT_EQ(varAddrA, varAddrB);
        EXPECT_TRUE(varAddrA->isVariableAddress());
        symbolic::VariableAddressView variableView{varAddrA};
        EXPECT_EQ(variableView.declaration().get(), var);
        EXPECT_EQ(variableView.handle(), varAddrA);

        symbolic::AddressBox handleBox{varAddrA};
        symbolic::AddressBox copiedHandleBox{handleBox};
        EXPECT_EQ(handleBox.handle(), varAddrA);
        EXPECT_EQ(copiedHandleBox.handle(), varAddrA);

        auto symbolValue = factory.symbolValue(
            symbolic::SymbolicExpr::Type{symbolic::SymbolicExpr::ScalarKind::Int, 32},
            varAddrA, point);
        EXPECT_EQ(symbolic::SymbolValueView{symbolValue}.from(), varAddrA);

        auto defaultSymAddr = factory.symbolAddress(
            firstField->getType(), std::optional<symbolic::AddrHandle>{varAddrA}, point);
        symbolic::SymbolAddressView defaultSymAddrView{defaultSymAddr};
        ASSERT_TRUE(defaultSymAddrView.from());
        EXPECT_EQ(*defaultSymAddrView.from(), varAddrA);
        auto defaultBase = defaultSymAddrView.baseInfo();
        ASSERT_TRUE(defaultBase.fromAddr_);
        EXPECT_EQ(defaultBase.fromAddr_->handle(), varAddrA);
        auto copiedBase = defaultBase;
        EXPECT_EQ(copiedBase.fromAddr_->handle(), varAddrA);
        EXPECT_EQ(defaultSymAddrView.offset().get(),
                  factory.literal(static_cast<int64_t>(symbolic::SymbolAddressView::ZERO_OFFSET))
                      .get()
                      .get());

        auto offset = factory.literal(4);
        auto length = factory.literal(2);
        auto symAddrA = factory.symbolAddress(
            firstField->getType(), std::optional<symbolic::AddrHandle>{varAddrA}, point,
            std::optional<symbolic::ExprHandle>{offset},
            std::optional<symbolic::ExprHandle>{length});
        auto symAddrB = factory.symbolAddress(
            firstField->getType(), std::optional<symbolic::AddrHandle>{varAddrB}, point,
            std::optional<symbolic::ExprHandle>{factory.literal(4)},
            std::optional<symbolic::ExprHandle>{factory.literal(2)});
        EXPECT_EQ(symAddrA, symAddrB);
        EXPECT_TRUE(symAddrA->isSymbolAddress());
        symbolic::SymbolAddressView symbolView{symAddrA};
        EXPECT_EQ(symbolView.from(), varAddrA);
        EXPECT_EQ(symbolView.fromPoint(), point);
        EXPECT_EQ(symbolView.offset(), offset);
        EXPECT_EQ(symbolView.length(), length);
        EXPECT_EQ(symbolView.pointeeType(), firstField->getType());
        EXPECT_EQ(symbolView.fromRoot().value().get(), var);
        EXPECT_EQ(symbolView.dimension(), 1);

        auto fieldAddrA = factory.fieldAddress(firstField->getType(), record, varAddrA, 0);
        auto fieldAddrB = factory.fieldAddress(firstField->getType(), record, varAddrB, 0);
        EXPECT_EQ(fieldAddrA, fieldAddrB);
        EXPECT_TRUE(fieldAddrA->isFieldAddress());
        symbolic::FieldAddressView fieldView{fieldAddrA};
        EXPECT_EQ(fieldView.definition().get(), record);
        EXPECT_EQ(fieldView.base(), varAddrA);
        EXPECT_EQ(fieldView.fieldIndex(), 0u);

        EXPECT_FALSE(symbolic::VariableAddressView::tryFrom(fieldAddrA).has_value());
        EXPECT_FALSE(symbolic::FieldAddressView::tryFrom(symAddrA).has_value());
        EXPECT_FALSE(symbolic::SymbolAddressView::tryFrom(varAddrA).has_value());
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
        auto sourceVariable = source.variableAddress(var);
        auto sourceField =
            source.fieldAddress(field->getType(), record, sourceVariable, field->getFieldIndex());
        auto sourceValue = source.symbolValue(symbolic::deriveType(field->getType()),
                                              sourceField, point);

        symbolic::ExprFactory target;
        auto importedVariable = target.importAddress(sourceVariable);
        auto importedField    = target.importAddress(sourceField);
        auto importedValue    = target.importExpr(sourceValue);

        auto expectedVariable = target.variableAddress(var);
        auto expectedField =
            target.fieldAddress(field->getType(), record, expectedVariable, field->getFieldIndex());
        auto expectedValue = target.symbolValue(symbolic::deriveType(field->getType()),
                                                expectedField, point);

        EXPECT_EQ(importedVariable, expectedVariable);
        EXPECT_EQ(importedField, expectedField);
        EXPECT_EQ(importedValue, expectedValue);
        EXPECT_EQ(symbolic::FieldAddressView{importedField}.base(), expectedVariable);
        EXPECT_EQ(symbolic::SymbolValueView{importedValue}.from(), expectedField);
        EXPECT_NE(importedVariable.get().get(), sourceVariable.get().get());
        EXPECT_NE(importedField.get().get(), sourceField.get().get());
        EXPECT_NE(importedValue.get().get(), sourceValue.get().get());
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
        auto varAddr = factory.variableAddress(var);
        auto structure = factory.structure(record, layout, varAddr, point);
        symbolic::StructureView structureView{structure};
        EXPECT_TRUE(structure->isStructure());
        EXPECT_TRUE(symbolic::StructureView::tryFrom(structure).has_value());
        EXPECT_FALSE(symbolic::StructureView::tryFrom(factory.literal(0)).has_value());
        EXPECT_EQ(structureView.info().definition_.get(), record);
        EXPECT_EQ(structureView.fromPoint(), point);

        auto structureFrom = symbolic::getFromAddrHandle(factory, structureView.handle());
        ASSERT_TRUE(structureFrom);
        EXPECT_EQ(*structureFrom, varAddr);

        ASSERT_EQ(structureView.size(), 3u);
        auto field0 = structureView.field(0);
        auto field1 = structureView.field(1);
        auto field2 = structureView.field(2);

        EXPECT_TRUE(field0->isSymbolValue());
        EXPECT_TRUE(field1->isSymbolAddress());
        auto arrayAddr = symbolic::SymbolAddressView::tryFrom(field2);
        ASSERT_TRUE(arrayAddr);
        ASSERT_TRUE(arrayAddr->length());

        EXPECT_EQ(field0, factory.importExpr(field0));
        EXPECT_EQ(field1, factory.importExpr(field1));
        EXPECT_EQ(field2, factory.importExpr(field2));
        EXPECT_EQ(arrayAddr->length().value().get().get(),
                  factory.literal(uint64_t{2}).get().get());

    }

    TEST(ExprFactoryTest, ScopedGetSymbolBuildsThroughFactory) {
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
        auto scalarSym =
            symbolic::getSymbol(scalar->getType(), factory.variableAddress(scalar), point);
        auto ptrSym = symbolic::getSymbol(ptr->getType(), factory.variableAddress(ptr), point);
        auto unbasedPtrSym = symbolic::getSymbol(ptr->getType(), std::nullopt, point);
        auto arrSym = symbolic::getSymbol(arr->getType(), factory.variableAddress(arr), point);
        auto stSym = symbolic::getSymbol(st->getType(), factory.variableAddress(st), point);

        EXPECT_EQ(scalarSym,
                  factory.symbolValue(symbolic::deriveType(scalar->getType()),
                                      factory.variableAddress(scalar), point));
        auto pointerType = llvm::cast<PointerType>(ptr->getType());
        EXPECT_EQ(ptrSym,
                  factory.symbolAddress(pointerType->getPointeeType(),
                                        factory.variableAddress(ptr), point)
                      .asExpr());
        EXPECT_EQ(unbasedPtrSym,
                  symbolic::Addr::symbol(factory, pointerType->getPointeeType(), point)
                      .asExpr()
                      .handle());
        auto arrayType = llvm::cast<ArrayType>(arr->getType());
        EXPECT_EQ(arrSym,
                  factory.symbolAddress(arrayType->getElementType(),
                                        factory.variableAddress(arr), point)
                      .asExpr());

        auto *record = st->getType()->getAsRecordDecl();
        ASSERT_NE(record, nullptr);
        ASSERT_TRUE(record->isCompleteDefinition());
        record = record->getDefinition();
        auto &layout = record->getASTContext().getASTRecordLayout(record);
        EXPECT_EQ(stSym,
                  factory.structure(record, layout, factory.variableAddress(st), point));
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
        auto structureHandle =
            factory.structure(record, layout, factory.variableAddress(st), point);
        symbolic::StructureView structure{structureHandle};

        std::vector<const FieldDecl *> fields;
        for (const auto *field : record->fields())
            fields.push_back(field);
        ASSERT_EQ(fields.size(), 3u);

        auto fromHandle = factory.variableAddress(st);
        auto field0Addr = factory.fieldAddress(fields[0]->getType(), record, fromHandle, 0);
        auto expectedField0 =
            factory.symbolValue(symbolic::deriveType(fields[0]->getType()), field0Addr, point);
        EXPECT_EQ(structure.field(0), expectedField0);

        auto field1Addr = factory.fieldAddress(fields[1]->getType(), record, fromHandle, 1);
        auto arrayType = llvm::cast<ArrayType>(fields[1]->getType());
        auto expectedField1 = factory.symbolAddress(
            arrayType->getElementType(), field1Addr, point, std::nullopt,
            factory.literal(uint64_t{3}));
        EXPECT_EQ(structure.field(1), expectedField1.asExpr());

        auto field2Addr = factory.fieldAddress(fields[2]->getType(), record, fromHandle, 2);
        auto *nestedRecord = fields[2]->getType()->getAsRecordDecl();
        ASSERT_NE(nestedRecord, nullptr);
        ASSERT_TRUE(nestedRecord->isCompleteDefinition());
        nestedRecord = nestedRecord->getDefinition();
        auto &nestedLayout =
            nestedRecord->getASTContext().getASTRecordLayout(nestedRecord);
        auto expectedField2 =
            factory.structure(nestedRecord, nestedLayout, field2Addr, point);
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
        auto sizeBefore = factory.size();
        auto structureExpr = factory.structure(record, layout, factory.variableAddress(var), point);
        EXPECT_GT(factory.size(), sizeBefore);

        auto expected = factory.structure(record, layout, factory.variableAddress(var), point);
        EXPECT_EQ(structureExpr, expected);
        symbolic::StructureView structure{structureExpr};
        symbolic::StructureView expectedStructure{expected};
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
        auto varAddr = factory.variableAddress(x);
        auto rangeIndex = factory.rangeIndex("i");
        auto indexedAddr = factory.symbolAddress(
            x->getType(), varAddr, point, rangeIndex, rangeIndex);
        auto index = factory.literal(int64_t{4});
        auto rangeBase = symbolic::SymbolAddressView{indexedAddr}.baseInfo();

        auto substitutedAddr =
            symbolic::getRangeIndexSubstitutedHandle(factory, indexedAddr.asExpr(), rangeBase,
                                                      index);
        EXPECT_EQ(substitutedAddr.get().get(),
                  factory.symbolAddress(x->getType(), varAddr, point, index, index)
                      .asExpr()
                      .get()
                      .get());

        auto fieldAddr = factory.fieldAddress(
            firstField->getType(), record, factory.variableAddress(s), 0);
        auto substitutedField =
            symbolic::getRangeIndexSubstitutedHandle(factory, fieldAddr.asExpr(), rangeBase,
                                                      index);
        EXPECT_EQ(substitutedField.get().get(), fieldAddr.asExpr().get().get());

        auto indexedStructAddr = factory.symbolAddress(
            s->getType(), factory.variableAddress(s), point, rangeIndex, rangeIndex);
        auto indexedFieldAddr =
            factory.fieldAddress(firstField->getType(), record, indexedStructAddr, 0);
        auto indexedRangeBase = symbolic::SymbolAddressView{indexedStructAddr}.baseInfo();
        auto substitutedIndexedField = symbolic::getRangeIndexSubstitutedHandle(
            factory, indexedFieldAddr.asExpr(), indexedRangeBase, index);
        auto expectedStructAddr = factory.symbolAddress(
            s->getType(), factory.variableAddress(s), point, index, index);
        EXPECT_EQ(substitutedIndexedField.get().get(),
                  factory.fieldAddress(firstField->getType(), record, expectedStructAddr, 0)
                      .asExpr()
                      .get()
                      .get());
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
        auto source = symbolic::Addr::symbol(setupFactory, var->getType(), point)
                          .withOffset(symbolic::LiteralExpr{setupFactory, int64_t{4}})
                          .handle();

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto sizeBefore = factory.size();
        auto evaluated = symbolic::tryEvalAsSymbolAddrHandle(factory, source.asExpr());
        ASSERT_TRUE(evaluated);
        EXPECT_GT(factory.size(), sizeBefore);
        EXPECT_EQ(evaluated.value(),
                  factory.symbolAddress(var->getType(), std::nullopt, point,
                                        factory.literal(int64_t{4})));
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
        auto legacyAddr = symbolic::Addr::symbol(setupFactory, var->getType(), point).handle();
        auto legacyAdd = setupFactory.binary(
            legacyAddr.asExpr(), symbolic::BinaryOp::Add,
            setupFactory.literal(int64_t{4}));

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto expected =
            factory.symbolAddress(var->getType(), std::nullopt, point, factory.literal(int64_t{4}));

        auto evaluatedHandle = symbolic::tryEvalAsSymbolAddrHandle(factory, legacyAdd);
        ASSERT_TRUE(evaluatedHandle);
        EXPECT_EQ(*evaluatedHandle, expected);
        EXPECT_EQ(symbolic::SymbolAddressView{evaluatedHandle.value()}.offset().get(),
                  factory.literal(int64_t{4}).get().get());

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
        auto address = factory.symbolAddress(var->getType(), std::nullopt, point);
        auto offset  = factory.literal(int64_t{4});

        auto commutedAdd = factory.binary(
            offset, symbolic::BinaryOp::Add, address.asExpr());
        auto evaluated = symbolic::tryEvalAsSymbolAddrHandle(factory, commutedAdd);
        ASSERT_TRUE(evaluated);
        EXPECT_EQ(*evaluated,
                  factory.symbolAddress(var->getType(), std::nullopt, point, offset));

        auto invalidSubtract = factory.binary(
            offset, symbolic::BinaryOp::Subtract, address.asExpr());
        EXPECT_FALSE(symbolic::tryEvalAsSymbolAddrHandle(factory, invalidSubtract));

        auto twoAddresses = factory.binary(
            address.asExpr(), symbolic::BinaryOp::Add, address.asExpr());
        EXPECT_FALSE(symbolic::tryEvalAsSymbolAddrHandle(factory, twoAddresses));
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
        auto varAddr = factory.variableAddress(var);
        auto zero    = factory.literal(0);
        auto length  = factory.literal(3);
        auto offset  = factory.literal(4);
        auto extra   = factory.unknown();

        auto base = factory.symbolAddress(
            var->getType(), std::optional<symbolic::AddrHandle>{varAddr}, point,
            std::optional<symbolic::ExprHandle>{zero}, std::nullopt);

        auto ranged = factory.withLength(base, length);
        EXPECT_EQ(ranged, factory.withLength(base, length));
        symbolic::SymbolAddressView rangedView{ranged};
        EXPECT_EQ(rangedView.offset().get(), zero.get().get());
        ASSERT_TRUE(rangedView.length());
        EXPECT_EQ(rangedView.length().value().get().get(), length.get().get());

        auto shifted = factory.withOffset(ranged, offset);
        EXPECT_EQ(shifted, factory.withOffset(ranged, offset));
        symbolic::SymbolAddressView shiftedView{shifted};
        EXPECT_EQ(shiftedView.offset().get(), offset.get().get());
        ASSERT_TRUE(shiftedView.length());
        EXPECT_EQ(shiftedView.length().value().get().get(), length.get().get());

        auto scalarAddr = factory.withoutLength(shifted);
        EXPECT_EQ(scalarAddr, factory.withoutLength(shifted));
        symbolic::SymbolAddressView scalarView{scalarAddr};
        EXPECT_EQ(scalarView.offset().get(), offset.get().get());
        EXPECT_FALSE(scalarView.length());

        auto addedOffset = factory.withAddedOffset(base, extra);
        EXPECT_EQ(addedOffset, factory.withAddedOffset(base, extra));
        symbolic::SymbolAddressView addedOffsetView{addedOffset};
        EXPECT_EQ(addedOffsetView.offset().get(),
                  factory.simplifiedBinary(zero, symbolic::BinaryOp::Add, extra)
                      .get()
                      .get());

        auto subtractedOffset = factory.withSubtractedOffset(base, extra);
        EXPECT_EQ(subtractedOffset, factory.withSubtractedOffset(base, extra));
        symbolic::SymbolAddressView subtractedOffsetView{subtractedOffset};
        EXPECT_EQ(subtractedOffsetView.offset().get(),
                  factory.simplifiedBinary(zero, symbolic::BinaryOp::Subtract, extra)
                      .get()
                      .get());

        auto addedLength = factory.withAddedLength(base, extra);
        EXPECT_EQ(addedLength, factory.withAddedLength(base, extra));
        auto expectedAddedLength = factory.simplifiedBinary(
            factory.literal(1), symbolic::BinaryOp::Add, extra);
        symbolic::SymbolAddressView addedLengthView{addedLength};
        ASSERT_TRUE(addedLengthView.length());
        EXPECT_EQ(addedLengthView.length().value().get().get(),
                  expectedAddedLength.get().get());

        auto extendedLength = factory.withAddedLength(ranged, extra);
        EXPECT_EQ(extendedLength, factory.withAddedLength(ranged, extra));
        auto expectedExtendedLength =
            factory.simplifiedBinary(length, symbolic::BinaryOp::Add, extra);
        symbolic::SymbolAddressView extendedLengthView{extendedLength};
        ASSERT_TRUE(extendedLengthView.length());
        EXPECT_EQ(extendedLengthView.length().value().get().get(),
                  expectedExtendedLength.get().get());
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
        auto source = setupFactory.symbolAddress(
            var->getType(), setupFactory.variableAddress(var), point,
            setupFactory.literal(int64_t{4}), setupFactory.literal(int64_t{2}));
        symbolic::SymbolAddressView sourceView{source};

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto importedA = factory.importExpr(source.asExpr());
        auto importedB = factory.importExpr(source.asExpr());
        auto importedAddress = factory.importAddress(source);
        EXPECT_EQ(importedA, importedB);
        EXPECT_EQ(importedAddress.asExpr(), importedA);

        auto importedView = symbolic::SymbolAddressView::tryFrom(importedA).value();
        auto importedOffset = factory.importExpr(sourceView.offset());
        ASSERT_TRUE(importedView.length());
        ASSERT_TRUE(sourceView.length());
        auto importedLength = factory.importExpr(sourceView.length().value());

        EXPECT_EQ(importedView.offset().get(), importedOffset.get().get());
        EXPECT_EQ(importedView.length().value().get().get(), importedLength.get().get());
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
        symbolic::StructureView legacyStructure{legacyExpr};

        symbolic::ExprFactory factory;
        auto importedA = factory.importExpr(legacyStructure.handle());
        auto importedB = factory.importExpr(legacyStructure.handle());
        EXPECT_EQ(importedA, importedB);

        symbolic::StructureView importedStructure{importedA};
        auto importedField0 = factory.importExpr(legacyStructure.field(0));
        auto importedField1 = factory.importExpr(legacyStructure.field(1));

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
        auto structure = factory.structure(record, layout, factory.variableAddress(var), point);
        symbolic::StructureView original{structure};
        auto originalField0 = factory.importExpr(original.field(0));
        auto originalField1 = factory.importExpr(original.field(1));
        auto replacement = factory.literal(42);

        auto updated = factory.withField(structure, 0, replacement);
        EXPECT_EQ(updated, factory.withField(structure, 0, replacement));

        symbolic::StructureView updatedView{updated};
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
        symbolic::StructureView structure{structureExpr};
        auto replacement = factory.literal(42);

        symbolic::HashExprHandleMap substitutions;
        substitutions.emplace(structure.field(0)->hash(), replacement);

        auto substituted =
            symbolic::getSubstitutedValueHandle(factory, structure.handle(), substitutions);
        symbolic::StructureView substitutedStructure{substituted};

        EXPECT_EQ(substitutedStructure.field(0), replacement);
        EXPECT_EQ(substitutedStructure.field(1), factory.importExpr(structure.field(1)));
    }
} // namespace acslg::test::unit::analyzer
