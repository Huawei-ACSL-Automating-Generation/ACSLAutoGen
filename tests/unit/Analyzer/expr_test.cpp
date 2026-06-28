// tests/unit/SpecGenerator/expr_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <clang/AST/AST.h>
#include <clang/AST/Decl.h>

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
        mm.write(var0Addr, makeConstU64(42));

        auto point   = getSourcePoint(0);
        auto varNode = makeSymbolValue(0, point);
        auto expr    = makeNotNull(unique_ptr<SymbolicExpr>(varNode.release()));

        ASSERT_EQ(*expr->getSubstitutedExpr(*path, point), *makeConstU64(42));
    }

    TEST_F(SubstituteTest, VarWithDifferentFromIsKeptUnchanged) {
        auto var0Addr = makeVariableAddr(0);
        mm.write(var0Addr, makeConstU64(7));

        auto point   = getSourcePoint(0);
        auto varNode = makeSymbolValue(0, point);

        auto exprBefore = varNode->clone();
        auto expr       = makeNotNull(unique_ptr<SymbolicExpr>(varNode.release()));

        ASSERT_EQ(*expr->getSubstitutedExpr(*path, getSourcePoint(42)), *exprBefore);
    }

    TEST_F(SubstituteTest, CompositeExprIsSubstitutedRecursively) {
        // loop-entry：g1 -> 1, g2 -> 2
        mm.write(makeVariableAddr(1), makeConstU64(1));
        mm.write(makeVariableAddr(2), makeConstU64(2));

        auto point = getSourcePoint(0);

        // expr = Var(g1, point) + Var(g2, point)
        auto aVar = makeSymbolValue(1, point);
        auto bVar = makeSymbolValue(2, point);
        auto expr = makeNotNull(makeAdd(std::move(aVar), std::move(bVar)));

        auto expected = makeAdd(makeConstU64(1), makeConstU64(2));
        ASSERT_EQ(*expr->getSubstitutedExpr(*path, point), *expected);
    }

    TEST_F(SubstituteTest, SymbolAddrResolvedBaseAndOffsetApplied) {
        auto originAddr = makeVariableAddr(3);
        auto realAddr   = makeSimpleSymbolAddr(4);
        mm.write(originAddr, realAddr.clone());

        // Var(g5) = 3
        mm.write(makeVariableAddr(5), makeConstU64(3));

        auto point = getSourcePoint(0);

        // symAddr: base=origin(g3), offset=(Var(g5,point) + 4), from=point
        auto vVar   = makeSymbolValue(5, point);
        auto offset = makeAdd(unique_ptr<SymbolicExpr>(vVar.release()), makeConstU64(4));
        auto sym    = makeRangeAddr(/*origin id*/ 3, std::move(offset), nullptr, point);

        // expect: realAddr (g4) + 7
        auto expected = makePointAddr(/*real id*/ 4, /*off*/ 7).simplifiedExpr();

        ASSERT_EQ(*sym.getSubstitutedExpr(*path, point)->simplifiedExpr(), *expected);
    }

    TEST_F(SubstituteTest, SymbolAddrUnresolvedReturnsClone) {
        auto sym = makeSimpleSymbolAddr(/*origin id*/ 6);

        ASSERT_EQ(*sym.getSubstitutedExpr(*path, defaultPoint), sym);
    }

    TEST_F(SubstituteTest, NonSymbolAddrIsCloned) {
        auto varAddr = makeVariableAddr(7);

        ASSERT_EQ(*varAddr.getSubstitutedExpr(*path, defaultPoint), varAddr);
    }

    TEST_F(SubstituteTest, FromPointMismatchReturnsUnchangedSymbolAddr) {
        auto originAddr = makeVariableAddr(8);
        auto realAddr   = makeSimpleSymbolAddr(9);
        mm.write(originAddr, realAddr.clone());

        auto point = getSourcePoint(0);
        auto sym   = makeRangeAddr(/*origin id*/ 8, makeConstU64(1), nullptr, point);

        auto otherPoint = getSourcePoint(1);

        ASSERT_EQ(*sym.getSubstitutedExpr(*path, otherPoint), sym);
    }

    TEST_F(SubstituteTest, ResolvedValueNotAddressShouldError) {
        auto originAddr = makeVariableAddr(10);
        mm.write(originAddr, makeConstU64(5));

        auto point = getSourcePoint(0);
        auto sym   = makeRangeAddr(/*origin id*/ 10, makeConstU64(0), nullptr, point);

        ASSERT_DEATH(sym.getSubstitutedExpr(*path, point), "");
        SUCCEED();
    }

    class GetACSLTest : public FixtureWithCode {};

    // Test literal expressions for correct ACSL output.
    TEST_F(GetACSLTest, Literal_GetACSL) {
        SymbolicExpr::GetACSLConfig config;
        config.noStateLabelFunctionAt = true;

        // Boolean literal
        auto litTrue = std::make_unique<detail::LiteralExprNode>(true);
        auto resTrue = litTrue->getACSL(config);
        ASSERT_TRUE(resTrue);
        EXPECT_EQ(resTrue.value().first, "true");
        EXPECT_TRUE(resTrue.value().second.empty());

        // Integer literal
        auto litInt = std::make_unique<detail::LiteralExprNode>(123);
        auto resInt = litInt->getACSL(config);
        ASSERT_TRUE(resInt);
        EXPECT_EQ(resInt.value().first, "123");
        EXPECT_TRUE(resInt.value().second.empty());
    }

    // Test binary addition and operator precedence/parentheses.
    TEST_F(GetACSLTest, BinaryOp_AdditionAndPrecedence) {
        SymbolicExpr::GetACSLConfig config;
        config.noStateLabelFunctionAt = true;

        // Simple addition: 5 + 3
        auto exprSimple = std::make_unique<BinaryOpExpr>(std::make_unique<detail::LiteralExprNode>(5),
                                                         BinaryOpExpr::Operator::Add,
                                                         std::make_unique<detail::LiteralExprNode>(3));
        auto resSimple  = exprSimple->getACSL(config);
        ASSERT_TRUE(resSimple);
        EXPECT_EQ(resSimple.value().first, "5 + 3");

        // Nested addition (left-child nested): (1 + 2) + 3 -> "1 + 2 + 3"
        auto innerLeft = std::make_unique<BinaryOpExpr>(std::make_unique<detail::LiteralExprNode>(1),
                                                        BinaryOpExpr::Operator::Add,
                                                        std::make_unique<detail::LiteralExprNode>(2));
        auto exprLeft  = std::make_unique<BinaryOpExpr>(
            std::move(innerLeft), BinaryOpExpr::Operator::Add, std::make_unique<detail::LiteralExprNode>(3));
        auto resLeft = exprLeft->getACSL(config);
        ASSERT_TRUE(resLeft);
        EXPECT_EQ(resLeft.value().first, "1 + 2 + 3");

        // Nested addition (right-child nested): 1 + (2 + 3) -> "1 + (2 + 3)"
        auto innerRight = std::make_unique<BinaryOpExpr>(std::make_unique<detail::LiteralExprNode>(2),
                                                         BinaryOpExpr::Operator::Add,
                                                         std::make_unique<detail::LiteralExprNode>(3));
        auto exprRight  = std::make_unique<BinaryOpExpr>(
            std::make_unique<detail::LiteralExprNode>(1), BinaryOpExpr::Operator::Add, std::move(innerRight));
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

        // Prefix increment (e.g., ++x)
        auto preInc =
            std::make_unique<UnaryOpExpr>(UnaryOpExpr::Operator::PreInc, std::move(symVal0));
        auto resPre = preInc->getACSL(config);
        ASSERT_TRUE(resPre);
        EXPECT_EQ(resPre.value().first, "++" + name0);

        auto var1 = getVarDecl(1);
        ASSERT_NE(var1, nullptr);
        std::string name1 = var1->getNameAsString();
        auto symVal1      = makeSymbolValue(1);

        // Postfix increment (e.g., x++)
        auto postInc =
            std::make_unique<UnaryOpExpr>(UnaryOpExpr::Operator::PostInc, std::move(symVal1));
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
        auto offset2 = std::make_unique<detail::LiteralExprNode>(2);
        auto addr2   = makeRangeAddr(0, std::move(offset2), nullptr);
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
        auto offset0 = std::make_unique<detail::LiteralExprNode>(0);
        auto addr0   = makeRangeAddr(0, std::move(offset0), nullptr);
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
        auto offset5   = std::make_unique<detail::LiteralExprNode>(5);
        auto length3   = std::make_unique<detail::LiteralExprNode>(3);
        auto addrRange = makeRangeAddr(0, std::move(offset5), std::move(length3));
        auto resRange  = addrRange.getACSLOfValue(config);
        ASSERT_TRUE(resRange);
        EXPECT_EQ(resRange.value().first, baseName + "[5 .. 7]");
        EXPECT_TRUE(resRange.value().second.empty());
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
        auto offset1         = std::make_unique<detail::LiteralExprNode>(2);

        // Attach the source point to the address
        auto addr    = makeRangeAddr(0, std::move(offset1), nullptr, sp);
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

        auto offset2 = std::make_unique<detail::LiteralExprNode>(2);
        auto addr    = makeRangeAddr(0, std::move(offset2), nullptr, filtered);

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

            ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> getSubstitutedExpr(
                const Path &,
                const symbolic::SourcePoint &) const override {
                return clone();
            }

            ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>
            getRangeIndexSubstituted(
                const symbolic::SymbolAddrBaseInfo &,
                const symbolic::SymbolicExpr &) const override {
                return clone();
            }

            ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>
            getSubstitutedValueExpr(
                const symbolic::SymbolicExpr::HashExprMap &) const override {
                return clone();
            }

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

    TEST(ExprFactoryTest, ImportsLegacyOperationTreesIntoInternedDag) {
        symbolic::ExprFactory factory;

        auto legacy = std::make_unique<symbolic::BinaryOpExpr>(
            std::make_unique<symbolic::UnaryOpExpr>(
                symbolic::UnaryOpExpr::Operator::Minus,
                std::make_unique<symbolic::detail::LiteralExprNode>(1)),
            symbolic::BinaryOpExpr::Operator::Add,
            std::make_unique<symbolic::detail::LiteralExprNode>(2));

        auto imported = factory.importExpr(*legacy);
        auto repeated = factory.importExpr(*legacy->clone());

        EXPECT_EQ(imported, repeated);
        auto one = factory.literal(1);
        auto two = factory.literal(2);

        const auto &bin = imported.cast<symbolic::BinaryOpExpr>();
        EXPECT_EQ(bin.getRight().get(), two.get().get());
        const auto *unary = symbolic::cast<symbolic::UnaryOpExpr>(bin.getLeft().get());
        EXPECT_EQ(unary->getSub().get(), one.get().get());

        symbolic::ExprFactoryScope scope(factory);
        symbolic::Expr facade{*legacy};
        EXPECT_EQ(facade.handle(), imported);
    }

    TEST(ExprFactoryTest, UnknownBuilderReusesUnknownNode) {
        symbolic::ExprFactory factory;

        auto a = factory.unknown();
        auto b = factory.unknown();

        EXPECT_EQ(a, b);
        EXPECT_TRUE(a.isa<symbolic::UnknownExpr>());
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

        auto makeRange =
            [&]() -> ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolAddress>> {
            auto range = std::make_unique<symbolic::SymbolAddress>(
                var->getType(),
                std::make_unique<symbolic::VariableAddress>(var),
                point);
            range = range->withLength(
                             std::make_unique<symbolic::detail::LiteralExprNode>(3))
                        .into_underlying();
            std::unique_ptr<const symbolic::SymbolAddress> constRange = std::move(range);
            return ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolAddress>>{
                std::move(constRange)};
        };

        auto makePred =
            []() -> ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>> {
            auto pred = std::make_unique<symbolic::BinaryOpExpr>(
                std::make_unique<symbolic::SymbolAddress::RangeIndex>("i"),
                symbolic::BinaryOpExpr::Operator::LessThan,
                std::make_unique<symbolic::detail::LiteralExprNode>(3));
            std::unique_ptr<const symbolic::SymbolicExpr> constPred = std::move(pred);
            return ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>>{
                std::move(constPred)};
        };

        symbolic::SumOverRange sum{makeRange(), "i", point};
        symbolic::QuantifierOverRange quantifier{
            makeRange(), "i", symbolic::QuantifierOverRange::Quantifier::ForAll, makePred()};
        symbolic::MaxMinOverRange max{
            makeRange(), "i", symbolic::MaxMinOverRange::Extremum::Max, point};

        symbolic::ExprFactory factory;

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

        auto range = std::make_unique<symbolic::SymbolAddress>(
            var->getType(),
            std::make_unique<symbolic::VariableAddress>(var),
            point);
        range = range->withOffset(
                         std::make_unique<symbolic::SymbolAddress::RangeIndex>("i"))
                    .into_underlying();
        range = range->withLength(
                         std::make_unique<symbolic::detail::LiteralExprNode>(3))
                    .into_underlying();
        auto rangeBase = range->getBaseInfo();

        std::unique_ptr<const symbolic::SymbolAddress> constRange = std::move(range);
        symbolic::SumOverRange sum{
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolAddress>>{
                std::move(constRange)},
            "i",
            point};

        auto clone = sum.clone();
        EXPECT_EQ(*clone, sum);

        auto substituted =
            sum.getRangeIndexSubstituted(rangeBase, symbolic::detail::LiteralExprNode{1});
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

        auto range = std::make_unique<symbolic::SymbolAddress>(
            var->getType(),
            std::make_unique<symbolic::VariableAddress>(var),
            point);
        range = range->withLength(
                         std::make_unique<symbolic::detail::LiteralExprNode>(3))
                    .into_underlying();
        auto rangeBase = range->getBaseInfo();

        auto pred = std::make_unique<symbolic::BinaryOpExpr>(
            std::make_unique<symbolic::SymbolAddress::RangeIndex>("i"),
            symbolic::BinaryOpExpr::Operator::LessThan,
            std::make_unique<symbolic::detail::LiteralExprNode>(3));

        std::unique_ptr<const symbolic::SymbolAddress> constRange = std::move(range);
        std::unique_ptr<const symbolic::SymbolicExpr> constPred = std::move(pred);
        symbolic::QuantifierOverRange quantifier{
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolAddress>>{
                std::move(constRange)},
            "i",
            symbolic::QuantifierOverRange::Quantifier::ForAll,
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>>{
                std::move(constPred)}};

        auto clone = quantifier.clone();
        EXPECT_EQ(*clone, quantifier);

        auto substituted = quantifier.getRangeIndexSubstituted(
            rangeBase, symbolic::detail::LiteralExprNode{1});
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

        auto range = std::make_unique<symbolic::SymbolAddress>(
            var->getType(),
            std::make_unique<symbolic::VariableAddress>(var),
            point);
        range = range->withLength(
                         std::make_unique<symbolic::detail::LiteralExprNode>(3))
                    .into_underlying();
        auto rangeBase = range->getBaseInfo();

        std::unique_ptr<const symbolic::SymbolAddress> constRange = std::move(range);
        symbolic::MaxMinOverRange max{
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolAddress>>{
                std::move(constRange)},
            "i",
            symbolic::MaxMinOverRange::Extremum::Max,
            point};

        auto clone = max.clone();
        EXPECT_EQ(*clone, max);

        auto substituted = max.getRangeIndexSubstituted(
            rangeBase, symbolic::detail::LiteralExprNode{1});
        EXPECT_NE(*substituted, max);
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

        std::unique_ptr<const symbolic::Address> from =
            std::make_unique<symbolic::VariableAddress>(var);
        std::unique_ptr<const symbolic::SymbolicExpr> offset =
            std::make_unique<symbolic::detail::LiteralExprNode>(4);
        std::unique_ptr<const symbolic::SymbolicExpr> length =
            std::make_unique<symbolic::detail::LiteralExprNode>(2);
        symbolic::SymbolAddress legacy{
            var->getType(),
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::Address>>{std::move(from)},
            point,
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>>{
                std::move(offset)},
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>>{
                std::move(length)}};

        symbolic::ExprFactory factory;
        auto importedA = factory.importExpr(legacy);
        auto importedB = factory.importExpr(legacy);
        EXPECT_EQ(importedA, importedB);

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

        auto legacyExpr = symbolic::makeUnknownStructure(
            var->getType(), std::make_unique<symbolic::VariableAddress>(var), point);
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

    TEST(SymbolAddressRebuildTest, RangeUpdatesDoNotMutateOriginalAddress) {
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

        auto original = std::make_unique<symbolic::SymbolAddress>(
            var->getType(), std::nullopt, point);
        auto withOffset =
            original->withOffset(std::make_unique<symbolic::detail::LiteralExprNode>(5));
        auto withLength =
            withOffset->withLength(std::make_unique<symbolic::detail::LiteralExprNode>(3));
        auto withoutLength = withLength->withoutLength();
        auto resetOffset   = withLength->withResetOffset();

        EXPECT_EQ(symbolic::cast<symbolic::detail::LiteralExprNode>(original->getOffset().get())
                      ->getLiteralValue(),
                  0);
        EXPECT_FALSE(original->getLength());

        EXPECT_EQ(symbolic::cast<symbolic::detail::LiteralExprNode>(withOffset->getOffset().get())
                      ->getLiteralValue(),
                  5);
        EXPECT_FALSE(withOffset->getLength());

        ASSERT_TRUE(withLength->getLength());
        EXPECT_EQ(symbolic::cast<symbolic::detail::LiteralExprNode>(withLength->getOffset().get())
                      ->getLiteralValue(),
                  5);
        EXPECT_EQ(symbolic::cast<symbolic::detail::LiteralExprNode>(
                      withLength->getLength().value().get().get())
                      ->getLiteralValue(),
                  3);

        EXPECT_EQ(symbolic::cast<symbolic::detail::LiteralExprNode>(withoutLength->getOffset().get())
                      ->getLiteralValue(),
                  5);
        EXPECT_FALSE(withoutLength->getLength());

        ASSERT_TRUE(resetOffset->getLength());
        EXPECT_EQ(symbolic::cast<symbolic::detail::LiteralExprNode>(resetOffset->getOffset().get())
                      ->getLiteralValue(),
                  0);
        EXPECT_EQ(symbolic::cast<symbolic::detail::LiteralExprNode>(
                      resetOffset->getLength().value().get().get())
                      ->getLiteralValue(),
                  3);
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

        auto structureExpr = symbolic::makeUnknownStructure(
            var->getType(), std::make_unique<symbolic::VariableAddress>(var), point);
        auto *structure = symbolic::cast<symbolic::Structure>(structureExpr.get().get());
        auto originalField0 = structure->getFieldValue(0)->clone();
        auto originalField1 = structure->getFieldValue(1)->clone();

        auto updated = structure->withFieldValue(
            0, std::make_unique<symbolic::detail::LiteralExprNode>(42));

        EXPECT_EQ(*structure->getFieldValue(0), *originalField0);
        EXPECT_EQ(*structure->getFieldValue(1), *originalField1);

        auto *updatedField0 =
            symbolic::cast<symbolic::detail::LiteralExprNode>(
                updated->getFieldValue(0).get());
        EXPECT_EQ(updatedField0->getLiteralValue(), 42);
        EXPECT_EQ(*updated->getFieldValue(1), *originalField1);
    }
} // namespace acslg::test::unit::analyzer
