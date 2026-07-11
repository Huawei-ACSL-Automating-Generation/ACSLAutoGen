// tests/unit/SpecGenerator/expr_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <clang/AST/AST.h>
#include <clang/AST/Decl.h>
#include <limits>

#include "ASTExtractor.h"
#include "Context/context.h"
#include "Symbolic/aggregateExpr.h"
#include "Symbolic/expr.h"
#include "testHelper.h"

using namespace clang;
using namespace std;

using ::testing::HasSubstr;

namespace acslg::test::unit::analyzer {
    using namespace acslg::analyzer;
    using namespace utils;

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

        ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>
        cloneWithFactory(symbolic::ExprFactory &factory,
                         const symbolic::SymbolicExpr &expr) {
            return factory.cloneExpr(factory.importExpr(expr));
        }

        ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>
        cloneLiteralForLegacyTest(int64_t value) {
            auto &factory = symbolic::ExprFactoryScope::current();
            return factory.cloneExpr(factory.literal(value));
        }

        ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>
        cloneRangeIndexForLegacyTest(std::string_view name) {
            auto &factory = symbolic::ExprFactoryScope::current();
            return factory.cloneExpr(factory.rangeIndex(name));
        }

        ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>
        cloneBinaryForLegacyTest(
            ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> lhs,
            symbolic::BinaryOpExpr::Operator op,
            ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> rhs) {
            auto &factory = symbolic::ExprFactoryScope::current();
            return factory.cloneExpr(
                factory.binary(factory.importExpr(*lhs), op, factory.importExpr(*rhs)));
        }

        std::unique_ptr<symbolic::SymbolAddress> cloneSymbolAddressForLegacyTest(
            symbolic::AddrHandle address) {
            return std::make_unique<symbolic::SymbolAddress>(
                address.cast<symbolic::SymbolAddress>());
        }

        ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>
        makeStructureCloneWithFacade(symbolic::ExprFactory &factory,
                                     clang::QualType type,
                                     ::acslg::utils::not_null<const clang::VarDecl *> var,
                                     symbolic::SourcePoint point) {
            auto *record = type->getAsRecordDecl()->getDefinition();
            auto &layout = record->getASTContext().getASTRecordLayout(record);
            auto from    = symbolic::Addr::variable(var);
            return factory.cloneExpr(factory.structure(record, layout, from.handle(), point));
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
        unique_ptr<SymbolicExpr> makeConstU64(uint64_t v) { return make_unique<detail::LiteralExprNode>(v); }

        template <class ExprPtr> ExprHandle internForTest(const ExprPtr &expr) {
            return ExprFactoryScope::current().importExpr(*expr);
        }

        unique_ptr<SymbolicExpr> makeAdd(unique_ptr<SymbolicExpr> a, unique_ptr<SymbolicExpr> b) {
            return make_unique<BinaryOpExpr>(std::move(a), BinaryOpExpr::Operator::Add,
                                             std::move(b));
        }

        template <class T>
        static ::acslg::utils::not_null<unique_ptr<T>> makeNotNull(unique_ptr<T> p) {
            return ::acslg::utils::not_null<unique_ptr<T>>(std::move(p));
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
            symbolic::getSubstitutedExprHandle(factory, *varNode, *path, point);

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
            symbolic::getSubstitutedExprHandle(factory, *varNode, *path, point);

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
            symbolic::getSubstitutedExprHandle(factory, *varNode, *path, point);

        EXPECT_EQ(result.get().get(), factory.literal(uint64_t{42}).get().get());
    }

    TEST_F(SubstituteTest, VarWithDifferentFromIsKeptUnchanged) {
        auto var0Addr = makeVariableAddr(0);
        mm.write(var0Addr, internForTest(makeConstU64(7)));

        auto point   = getSourcePoint(0);
        auto varNode = makeSymbolValue(0, point);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto exprBefore = cloneWithFactory(factory, *varNode);
        auto result =
            symbolic::getSubstitutedExprHandle(factory, *varNode, *path, getSourcePoint(42));

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
        auto expr = makeNotNull(makeAdd(std::move(aVar), std::move(bVar)));

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto expected = makeAdd(makeConstU64(1), makeConstU64(2));
        auto result = symbolic::getSubstitutedExprHandle(factory, *expr, *path, point);
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

        auto result = symbolic::getSubstitutedExprHandle(factory, *expr, *path, point);
        auto expected = factory.binary(factory.literal(uint64_t{1}),
                                       symbolic::BinaryOpExpr::Operator::Add,
                                       factory.literal(uint64_t{2}));

        EXPECT_EQ(result.get().get(), expected.get().get());
    }

    TEST_F(SubstituteTest, SymbolAddrResolvedBaseAndOffsetApplied) {
        auto originAddr = makeVariableAddr(3);
        auto realAddr   = makeSimpleSymbolAddr(4);
        mm.write(originAddr, internForTest(realAddr.clone()));

        // Var(g5) = 3
        mm.write(makeVariableAddr(5), internForTest(makeConstU64(3)));

        auto point = getSourcePoint(0);

        // symAddr: base=origin(g3), offset=(Var(g5,point) + 4), from=point
        auto vVar   = makeSymbolValue(5, point);
        auto offset = makeAdd(unique_ptr<SymbolicExpr>(vVar.release()), makeConstU64(4));
        auto sym    = makeRangeAddr(/*origin id*/ 3, std::move(offset), nullptr, point);

        // expect: realAddr (g4) + 7
        auto expected = makePointAddr(/*real id*/ 4, /*off*/ 7).simplifiedExpr();

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto result = symbolic::getSubstitutedExprHandle(factory, sym, *path, point);
        ASSERT_EQ(*result->simplifiedExpr(), *expected);
    }

    TEST_F(SubstituteTest, SymbolAddrUnresolvedReturnsClone) {
        auto sym = makeSimpleSymbolAddr(/*origin id*/ 6);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto result = symbolic::getSubstitutedExprHandle(factory, sym, *path, defaultPoint);
        ASSERT_EQ(*result, sym);
    }

    TEST_F(SubstituteTest, NonSymbolAddrIsCloned) {
        auto varAddr = makeVariableAddr(7);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto result = symbolic::getSubstitutedExprHandle(factory, varAddr, *path, defaultPoint);
        ASSERT_EQ(*result, varAddr);
    }

    TEST_F(SubstituteTest, FromPointMismatchReturnsUnchangedSymbolAddr) {
        auto originAddr = makeVariableAddr(8);
        auto realAddr   = makeSimpleSymbolAddr(9);
        mm.write(originAddr, internForTest(realAddr.clone()));

        auto point = getSourcePoint(0);
        auto sym   = makeRangeAddr(/*origin id*/ 8, makeConstU64(1), nullptr, point);

        auto otherPoint = getSourcePoint(1);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto result = symbolic::getSubstitutedExprHandle(factory, sym, *path, otherPoint);
        ASSERT_EQ(*result, sym);
    }

    TEST_F(SubstituteTest, ScopedFromPointMismatchImportsUnchangedSymbolAddr) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto originAddr = makeVariableAddr(8);
        auto realAddr   = makeSimpleSymbolAddr(9);
        mm.write(originAddr, internForTest(realAddr.clone()));

        auto point = getSourcePoint(0);
        auto sym   = makeRangeAddr(/*origin id*/ 8, makeConstU64(1), nullptr, point);

        auto otherPoint = getSourcePoint(1);
        auto result = symbolic::getSubstitutedExprHandle(factory, sym, *path, otherPoint);
        auto *resultAddr = symbolic::cast<symbolic::Address>(result.get().get());

        EXPECT_EQ(factory.importAddress(*resultAddr), factory.importAddress(sym));
    }

    TEST_F(SubstituteTest, ScopedNoBaseSymbolAddrSubstitutionUsesFactory) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto var = getVarDecl(0);
        auto point = getSourcePoint(0);
        symbolic::SymbolAddress sym{var->getType(), std::nullopt, point};

        auto result = symbolic::getSubstitutedExprHandle(factory, sym, *path, point);
        auto *resultAddr = symbolic::cast<symbolic::Address>(result.get().get());

        EXPECT_EQ(factory.importAddress(*resultAddr),
                  factory.symbolAddress(var->getType(), std::nullopt, point));
    }

    TEST_F(SubstituteTest, ResolvedValueNotAddressShouldError) {
        auto originAddr = makeVariableAddr(10);
        mm.write(originAddr, internForTest(makeConstU64(5)));

        auto point = getSourcePoint(0);
        auto sym   = makeRangeAddr(/*origin id*/ 10, makeConstU64(0), nullptr, point);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        ASSERT_DEATH(symbolic::getSubstitutedExprHandle(factory, sym, *path, point), "");
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

        // Simple addition: 5 + 3
        auto exprSimple = cloneBinaryForLegacyTest(
            makeLiteralExpr(5), BinaryOpExpr::Operator::Add, makeLiteralExpr(3));
        auto resSimple  = exprSimple->getACSL(config);
        ASSERT_TRUE(resSimple);
        EXPECT_EQ(resSimple.value().first, "5 + 3");

        // Nested addition (left-child nested): (1 + 2) + 3 -> "1 + 2 + 3"
        auto innerLeft = cloneBinaryForLegacyTest(
            makeLiteralExpr(1), BinaryOpExpr::Operator::Add, makeLiteralExpr(2));
        auto exprLeft = cloneBinaryForLegacyTest(
            std::move(innerLeft), BinaryOpExpr::Operator::Add, makeLiteralExpr(3));
        auto resLeft = exprLeft->getACSL(config);
        ASSERT_TRUE(resLeft);
        EXPECT_EQ(resLeft.value().first, "1 + 2 + 3");

        // Nested addition (right-child nested): 1 + (2 + 3) -> "1 + (2 + 3)"
        auto innerRight = cloneBinaryForLegacyTest(
            makeLiteralExpr(2), BinaryOpExpr::Operator::Add, makeLiteralExpr(3));
        auto exprRight = cloneBinaryForLegacyTest(
            makeLiteralExpr(1), BinaryOpExpr::Operator::Add, std::move(innerRight));
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
        auto preInc = factory.unary(UnaryOpExpr::Operator::PreInc,
                                    factory.importExpr(*symVal0));
        auto resPre = preInc->getACSL(config);
        ASSERT_TRUE(resPre);
        EXPECT_EQ(resPre.value().first, "++" + name0);

        auto var1 = getVarDecl(1);
        ASSERT_NE(var1, nullptr);
        std::string name1 = var1->getNameAsString();
        auto symVal1      = makeSymbolValue(1);

        // Postfix increment (e.g., x++)
        auto postInc = factory.unary(UnaryOpExpr::Operator::PostInc,
                                     factory.importExpr(*symVal1));
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
        auto addr2 = makeRangeAddr(0, makeLiteralExpr(2).into_underlying(), nullptr);
        // ACSL should be "baseName + 2"
        auto resACSL = addr2.getACSL(config);
        ASSERT_TRUE(resACSL);
        EXPECT_EQ(resACSL.value().first, baseName + " + 2");
        EXPECT_TRUE(resACSL.value().second.empty());
        // ACSLOfValue should be "baseName[2]"
        auto resVal = addr2.getACSLOfValue(config);
        ASSERT_TRUE(resVal);
        EXPECT_EQ(resVal.value().first, baseName + "[2]");
        EXPECT_TRUE(resVal.value().second.empty());

        // Case 2: offset = 0 (no offset effectively)
        auto addr0 = makeRangeAddr(0, makeLiteralExpr(0).into_underlying(), nullptr);
        // ACSL should be just "baseName"
        auto resACSL0 = addr0.getACSL(config);
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
        SymbolicExpr::GetACSLConfig config;
        config.noStateLabelFunctionAt = true;

        auto baseVar = getVarDecl(0);
        ASSERT_NE(baseVar, nullptr);
        std::string baseName = baseVar->getNameAsString();

        // offset = 5, length = 3 => [5 .. 7]
        auto addrRange = makeRangeAddr(0, makeLiteralExpr(5).into_underlying(),
                                       makeLiteralExpr(3).into_underlying());
        auto resRange  = addrRange.getACSLOfValue(config);
        ASSERT_TRUE(resRange);
        EXPECT_EQ(resRange.value().first, baseName + "[5 .. 7]");
        EXPECT_TRUE(resRange.value().second.empty());
    }

    TEST_F(GetACSLTest, SymbolAddressRightBoundUsesFactoryScope) {
        auto addrRange = makeRangeAddr(0, makeLiteralExpr(5).into_underlying(),
                                       makeLiteralExpr(3).into_underlying());

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto rightBound = addrRange.getRightBound();
        ASSERT_TRUE(rightBound);

        auto expected = factory.binary(factory.importExpr(*addrRange.getOffset()),
                                       BinaryOpExpr::Operator::Add,
                                       factory.importExpr(*addrRange.getLength().value()));
        EXPECT_EQ(rightBound.value(), expected);
        EXPECT_EQ(addrRange.getRightBound(), rightBound);

        auto *rightBoundNode =
            symbolic::cast<symbolic::BinaryOpExpr>(rightBound.value().get().get());
        EXPECT_EQ(rightBoundNode->getLeft().get(),
                  factory.importExpr(*addrRange.getOffset()).get().get());
        EXPECT_EQ(rightBoundNode->getRight().get(),
                  factory.importExpr(*addrRange.getLength().value()).get().get());
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
        auto addr    = makeRangeAddr(0, makeLiteralExpr(2).into_underlying(), nullptr, sp);
        auto resACSL = addr.getACSL(config);
        ASSERT_TRUE(resACSL);
        EXPECT_EQ(resACSL.value().first, "\\at(" + baseName + ", Old) + 2");
        EXPECT_TRUE(resACSL.value().second.empty());
        auto resVal = addr.getACSLOfValue(config);
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

        auto addr = makeRangeAddr(0, makeLiteralExpr(2).into_underlying(), nullptr, filtered);

        auto resACSL = addr.getACSL(config);
        ASSERT_TRUE(resACSL);
        EXPECT_EQ(resACSL.value().first, baseName + " + 2");
        EXPECT_TRUE(resACSL.value().second.empty());

        auto resVal = addr.getACSLOfValue(config);
        ASSERT_TRUE(resVal);
        EXPECT_EQ(resVal.value().first, baseName + "[2]");
        EXPECT_TRUE(resVal.value().second.empty());
    }

    namespace {
        class CollisionExpr final : public symbolic::SymbolicExpr {
          public:
            explicit CollisionExpr(int id)
                : SymbolicExpr(ExprKind::K_UnknownExpr, Type{ScalarKind::Void, 0}), id_(id) {}

            ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> clone()
                const override {
                return std::make_unique<CollisionExpr>(id_);
            }

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

        class NonLinearBinaryProbe final : public symbolic::detail::BinaryOpExprNode {
          public:
            using symbolic::detail::BinaryOpExprNode::BinaryOpExprNode;

            symbolic::ExprHandle callSimplifiedExprIfLinear() const {
                return simplifiedExprIfLinear();
            }
        };

        class BaseSimplifiedProbe final : public symbolic::SymbolicExpr {
          public:
            BaseSimplifiedProbe()
                : SymbolicExpr(ExprKind::K_UnknownExpr, Type{ScalarKind::Void, 0}) {}

            ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> clone()
                const override {
                return cloneBinaryForLegacyTest(
                    cloneLiteralForLegacyTest(1), symbolic::BinaryOpExpr::Operator::Add,
                    cloneLiteralForLegacyTest(2));
            }

            std::string dump() const override { return "base-simplified-probe"; }

            bool equal(const symbolic::SymbolicExpr &other) const override {
                return symbolic::dyn_cast<const BaseSimplifiedProbe>(&other) != nullptr;
            }

            std::size_t hash() const override { return 314159; }

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
        };
    } // namespace

    TEST(ExprFactoryTest, StripSizeofFactorPreservesHandleIdentity) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto value = factory.rangeIndex("count");
        auto product = factory.binary(factory.literal(std::uint64_t{8}),
                                      symbolic::BinaryOpExpr::Operator::Multiply, value);

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

        auto a = factory.intern(std::make_unique<CollisionExpr>(1));
        auto b = factory.intern(std::make_unique<CollisionExpr>(1));
        auto c = factory.intern(std::make_unique<CollisionExpr>(2));

        EXPECT_EQ(a, b);
        EXPECT_NE(a, c);
        EXPECT_EQ(factory.size(), 2u);
    }

    TEST(ExprFactoryTest, TypedBuildersReuseEqualLiteralAndOperationNodes) {
        symbolic::ExprFactory factory;

        auto oneA = factory.literal(1);
        auto oneB = factory.literal(1);
        auto two  = factory.literal(2);

        EXPECT_EQ(oneA, oneB);
        EXPECT_NE(oneA, two);
        EXPECT_TRUE(oneA.isa<symbolic::detail::LiteralExprNode>());
        EXPECT_EQ(oneA.cast<symbolic::detail::LiteralExprNode>().getLiteralValue(), 1);

        auto sumA =
            factory.binary(oneA, symbolic::BinaryOpExpr::Operator::Add, two);
        auto sumB =
            factory.binary(oneB, symbolic::BinaryOpExpr::Operator::Add, factory.literal(2));
        auto diff = factory.binary(oneA, symbolic::BinaryOpExpr::Operator::Subtract, two);

        EXPECT_EQ(sumA, sumB);
        EXPECT_NE(sumA, diff);
        ASSERT_NE(sumA.dyn_cast<symbolic::BinaryOpExpr>(), nullptr);
        ASSERT_NE(sumA.dyn_cast<symbolic::detail::BinaryOpExprNode>(), nullptr);
        EXPECT_EQ(sumA.cast<symbolic::BinaryOpExpr>().getOperator(),
                  symbolic::BinaryOpExpr::Operator::Add);
        EXPECT_EQ(sumA.cast<symbolic::BinaryOpExpr>().getLeft().get(), oneA.get().get());
        EXPECT_EQ(sumA.cast<symbolic::BinaryOpExpr>().getRight().get(), two.get().get());

        auto negA = factory.unary(symbolic::UnaryOpExpr::Operator::Minus, oneA);
        auto negB = factory.unary(symbolic::UnaryOpExpr::Operator::Minus, oneB);
        EXPECT_EQ(negA, negB);
        EXPECT_TRUE(negA.isa<symbolic::UnaryOpExpr>());
        EXPECT_TRUE(negA.isa<symbolic::detail::UnaryOpExprNode>());
        EXPECT_EQ(negA.cast<symbolic::UnaryOpExpr>().getSub().get(), oneA.get().get());
    }

    TEST(ExprFactoryTest, CloneOfFactoryBuiltOperationsPreservesChildHandles) {
        symbolic::ExprFactory factory;

        auto one = factory.literal(1);
        auto two = factory.literal(2);
        auto sum = factory.binary(one, symbolic::BinaryOpExpr::Operator::Add, two);
        auto neg = factory.unary(symbolic::UnaryOpExpr::Operator::Minus, one);

        auto sumClone = sum->clone();
        auto *sumNode = symbolic::cast<symbolic::BinaryOpExpr>(sumClone.get().get());
        EXPECT_EQ(sumNode->getLeft().get(), one.get().get());
        EXPECT_EQ(sumNode->getRight().get(), two.get().get());

        auto negClone = neg->clone();
        auto *negNode = symbolic::cast<symbolic::UnaryOpExpr>(negClone.get().get());
        EXPECT_EQ(negNode->getSub().get(), one.get().get());
    }

    TEST(ExprFactoryTest, WithValTypeDoesNotMutateFactorySharedOperation) {
        symbolic::ExprFactory factory;

        auto one = factory.literal(1);
        auto two = factory.literal(2);
        auto sum = factory.binary(one, symbolic::BinaryOpExpr::Operator::Add, two);

        auto targetType = symbolic::SymbolicExpr::Type{
            symbolic::SymbolicExpr::ScalarKind::UInt, 64};
        auto typedSum = factory.withValType(sum, targetType);

        EXPECT_EQ(sum->getValType().kind, symbolic::SymbolicExpr::ScalarKind::Int);
        EXPECT_EQ(sum->getValType().bitWidth, 32);
        EXPECT_EQ(typedSum->getValType().kind, symbolic::SymbolicExpr::ScalarKind::UInt);
        EXPECT_EQ(typedSum->getValType().bitWidth, 64);

        const auto *typedSumNode =
            symbolic::cast<symbolic::BinaryOpExpr>(typedSum.get().get());
        EXPECT_EQ(typedSumNode->getLeft().get(), one.get().get());
        EXPECT_EQ(typedSumNode->getRight().get(), two.get().get());

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

        symbolic::ExprFactoryScope scope(factory);
        symbolic::Expr facade{one};
        auto typedFacade = facade.withType(targetType);
        EXPECT_EQ(typedFacade.handle(), typedOne);
        EXPECT_EQ(typedFacade.getValType(), targetType);
    }

    TEST(ExprFactoryTest, ValueSubstitutionHandleMapImportsReplacement) {
        symbolic::ExprFactory factory;

        auto one = factory.literal(int64_t{1});
        auto two = factory.literal(int64_t{2});
        auto original = factory.binary(one, symbolic::BinaryOpExpr::Operator::Add, two);
        auto replacement =
            factory.binary(two, symbolic::BinaryOpExpr::Operator::Subtract, one);

        symbolic::HashExprHandleMap substitutions;
        substitutions.emplace(original.hash(), replacement);

        auto substituted =
            symbolic::getSubstitutedValueHandle(factory, *original, substitutions);
        EXPECT_EQ(substituted.get().get(), replacement.get().get());
    }

    TEST(ExprFactoryTest, ValueSubstitutionHandleMapReturnsInternedReplacement) {
        symbolic::ExprFactory factory;

        auto one = factory.literal(int64_t{1});
        auto two = factory.literal(int64_t{2});
        auto original = factory.binary(one, symbolic::BinaryOpExpr::Operator::Add, two);
        auto replacement =
            factory.binary(two, symbolic::BinaryOpExpr::Operator::Subtract, one);

        symbolic::HashExprHandleMap substitutions;
        substitutions.emplace(original.hash(), replacement);

        auto substituted =
            symbolic::getSubstitutedValueHandle(factory, *original, substitutions);
        EXPECT_EQ(substituted.get().get(), replacement.get().get());
    }

    TEST(ExprFactoryTest, ValueSubstitutionHandleMapRebuildsBinaryThroughFactory) {
        symbolic::ExprFactory factory;

        auto one = factory.literal(int64_t{1});
        auto two = factory.literal(int64_t{2});
        auto three = factory.literal(int64_t{3});
        auto original = factory.binary(one, symbolic::BinaryOpExpr::Operator::Add, two);
        auto expected = factory.binary(three, symbolic::BinaryOpExpr::Operator::Add, two);

        symbolic::HashExprHandleMap substitutions;
        substitutions.emplace(one.hash(), three);

        auto substituted =
            symbolic::getSubstitutedValueHandle(factory, *original, substitutions);
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
        auto pred = factory.binary(one, symbolic::BinaryOpExpr::Operator::LessThan, two);
        auto expectedPred =
            factory.binary(three, symbolic::BinaryOpExpr::Operator::LessThan, two);
        auto original = symbolic::makeQuantifierOverRangeHandle(
            factory, range.cast<symbolic::SymbolAddress>(), "i",
            symbolic::QuantifierOverRange::Quantifier::ForAll, *pred);
        auto expected = symbolic::makeQuantifierOverRangeHandle(
            factory, range.cast<symbolic::SymbolAddress>(), "i",
            symbolic::QuantifierOverRange::Quantifier::ForAll, *expectedPred);

        symbolic::HashExprHandleMap substitutions;
        substitutions.emplace(one.hash(), three);

        auto substituted =
            symbolic::getSubstitutedValueHandle(factory, *original, substitutions);
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
        auto sum     = factory.binary(x, symbolic::BinaryOpExpr::Operator::Add, two);

        auto simplified = sum->simplifiedExpr();
        auto *rebuilt = symbolic::cast<symbolic::BinaryOpExpr>(simplified.get().get());

        EXPECT_EQ(rebuilt->getLeft().get(),
                  factory.importExpr(*rebuilt->getLeft().get()).get().get());
        EXPECT_EQ(rebuilt->getRight().get(),
                  factory.importExpr(*rebuilt->getRight().get()).get().get());
        EXPECT_EQ(factory.importExpr(*simplified),
                  factory.importExpr(*cloneWithFactory(factory, *simplified)));
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
        auto x = factory.cloneExpr(
            symbolic::Expr::symbolValue(symbolic::deriveType(var->getType()), from, point)
                .handle());
        NonLinearBinaryProbe legacyProduct{
            cloneWithFactory(factory, *x), symbolic::BinaryOpExpr::Operator::Multiply,
            cloneWithFactory(factory, *x)};

        auto simplified = legacyProduct.callSimplifiedExprIfLinear();
        const auto &product = simplified.cast<symbolic::BinaryOpExpr>();

        EXPECT_EQ(product.getLeft().get(),
                  factory.importExpr(*product.getLeft().get()).get().get());
        EXPECT_EQ(product.getRight().get(),
                  factory.importExpr(*product.getRight().get()).get().get());
        EXPECT_EQ(simplified, factory.importExpr(legacyProduct));
    }

    TEST(ExprFactoryTest, ScopedDefaultSimplifiedExprImportsCloneThroughFactory) {
        BaseSimplifiedProbe legacy;

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto simplified = legacy.simplifiedExpr();
        auto expected = cloneBinaryForLegacyTest(
            cloneLiteralForLegacyTest(1), symbolic::BinaryOpExpr::Operator::Add,
            cloneLiteralForLegacyTest(2));

        EXPECT_EQ(factory.importExpr(*simplified), factory.importExpr(*expected));
    }

    TEST(ExprFactoryTest, SimplifiedExprHandleReturnsInternedNode) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto expr = factory.binary(factory.literal(int64_t{1}),
                                   symbolic::BinaryOpExpr::Operator::Add,
                                   factory.literal(int64_t{2}));

        auto simplified = symbolic::simplifiedExprHandle(factory, *expr);

        EXPECT_EQ(simplified, factory.literal(int64_t{3}));
    }

    TEST(ExprFactoryTest, SimplifiedBinaryFallbackPreservesOperationAndChildHandles) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto i = factory.rangeIndex("i");
        auto j = factory.rangeIndex("j");
        auto product = factory.binary(
            i, symbolic::BinaryOpExpr::Operator::Multiply, j);

        auto simplified = factory.simplifiedBinary(
            i, symbolic::BinaryOpExpr::Operator::Multiply, j);

        EXPECT_EQ(simplified, product);
        const auto &node = simplified.cast<symbolic::BinaryOpExpr>();
        EXPECT_EQ(node.getLeft().get(), i.get().get());
        EXPECT_EQ(node.getRight().get(), j.get().get());
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
        auto x = factory.cloneExpr(
            symbolic::Expr::symbolValue(symbolic::deriveType(var->getType()), from, point)
                .handle());
        auto predicate = cloneBinaryForLegacyTest(
            cloneWithFactory(factory, *x), symbolic::BinaryOpExpr::Operator::Equal,
            cloneLiteralForLegacyTest(0));
        auto wrapped = cloneBinaryForLegacyTest(
            std::move(predicate), symbolic::BinaryOpExpr::Operator::Equal,
            cloneLiteralForLegacyTest(1));

        auto simplified = wrapped->simplifiedExpr();
        auto *returnedPredicate =
            symbolic::cast<symbolic::BinaryOpExpr>(simplified.get().get());

        auto expectedX = factory.importExpr(*x);
        auto zero = factory.literal(int64_t{0});
        EXPECT_EQ(returnedPredicate->getOperator(), symbolic::BinaryOpExpr::Operator::Equal);
        EXPECT_EQ(returnedPredicate->getLeft().get(), expectedX.get().get());
        EXPECT_EQ(returnedPredicate->getRight().get(), zero.get().get());
        EXPECT_EQ(factory.importExpr(*simplified),
                  factory.binary(expectedX, symbolic::BinaryOpExpr::Operator::Equal, zero));
    }

    TEST(ExprFactoryTest, ConstantEvalReturnsInternedLiteral) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto expr = factory.cloneExpr(
            factory.binary(factory.literal(int64_t{1}),
                           symbolic::BinaryOpExpr::Operator::Add,
                           factory.literal(int64_t{2})));

        auto *literal = expr->evalToConstExpr();

        ASSERT_NE(literal, nullptr);
        EXPECT_EQ(literal, factory.literal(int64_t{3}).get().get());
        EXPECT_EQ(factory.importExpr(*literal), factory.literal(int64_t{3}));

        auto simplified = expr->simplifiedExpr();
        EXPECT_EQ(factory.importExpr(*simplified), factory.literal(int64_t{3}));
    }

    TEST(ExprFactoryTest, ImportsLegacyOperationTreesIntoInternedDag) {
        symbolic::ExprFactory factory;

        auto legacy = [&]() {
            symbolic::ExprFactoryScope scope(factory);
            auto unary = factory.cloneExpr(factory.unary(
                symbolic::UnaryOpExpr::Operator::Minus, factory.literal(int64_t{1})));
            return cloneBinaryForLegacyTest(
                std::move(unary),
                symbolic::BinaryOpExpr::Operator::Add,
                cloneLiteralForLegacyTest(2));
        }();

        auto imported = factory.importExpr(*legacy);
        auto repeated = factory.importExpr(*legacy->clone());

        EXPECT_EQ(imported, repeated);
        auto one = factory.literal(int64_t{1});
        auto two = factory.literal(int64_t{2});

        const auto &bin = imported.cast<symbolic::BinaryOpExpr>();
        EXPECT_EQ(bin.getRight().get(), two.get().get());
        const auto *unary = symbolic::cast<symbolic::UnaryOpExpr>(bin.getLeft().get());
        EXPECT_EQ(unary->getSub().get(), one.get().get());

        symbolic::ExprFactoryScope scope(factory);
        symbolic::Expr facade{*legacy};
        EXPECT_EQ(facade.handle(), imported);
    }

    TEST(ExprFactoryTest, ImportsLegacyUInt64LiteralWithoutValueNarrowing) {
        symbolic::ExprFactory factory;
        const auto large = std::numeric_limits<std::uint64_t>::max();
        symbolic::detail::LiteralExprNode legacy{large};

        auto imported = factory.importExpr(legacy);
        auto expected = factory.literal(large);

        EXPECT_EQ(imported, expected);
        EXPECT_EQ(imported.get().get(), expected.get().get());
        EXPECT_EQ(imported.cast<symbolic::detail::LiteralExprNode>().getLiteralType(),
                  symbolic::detail::LiteralExprNode::LiteralType::UInt64);
    }

    TEST(ExprFactoryTest, ScopedLegacyOperationCloneImportsChildren) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto legacyBinary = make_unique<symbolic::BinaryOpExpr>(
            make_unique<symbolic::detail::LiteralExprNode>(int64_t{1}),
            symbolic::BinaryOpExpr::Operator::Add,
            make_unique<symbolic::detail::LiteralExprNode>(int64_t{2}));
        auto clonedBinary = legacyBinary->clone();
        auto &binary = *symbolic::cast<symbolic::BinaryOpExpr>(clonedBinary.get().get());

        EXPECT_EQ(binary.getLeft().get(), factory.literal(int64_t{1}).get().get());
        EXPECT_EQ(binary.getRight().get(), factory.literal(int64_t{2}).get().get());

        auto legacyUnary = make_unique<symbolic::UnaryOpExpr>(
            symbolic::UnaryOpExpr::Operator::Minus,
            make_unique<symbolic::detail::LiteralExprNode>(int64_t{3}));
        auto clonedUnary = legacyUnary->clone();
        auto &unary = *symbolic::cast<symbolic::UnaryOpExpr>(clonedUnary.get().get());

        EXPECT_EQ(unary.getSub().get(), factory.literal(int64_t{3}).get().get());
    }

    TEST(ExprFactoryTest, UnknownBuilderReusesUnknownNode) {
        symbolic::ExprFactory factory;

        auto a = factory.unknown();
        auto b = factory.unknown();

        EXPECT_EQ(a, b);
        EXPECT_TRUE(a.isa<symbolic::UnknownExpr>());
    }

    TEST(ExprFactoryTest, RangeIndexBuilderAndImportReuseNode) {
        symbolic::ExprFactory factory;

        auto k = factory.rangeIndex("k");
        auto i = factory.rangeIndex("i");

        EXPECT_EQ(k, i);
        EXPECT_TRUE(k.isa<symbolic::SymbolAddress::RangeIndex>());

        symbolic::SymbolAddress::RangeIndex legacy{"j"};
        auto imported = factory.importExpr(legacy);

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
            factory.binary(one, symbolic::BinaryOpExpr::Operator::Add, two);

        symbolic::SymbolAddress::RangeIndex legacy{"i"};
        auto substituted =
            symbolic::getRangeIndexSubstitutedHandle(factory, legacy, rangeBase, replacement);
        const auto &node = substituted.cast<symbolic::BinaryOpExpr>();

        EXPECT_EQ(node.getLeft().get(), one.get().get());
        EXPECT_EQ(node.getRight().get(), two.get().get());

        auto varAddr = factory.variableAddress(var);
        auto rangeIndex = factory.rangeIndex("i");
        auto indexedFrom =
            factory.symbolAddress(var->getType(), varAddr, point, rangeIndex, rangeIndex);
        auto indexedValue = factory.symbolValue(
            symbolic::deriveType(var->getType()), indexedFrom, point);
        auto indexedRangeBase =
            indexedFrom.cast<symbolic::SymbolAddress>().getBaseInfo();
        auto substitutedValue = symbolic::getRangeIndexSubstitutedHandle(
            factory, *indexedValue, indexedRangeBase, replacement);
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
            symbolic::getRangeIndexSubstitutedHandle(factory, *literal, rangeBase, index);
        EXPECT_EQ(substitutedLiteral.get().get(), literal.get().get());

        symbolic::HashExprHandleMap emptySubstitutions;
        auto valueSubstitutedLiteral =
            symbolic::getSubstitutedValueHandle(factory, *literal, emptySubstitutions);
        EXPECT_EQ(valueSubstitutedLiteral.get().get(), literal.get().get());

        auto varAddr = factory.variableAddress(var);
        auto substitutedAddr =
            symbolic::getRangeIndexSubstitutedHandle(factory, *varAddr, rangeBase, index);
        EXPECT_EQ(substitutedAddr.get().get(), varAddr.asExpr().get().get());
    }

    TEST(ExprFactoryTest, ImportsLegacyAggregateChildrenAsHandles) {
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

        auto makeRange =
            [&]() -> ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolAddress>> {
            auto from  = symbolic::Addr::variable(var);
            auto range = symbolic::Addr::symbol(var->getType(), from, point)
                             .withLength(symbolic::LiteralExpr{factory, int64_t{3}});
            std::unique_ptr<const symbolic::SymbolAddress> constRange =
                cloneSymbolAddressForLegacyTest(range.handle());
            return ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolAddress>>{
                std::move(constRange)};
        };

        auto makePred =
            []() -> ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>> {
            auto pred = cloneBinaryForLegacyTest(
                cloneRangeIndexForLegacyTest("i"),
                symbolic::BinaryOpExpr::Operator::LessThan,
                cloneLiteralForLegacyTest(3));
            std::unique_ptr<const symbolic::SymbolicExpr> constPred =
                std::move(pred).into_underlying();
            return ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>>{
                std::move(constPred)};
        };

        auto ownedSumRange = makeRange();
        symbolic::SumOverRange sum{factory.importAddress(*ownedSumRange), "i", point};
        symbolic::QuantifierOverRange quantifier{
            makeRange(), "i", symbolic::QuantifierOverRange::Quantifier::ForAll, makePred()};
        symbolic::MaxMinOverRange max{
            makeRange(), "i", symbolic::MaxMinOverRange::Extremum::Max, point};

        auto importedSum = factory.importExpr(sum);
        auto sumRange    = factory.importExpr(sum.getRange());
        const auto &sumNode = importedSum.cast<symbolic::SumOverRange>();
        EXPECT_EQ(&sumNode.getRange(), sumRange.get().get());
        EXPECT_EQ(importedSum, factory.importExpr(*sum.clone()));

        auto importedQuantifier = factory.importExpr(quantifier);
        auto quantifierRange    = factory.importExpr(quantifier.getRange());
        auto quantifierPred     = factory.importExpr(quantifier.getPredicate());
        const auto &quantifierNode =
            importedQuantifier.cast<symbolic::QuantifierOverRange>();
        EXPECT_EQ(&quantifierNode.getRange(), quantifierRange.get().get());
        EXPECT_EQ(&quantifierNode.getPredicate(), quantifierPred.get().get());
        EXPECT_EQ(importedQuantifier, factory.importExpr(*quantifier.clone()));

        auto importedMax = factory.importExpr(max);
        auto maxRange    = factory.importExpr(max.getRange());
        auto maxBody     = factory.importExpr(max.getExpr());
        const auto &maxNode = importedMax.cast<symbolic::MaxMinOverRange>();
        EXPECT_EQ(&maxNode.getRange(), maxRange.get().get());
        EXPECT_EQ(&maxNode.getExpr(), maxBody.get().get());
        EXPECT_EQ(importedMax, factory.importExpr(*max.clone()));
    }

    TEST(SumOverRangeRebuildTest, RangeUsesExprChildAcrossCloneAndSubstitution) {
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
        auto rangeBase = rangeHandle.cast<symbolic::SymbolAddress>().getBaseInfo();
        symbolic::SumOverRange sum{rangeHandle, "i", point};

        auto clone = sum.clone();
        EXPECT_EQ(*clone, sum);
        const auto &clonedSum = *symbolic::cast<symbolic::SumOverRange>(clone.get().get());
        EXPECT_EQ(&clonedSum.getRange(), rangeHandle.get().get());

        auto index = factory.literal(int64_t{1});
        auto substituted =
            symbolic::getRangeIndexSubstitutedHandle(factory, sum, rangeBase, index);
        EXPECT_NE(*substituted, sum);
    }

    TEST(QuantifierOverRangeRebuildTest, PredicateUsesExprChildAcrossCloneAndSubstitution) {
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
        auto range = cloneSymbolAddressForLegacyTest(rangeHandle);
        auto rangeBase = range->getBaseInfo();

        auto pred = cloneBinaryForLegacyTest(
            cloneRangeIndexForLegacyTest("i"),
            symbolic::BinaryOpExpr::Operator::LessThan,
            cloneLiteralForLegacyTest(3));

        std::unique_ptr<const symbolic::SymbolAddress> constRange = std::move(range);
        std::unique_ptr<const symbolic::SymbolicExpr> constPred =
            std::move(pred).into_underlying();
        symbolic::QuantifierOverRange quantifier{
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolAddress>>{
                std::move(constRange)},
            "i",
            symbolic::QuantifierOverRange::Quantifier::ForAll,
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>>{
                std::move(constPred)}};

        auto clone = quantifier.clone();
        EXPECT_EQ(*clone, quantifier);
        const auto &clonedQuantifier =
            *symbolic::cast<symbolic::QuantifierOverRange>(clone.get().get());
        EXPECT_EQ(&clonedQuantifier.getRange(), rangeHandle.get().get());
        EXPECT_EQ(&clonedQuantifier.getPredicate(),
                  factory.importExpr(quantifier.getPredicate()).get().get());

        auto index = factory.literal(int64_t{1});
        auto substituted =
            symbolic::getRangeIndexSubstitutedHandle(factory, quantifier, rangeBase, index);
        EXPECT_NE(*substituted, quantifier);
    }

    TEST(MaxMinOverRangeRebuildTest, BodyUsesExprChildAcrossCloneAndSubstitution) {
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
        auto range = cloneSymbolAddressForLegacyTest(rangeHandle);
        auto rangeBase = range->getBaseInfo();

        std::unique_ptr<const symbolic::SymbolAddress> constRange = std::move(range);
        symbolic::MaxMinOverRange max{
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolAddress>>{
                std::move(constRange)},
            "i",
            symbolic::MaxMinOverRange::Extremum::Max,
            point};

        auto indexedRange = factory.withOffset(rangeHandle, factory.rangeIndex("i"));
        indexedRange      = factory.withoutLength(indexedRange);
        auto expectedBody =
            symbolic::getSymbol(rangeHandle->getPointeeType(), indexedRange, point);
        EXPECT_EQ(&max.getExpr(), expectedBody.get().get());

        auto clone = max.clone();
        EXPECT_EQ(*clone, max);
        const auto &clonedMax = *symbolic::cast<symbolic::MaxMinOverRange>(clone.get().get());
        EXPECT_EQ(&clonedMax.getRange(), rangeHandle.get().get());
        EXPECT_EQ(&clonedMax.getExpr(), factory.importExpr(max.getExpr()).get().get());

        auto index = factory.literal(int64_t{1});
        auto substituted =
            symbolic::getRangeIndexSubstitutedHandle(factory, max, rangeBase, index);
        EXPECT_NE(*substituted, max);
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
            return cloneSymbolAddressForLegacyTest(rangeHandle);
        };
        auto makeConstRange = [](std::unique_ptr<symbolic::SymbolAddress> range) {
            std::unique_ptr<const symbolic::SymbolAddress> constRange = std::move(range);
            return ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolAddress>>{
                std::move(constRange)};
        };

        auto one   = factory.literal(int64_t{1});
        auto three = factory.literal(int64_t{3});

        auto sumRange = makeRange();
        auto rangeBase = sumRange->getBaseInfo();
        symbolic::SumOverRange sum{factory.importAddress(*sumRange), "i", point};
        auto substitutedSum =
            symbolic::getRangeIndexSubstitutedHandle(factory, sum, rangeBase, one);
        const auto &sumNode = substitutedSum.cast<symbolic::SumOverRange>();
        EXPECT_EQ(sumNode.getRange().getOffset().get(), one.get().get());
        ASSERT_TRUE(sumNode.getRange().getLength());
        EXPECT_EQ(sumNode.getRange().getLength().value().get().get(), three.get().get());

        auto quantRange = makeRange();
        symbolic::QuantifierOverRange quantifier{
            makeConstRange(std::move(quantRange)),
            "i",
            symbolic::QuantifierOverRange::Quantifier::ForAll,
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>>{
                cloneRangeIndexForLegacyTest("i").into_underlying()}};
        auto substitutedQuantifier =
            symbolic::getRangeIndexSubstitutedHandle(factory, quantifier, rangeBase, one);
        const auto &quantifierNode =
            substitutedQuantifier.cast<symbolic::QuantifierOverRange>();
        EXPECT_EQ(&quantifierNode.getPredicate(), one.get().get());

        auto maxRange = makeRange();
        symbolic::MaxMinOverRange max{
            makeConstRange(std::move(maxRange)),
            "i",
            symbolic::MaxMinOverRange::Extremum::Max,
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>>{
                cloneRangeIndexForLegacyTest("i").into_underlying()},
            point};
        auto substitutedMax =
            symbolic::getRangeIndexSubstitutedHandle(factory, max, rangeBase, one);
        const auto &maxNode = substitutedMax.cast<symbolic::MaxMinOverRange>();
        EXPECT_EQ(&maxNode.getExpr(), one.get().get());
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
            factory, sumRange.cast<symbolic::SymbolAddress>(), "i", point);
        const auto &sumNode = sum.cast<symbolic::SumOverRange>();
        EXPECT_EQ(factory.importAddress(sumNode.getRange()).get().get(), &sumNode.getRange());

        auto quantifier = symbolic::makeQuantifierOverRangeHandle(
            factory, makeRange(), "i",
            symbolic::QuantifierOverRange::Quantifier::ForAll,
            factory.rangeIndex("i"));
        const auto &quantifierNode = quantifier.cast<symbolic::QuantifierOverRange>();
        EXPECT_EQ(factory.importAddress(quantifierNode.getRange()).get().get(),
                  &quantifierNode.getRange());
        EXPECT_EQ(factory.importExpr(quantifierNode.getPredicate()).get().get(),
                  &quantifierNode.getPredicate());

        auto max = symbolic::makeMaxMinOverRangeHandle(
            factory, makeRange(), "i",
            symbolic::MaxMinOverRange::Extremum::Max, point);
        const auto &maxNode = max.cast<symbolic::MaxMinOverRange>();
        EXPECT_EQ(factory.importAddress(maxNode.getRange()).get().get(), &maxNode.getRange());
        EXPECT_EQ(factory.importExpr(maxNode.getExpr()).get().get(), &maxNode.getExpr());
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
        EXPECT_EQ(sum.cast<symbolic::BinaryOpExpr>().getOperator(),
                  symbolic::BinaryOpExpr::Operator::Add);
        EXPECT_EQ(product.cast<symbolic::BinaryOpExpr>().getOperator(),
                  symbolic::BinaryOpExpr::Operator::Multiply);
        EXPECT_EQ(x.cast<symbolic::detail::LiteralExprNode>().getLiteralValue(), 10);
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

        EXPECT_EQ(equalA.cast<symbolic::BinaryOpExpr>().getOperator(),
                  symbolic::BinaryOpExpr::Operator::Equal);
        EXPECT_EQ(less.cast<symbolic::BinaryOpExpr>().getOperator(),
                  symbolic::BinaryOpExpr::Operator::LessThan);
        EXPECT_EQ(greaterEqual.cast<symbolic::BinaryOpExpr>().getOperator(),
                  symbolic::BinaryOpExpr::Operator::GreaterEqual);
        EXPECT_EQ(conjunction.cast<symbolic::BinaryOpExpr>().getOperator(),
                  symbolic::BinaryOpExpr::Operator::LogicalAnd);
        EXPECT_EQ(disjunction.cast<symbolic::BinaryOpExpr>().getOperator(),
                  symbolic::BinaryOpExpr::Operator::LogicalOr);
        EXPECT_EQ(negated.cast<symbolic::UnaryOpExpr>().getOperator(),
                  symbolic::UnaryOpExpr::Operator::LogicalNot);
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
        EXPECT_EQ(negated.cast<symbolic::UnaryOpExpr>().getOperator(),
                  symbolic::UnaryOpExpr::Operator::Minus);
        EXPECT_EQ(notOne.cast<symbolic::UnaryOpExpr>().getOperator(),
                  symbolic::UnaryOpExpr::Operator::LogicalNot);
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
        EXPECT_TRUE(unknown.isa<symbolic::UnknownExpr>());
        EXPECT_EQ(index.handle(), factory.rangeIndex("i"));
        EXPECT_TRUE(index.isa<symbolic::SymbolAddress::RangeIndex>());
        EXPECT_EQ(varAddr.handle(), factory.variableAddress(var));
        EXPECT_TRUE(varAddr.isa<symbolic::VariableAddress>());
        EXPECT_EQ(symbolAddr.handle(), factory.symbolAddress(var->getType(), varAddr.handle(), point));
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
        EXPECT_TRUE(symbolValue.isa<symbolic::SymbolValue>());
    }

    TEST(AddrFacadeTest, ImportsLegacyAddressThroughCurrentFactory) {
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
        symbolic::VariableAddress legacy{var};
        symbolic::Addr imported{legacy};

        EXPECT_EQ(&addr.factory(), &factory);
        EXPECT_EQ(addr.handle(), handle);
        EXPECT_EQ(imported.handle(), handle);
        EXPECT_EQ(addr, imported);
        EXPECT_TRUE(addr.isa<symbolic::VariableAddress>());
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
        EXPECT_TRUE(varAddrA.isa<symbolic::VariableAddress>());

        symbolic::AddressBox handleBox{varAddrA};
        symbolic::AddressBox copiedHandleBox{handleBox};
        EXPECT_EQ(&handleBox.get(), varAddrA.get().get());
        EXPECT_EQ(&copiedHandleBox.get(), varAddrA.get().get());
        symbolic::AddressBox ownedBox{varAddrA->addressClone()};
        symbolic::AddressBox copiedOwnedBox{ownedBox};
        EXPECT_EQ(ownedBox, copiedOwnedBox);
        EXPECT_NE(&ownedBox.get(), &copiedOwnedBox.get());

        auto symbolValue = factory.symbolValue(
            symbolic::SymbolicExpr::Type{symbolic::SymbolicExpr::ScalarKind::Int, 32},
            varAddrA, point);
        EXPECT_EQ(symbolValue.cast<symbolic::SymbolValue>().getFromAddrHandle(), varAddrA);
        auto clonedExpr = symbolValue->clone();
        auto *clonedValue = symbolic::cast<symbolic::SymbolValue>(clonedExpr.get().get());
        EXPECT_EQ(clonedValue->getFromAddrHandle(), varAddrA);

        auto defaultSymAddr = factory.symbolAddress(
            firstField->getType(), std::optional<symbolic::AddrHandle>{varAddrA}, point);
        const auto &defaultSymAddrNode = defaultSymAddr.cast<symbolic::SymbolAddress>();
        ASSERT_TRUE(defaultSymAddrNode.getFromAddrHandle());
        EXPECT_EQ(*defaultSymAddrNode.getFromAddrHandle(), varAddrA);
        auto defaultBase = defaultSymAddrNode.getBaseInfo();
        ASSERT_TRUE(defaultBase.fromAddr_);
        ASSERT_TRUE(defaultBase.fromAddr_->handle());
        EXPECT_EQ(*defaultBase.fromAddr_->handle(), varAddrA);
        auto copiedBase = defaultBase;
        ASSERT_TRUE(copiedBase.fromAddr_->handle());
        EXPECT_EQ(*copiedBase.fromAddr_->handle(), varAddrA);
        auto clonedAddr = cloneSymbolAddressForLegacyTest(defaultSymAddr);
        ASSERT_TRUE(clonedAddr->getFromAddrHandle());
        EXPECT_EQ(*clonedAddr->getFromAddrHandle(), varAddrA);
        EXPECT_EQ(defaultSymAddrNode.getOffset().get(),
                  factory.literal(static_cast<int64_t>(symbolic::SymbolAddress::ZERO_OFFSET))
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
        EXPECT_TRUE(symAddrA.isa<symbolic::SymbolAddress>());

        auto fieldAddrA = factory.fieldAddress(firstField->getType(), record, varAddrA, 0);
        auto fieldAddrB = factory.fieldAddress(firstField->getType(), record, varAddrB, 0);
        EXPECT_EQ(fieldAddrA, fieldAddrB);
        EXPECT_TRUE(fieldAddrA.isa<symbolic::FieldAddress>());
        EXPECT_EQ(fieldAddrA.cast<symbolic::FieldAddress>().getFieldIndex(), 0u);
        ASSERT_TRUE(fieldAddrA.cast<symbolic::FieldAddress>().getBaseAddr().handle());
        EXPECT_EQ(*fieldAddrA.cast<symbolic::FieldAddress>().getBaseAddr().handle(), varAddrA);
        auto clonedAddress = fieldAddrA->addressClone();
        auto *clonedField =
            symbolic::cast<symbolic::FieldAddress>(clonedAddress.get().get());
        ASSERT_TRUE(clonedField->getBaseAddr().handle());
        EXPECT_EQ(*clonedField->getBaseAddr().handle(), varAddrA);
    }

    TEST(ExprFactoryTest, LegacySymbolAddressDefaultOffsetUsesCurrentFactory) {
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

        ASSERT_FALSE(symbolic::ExprFactoryScope::hasCurrent());
        ASSERT_DEATH({ symbolic::SymbolAddress addr(var->getType(), std::nullopt, point); }, "");

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        symbolic::SymbolAddress addr(var->getType(), std::nullopt, point);

        EXPECT_EQ(addr.getOffset().get(), factory.literal(int64_t{0}).get().get());
        EXPECT_EQ(factory.importAddress(addr),
                  factory.symbolAddress(var->getType(), std::nullopt, point));
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
        const auto &structureNode = structure.cast<symbolic::Structure>();

        auto structureFrom = symbolic::getFromAddrHandle(factory, structureNode);
        ASSERT_TRUE(structureFrom);
        EXPECT_EQ(*structureFrom, varAddr);

        ASSERT_EQ(structureNode.getNumFields(), 3u);
        auto field0 = structureNode.getFieldValue(0);
        auto field1 = structureNode.getFieldValue(1);
        auto field2 = structureNode.getFieldValue(2);

        EXPECT_NE(symbolic::dyn_cast<const symbolic::SymbolValue>(field0.get()), nullptr);
        EXPECT_NE(symbolic::dyn_cast<const symbolic::SymbolAddress>(field1.get()), nullptr);
        auto *arrayAddr = symbolic::dyn_cast<const symbolic::SymbolAddress>(field2.get());
        ASSERT_NE(arrayAddr, nullptr);
        ASSERT_TRUE(arrayAddr->getLength());

        EXPECT_EQ(field0.get(), factory.importExpr(*field0.get()).get().get());
        EXPECT_EQ(field1.get(), factory.importExpr(*field1.get()).get().get());
        EXPECT_EQ(field2.get(), factory.importExpr(*field2.get()).get().get());
        EXPECT_EQ(arrayAddr->getLength().value().get().get(),
                  factory.literal(uint64_t{2}).get().get());

        auto structureClone = structureNode.clone();
        auto *clonedStructure =
            symbolic::cast<symbolic::Structure>(structureClone.get().get());
        EXPECT_EQ(clonedStructure->getFieldValue(0).get(), field0.get());
        EXPECT_EQ(clonedStructure->getFieldValue(1).get(), field1.get());
        EXPECT_EQ(clonedStructure->getFieldValue(2).get(), field2.get());
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

    TEST(ExprFactoryTest, ScopedLegacyStructureConstructorUsesFactoryFields) {
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
        std::unique_ptr<const symbolic::Address> from =
            factory.variableAddress(st)->addressClone().into_underlying();
        symbolic::Structure legacy{
            record, layout,
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::Address>>{
                std::move(from)},
            point};

        std::vector<const FieldDecl *> fields;
        for (const auto *field : record->fields())
            fields.push_back(field);
        ASSERT_EQ(fields.size(), 3u);

        auto fromHandle = factory.variableAddress(st);
        auto field0Addr = factory.fieldAddress(fields[0]->getType(), record, fromHandle, 0);
        auto expectedField0 =
            factory.symbolValue(symbolic::deriveType(fields[0]->getType()), field0Addr, point);
        EXPECT_EQ(legacy.getFieldValue(0).get(), expectedField0.get().get());

        auto field1Addr = factory.fieldAddress(fields[1]->getType(), record, fromHandle, 1);
        auto arrayType = llvm::cast<ArrayType>(fields[1]->getType());
        auto expectedField1 = factory.symbolAddress(
            arrayType->getElementType(), field1Addr, point, std::nullopt,
            factory.literal(uint64_t{3}));
        EXPECT_EQ(legacy.getFieldValue(1).get(), expectedField1.get().get());

        auto field2Addr = factory.fieldAddress(fields[2]->getType(), record, fromHandle, 2);
        auto *nestedRecord = fields[2]->getType()->getAsRecordDecl();
        ASSERT_NE(nestedRecord, nullptr);
        ASSERT_TRUE(nestedRecord->isCompleteDefinition());
        nestedRecord = nestedRecord->getDefinition();
        auto &nestedLayout =
            nestedRecord->getASTContext().getASTRecordLayout(nestedRecord);
        auto expectedField2 =
            factory.structure(nestedRecord, nestedLayout, field2Addr, point);
        EXPECT_EQ(legacy.getFieldValue(2).get(), expectedField2.get().get());
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
        const auto &structure = structureExpr.cast<symbolic::Structure>();
        const auto &expectedStructure = expected.cast<symbolic::Structure>();
        for (size_t i = 0; i < expectedStructure.getNumFields(); ++i)
            EXPECT_EQ(structure.getFieldValue(i).get(),
                      expectedStructure.getFieldValue(i).get());
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
        auto rangeBase = indexedAddr.cast<symbolic::SymbolAddress>().getBaseInfo();

        auto substitutedAddr =
            symbolic::getRangeIndexSubstitutedHandle(factory, *indexedAddr, rangeBase, index);
        EXPECT_EQ(substitutedAddr.get().get(),
                  factory.symbolAddress(x->getType(), varAddr, point, index, index)
                      .asExpr()
                      .get()
                      .get());

        auto fieldAddr = factory.fieldAddress(
            firstField->getType(), record, factory.variableAddress(s), 0);
        auto substitutedField =
            symbolic::getRangeIndexSubstitutedHandle(factory, *fieldAddr, rangeBase, index);
        EXPECT_EQ(substitutedField.get().get(), fieldAddr.asExpr().get().get());

        auto indexedStructAddr = factory.symbolAddress(
            s->getType(), factory.variableAddress(s), point, rangeIndex, rangeIndex);
        auto indexedFieldAddr =
            factory.fieldAddress(firstField->getType(), record, indexedStructAddr, 0);
        auto indexedRangeBase =
            indexedStructAddr.cast<symbolic::SymbolAddress>().getBaseInfo();
        auto substitutedIndexedField = symbolic::getRangeIndexSubstitutedHandle(
            factory, *indexedFieldAddr, indexedRangeBase, index);
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
        auto legacyPtr = cloneSymbolAddressForLegacyTest(
            symbolic::Addr::symbol(setupFactory, var->getType(), point)
                .withOffset(symbolic::LiteralExpr{setupFactory, int64_t{4}})
                .handle());
        const auto &legacy = *legacyPtr;

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto sizeBefore = factory.size();
        auto evaluated = symbolic::tryEvalAsSymbolAddrHandle(factory, legacy);
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
        auto legacyAddr = cloneSymbolAddressForLegacyTest(
            symbolic::Addr::symbol(setupFactory, var->getType(), point).handle());
        auto legacyOffset = setupFactory.cloneExpr(setupFactory.literal(int64_t{4}));
        symbolic::BinaryOpExpr legacyAdd(std::move(legacyAddr),
                                         symbolic::BinaryOpExpr::Operator::Add,
                                         std::move(legacyOffset));

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto expected =
            factory.symbolAddress(var->getType(), std::nullopt, point, factory.literal(int64_t{4}));

        auto evaluatedHandle = symbolic::tryEvalAsSymbolAddrHandle(factory, legacyAdd);
        ASSERT_TRUE(evaluatedHandle);
        EXPECT_EQ(*evaluatedHandle, expected);
        EXPECT_EQ(evaluatedHandle->cast<symbolic::SymbolAddress>().getOffset().get(),
                  factory.literal(int64_t{4}).get().get());

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
        const auto &rangedNode = ranged.cast<symbolic::SymbolAddress>();
        EXPECT_EQ(rangedNode.getOffset().get(), zero.get().get());
        ASSERT_TRUE(rangedNode.getLength());
        EXPECT_EQ(rangedNode.getLength().value().get().get(), length.get().get());

        auto shifted = factory.withOffset(ranged, offset);
        EXPECT_EQ(shifted, factory.withOffset(ranged, offset));
        const auto &shiftedNode = shifted.cast<symbolic::SymbolAddress>();
        EXPECT_EQ(shiftedNode.getOffset().get(), offset.get().get());
        ASSERT_TRUE(shiftedNode.getLength());
        EXPECT_EQ(shiftedNode.getLength().value().get().get(), length.get().get());

        auto scalarAddr = factory.withoutLength(shifted);
        EXPECT_EQ(scalarAddr, factory.withoutLength(shifted));
        const auto &scalarNode = scalarAddr.cast<symbolic::SymbolAddress>();
        EXPECT_EQ(scalarNode.getOffset().get(), offset.get().get());
        EXPECT_FALSE(scalarNode.getLength());

        auto addedOffset = factory.withAddedOffset(base, extra);
        EXPECT_EQ(addedOffset, factory.withAddedOffset(base, extra));
        const auto &addedOffsetNode = addedOffset.cast<symbolic::SymbolAddress>();
        EXPECT_EQ(addedOffsetNode.getOffset().get(),
                  factory.simplifiedBinary(zero, symbolic::BinaryOpExpr::Operator::Add, extra)
                      .get()
                      .get());

        auto subtractedOffset = factory.withSubtractedOffset(base, extra);
        EXPECT_EQ(subtractedOffset, factory.withSubtractedOffset(base, extra));
        const auto &subtractedOffsetNode = subtractedOffset.cast<symbolic::SymbolAddress>();
        EXPECT_EQ(subtractedOffsetNode.getOffset().get(),
                  factory.simplifiedBinary(zero, symbolic::BinaryOpExpr::Operator::Subtract, extra)
                      .get()
                      .get());

        auto addedLength = factory.withAddedLength(base, extra);
        EXPECT_EQ(addedLength, factory.withAddedLength(base, extra));
        auto expectedAddedLength = factory.simplifiedBinary(
            factory.literal(1), symbolic::BinaryOpExpr::Operator::Add, extra);
        const auto &addedLengthNode = addedLength.cast<symbolic::SymbolAddress>();
        ASSERT_TRUE(addedLengthNode.getLength());
        EXPECT_EQ(addedLengthNode.getLength().value().get().get(),
                  expectedAddedLength.get().get());

        auto extendedLength = factory.withAddedLength(ranged, extra);
        EXPECT_EQ(extendedLength, factory.withAddedLength(ranged, extra));
        auto expectedExtendedLength =
            factory.simplifiedBinary(length, symbolic::BinaryOpExpr::Operator::Add, extra);
        const auto &extendedLengthNode = extendedLength.cast<symbolic::SymbolAddress>();
        ASSERT_TRUE(extendedLengthNode.getLength());
        EXPECT_EQ(extendedLengthNode.getLength().value().get().get(),
                  expectedExtendedLength.get().get());
    }

    TEST(ExprFactoryTest, ImportsLegacySymbolAddressRangeChildrenAsHandles) {
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
        auto legacyPtr = cloneSymbolAddressForLegacyTest(
            symbolic::Addr::symbol(
                var->getType(),
                symbolic::Addr::variable(var),
                point,
                symbolic::LiteralExpr{factory, int64_t{4}},
                symbolic::LiteralExpr{factory, int64_t{2}})
                .handle());
        const auto &legacy = *legacyPtr;

        auto importedA = factory.importExpr(legacy);
        auto importedB = factory.importExpr(legacy);
        auto importedAddress = factory.importAddress(legacy);
        EXPECT_EQ(importedA, importedB);
        EXPECT_EQ(importedAddress.asExpr(), importedA);

        const auto &importedAddr = importedA.cast<symbolic::SymbolAddress>();
        auto importedOffset = factory.importExpr(*legacy.getOffset());
        ASSERT_TRUE(importedAddr.getLength());
        ASSERT_TRUE(legacy.getLength());
        auto importedLength = factory.importExpr(*legacy.getLength().value());

        EXPECT_EQ(importedAddr.getOffset().get(), importedOffset.get().get());
        EXPECT_EQ(importedAddr.getLength().value().get().get(), importedLength.get().get());
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
            return makeStructureCloneWithFacade(setupFactory, var->getType(), var, point);
        }();
        auto *legacyStructure = symbolic::cast<symbolic::Structure>(legacyExpr.get().get());

        symbolic::ExprFactory factory;
        auto importedA = factory.importExpr(*legacyStructure);
        auto importedB = factory.importExpr(*legacyStructure);
        EXPECT_EQ(importedA, importedB);

        const auto &importedStructure = importedA.cast<symbolic::Structure>();
        auto importedField0 = factory.importExpr(*legacyStructure->getFieldValue(0));
        auto importedField1 = factory.importExpr(*legacyStructure->getFieldValue(1));

        EXPECT_EQ(importedStructure.getFieldValue(0).get(), importedField0.get().get());
        EXPECT_EQ(importedStructure.getFieldValue(1).get(), importedField1.get().get());
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
        const auto &original = structure.cast<symbolic::Structure>();
        auto originalField0 = factory.importExpr(*original.getFieldValue(0));
        auto originalField1 = factory.importExpr(*original.getFieldValue(1));
        auto replacement = factory.literal(42);

        auto updated = factory.withField(structure, 0, replacement);
        EXPECT_EQ(updated, factory.withField(structure, 0, replacement));

        const auto &updatedNode = updated.cast<symbolic::Structure>();
        EXPECT_EQ(original.getFieldValue(0).get(), originalField0.get().get());
        EXPECT_EQ(original.getFieldValue(1).get(), originalField1.get().get());
        EXPECT_EQ(updatedNode.getFieldValue(0).get(), replacement.get().get());
        EXPECT_EQ(updatedNode.getFieldValue(1).get(), originalField1.get().get());
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

        auto structureExpr = makeStructureCloneWithFacade(factory, var->getType(), var, point);
        auto *structure = symbolic::cast<symbolic::Structure>(structureExpr.get().get());
        auto replacement = factory.literal(42);

        symbolic::HashExprHandleMap substitutions;
        substitutions.emplace(structure->getFieldValue(0)->hash(),
                              replacement);

        auto substituted =
            symbolic::getSubstitutedValueHandle(factory, *structure, substitutions);
        const auto &substitutedStructure =
            substituted.cast<symbolic::Structure>();

        EXPECT_EQ(substitutedStructure.getFieldValue(0).get(), replacement.get().get());
        EXPECT_EQ(substitutedStructure.getFieldValue(1).get(),
                  factory.importExpr(*structure->getFieldValue(1)).get().get());
    }
} // namespace acslg::test::unit::analyzer
