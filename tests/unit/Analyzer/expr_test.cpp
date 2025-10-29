// tests/unit/SpecGenerator/expr_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <clang/AST/AST.h>
#include <clang/AST/Decl.h>

#include "ASTExtractor.h"
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
        unique_ptr<SymbolicExpr> makeConstU64(uint64_t v) { return make_unique<LiteralExpr>(v); }

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
        auto litTrue = std::make_unique<LiteralExpr>(true);
        auto resTrue = litTrue->getACSL(config);
        ASSERT_TRUE(resTrue);
        EXPECT_EQ(resTrue.value().first, "true");
        EXPECT_TRUE(resTrue.value().second.empty());

        // Integer literal
        auto litInt = std::make_unique<LiteralExpr>(123);
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
        auto exprSimple = std::make_unique<BinaryOpExpr>(std::make_unique<LiteralExpr>(5),
                                                         BinaryOpExpr::Operator::Add,
                                                         std::make_unique<LiteralExpr>(3));
        auto resSimple  = exprSimple->getACSL(config);
        ASSERT_TRUE(resSimple);
        EXPECT_EQ(resSimple.value().first, "5 + 3");

        // Nested addition (left-child nested): (1 + 2) + 3 -> "1 + 2 + 3"
        auto innerLeft = std::make_unique<BinaryOpExpr>(std::make_unique<LiteralExpr>(1),
                                                        BinaryOpExpr::Operator::Add,
                                                        std::make_unique<LiteralExpr>(2));
        auto exprLeft  = std::make_unique<BinaryOpExpr>(
            std::move(innerLeft), BinaryOpExpr::Operator::Add, std::make_unique<LiteralExpr>(3));
        auto resLeft = exprLeft->getACSL(config);
        ASSERT_TRUE(resLeft);
        EXPECT_EQ(resLeft.value().first, "1 + 2 + 3");

        // Nested addition (right-child nested): 1 + (2 + 3) -> "1 + (2 + 3)"
        auto innerRight = std::make_unique<BinaryOpExpr>(std::make_unique<LiteralExpr>(2),
                                                         BinaryOpExpr::Operator::Add,
                                                         std::make_unique<LiteralExpr>(3));
        auto exprRight  = std::make_unique<BinaryOpExpr>(
            std::make_unique<LiteralExpr>(1), BinaryOpExpr::Operator::Add, std::move(innerRight));
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
        auto offset2 = std::make_unique<LiteralExpr>(2);
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
        auto offset0 = std::make_unique<LiteralExpr>(0);
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
        auto offset5   = std::make_unique<LiteralExpr>(5);
        auto length3   = std::make_unique<LiteralExpr>(3);
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
        auto offset1         = std::make_unique<LiteralExpr>(2);

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
} // namespace acslg::test::unit::analyzer