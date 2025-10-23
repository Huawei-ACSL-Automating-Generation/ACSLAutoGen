// tests/unit/SpecGenerator/expr_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <clang/AST/AST.h>
#include <clang/AST/Decl.h>

#include "ASTExtractor.h"
#include "expr.h"
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
            if (const auto *CS = llvm::dyn_cast<CompoundStmt>(Body)) {
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

        auto spF =
            symbolic::SourcePoint::fromFuncDeclBefore(F, e.getSourceManager(), e.getLangOptions());
        auto spG =
            symbolic::SourcePoint::fromFuncDeclBefore(G, e.getSourceManager(), e.getLangOptions());

        EXPECT_TRUE(spF < spG);
        EXPECT_FALSE(spG < spF);

        auto spF2 =
            symbolic::SourcePoint::fromFuncDeclBefore(F, e.getSourceManager(), e.getLangOptions());
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
        auto sp =
            symbolic::SourcePoint::fromFuncDeclBefore(F, e.getSourceManager(), e.getLangOptions());
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
                return SourcePoint::fromFuncDeclBefore(newFuncDecl, e.getSourceManager(),
                                                       e.getLangOptions());
            }

          private:
            context::ACSLContext acslContext;

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
} // namespace acslg::test::unit::analyzer