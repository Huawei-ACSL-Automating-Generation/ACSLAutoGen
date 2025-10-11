// tests/unit/SpecGenerator/expr_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <clang/AST/AST.h>
#include <clang/AST/Decl.h>

#include "ASTExtractor.h"
#include "expr.h"

using namespace clang;
using namespace std;
using namespace Symbolic;

using ::testing::HasSubstr;

namespace {

    const Stmt *nthStmtInBody(const FunctionDecl *FD, unsigned n) {
        if (!FD || !FD->hasBody())
            return nullptr;
        const Stmt *Body = FD->getBody();
        if (const auto *CS = llvm::dyn_cast<CompoundStmt>(Body)) {
            if (n < CS->size()) {
                auto it = CS->body_begin();
                std::advance(it, n);
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

    auto spF = SourcePoint::fromFuncDeclBefore(F, e.getSourceManager(), e.getLangOptions());
    auto spG = SourcePoint::fromFuncDeclBefore(G, e.getSourceManager(), e.getLangOptions());

    EXPECT_TRUE(spF < spG);
    EXPECT_FALSE(spG < spF);

    auto spF2 = SourcePoint::fromFuncDeclBefore(F, e.getSourceManager(), e.getLangOptions());
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

    auto before0 = SourcePoint::fromStmtBefore(s0, e.getSourceManager(), e.getLangOptions());
    auto after0  = SourcePoint::fromStmtAfter(s0, e.getSourceManager(), e.getLangOptions());
    auto before1 = SourcePoint::fromStmtBefore(s1, e.getSourceManager(), e.getLangOptions());
    auto after1  = SourcePoint::fromStmtAfter(s1, e.getSourceManager(), e.getLangOptions());

    EXPECT_TRUE(before0 < after0);
    EXPECT_TRUE(before1 < after1);

    EXPECT_TRUE(after0 < before1);

    auto before0_again = SourcePoint::fromStmtBefore(s0, e.getSourceManager(), e.getLangOptions());
    EXPECT_TRUE(before0 == before0_again);
}

TEST_F(SourcePointTest, HashConsistentWithEquality) {
    const FunctionDecl *F = e.findFunc("f");
    ASSERT_NE(F, nullptr);
    const Stmt *s0 = nthStmtInBody(F, 0);
    ASSERT_NE(s0, nullptr);

    auto before0     = SourcePoint::fromStmtBefore(s0, e.getSourceManager(), e.getLangOptions());
    auto before0_bis = SourcePoint::fromStmtBefore(s0, e.getSourceManager(), e.getLangOptions());

    EXPECT_TRUE(before0 == before0_bis);
    EXPECT_EQ(before0.hash(), before0_bis.hash());

    struct WrapperHash {
        size_t operator()(const SourcePoint &sp) const noexcept { return sp.hash(); }
    };
    struct WrapperEq {
        bool operator()(const SourcePoint &a, const SourcePoint &b) const noexcept {
            return a == b;
        }
    };

    std::unordered_set<SourcePoint, WrapperHash, WrapperEq> S;
    S.insert(before0);
    S.insert(before0_bis);

    EXPECT_EQ(S.size(), 1u);
}

TEST_F(SourcePointTest, DumpIsNonEmptyAndLooksLikeLocation) {
    const FunctionDecl *F = e.findFunc("f");
    ASSERT_NE(F, nullptr);
    auto sp       = SourcePoint::fromFuncDeclBefore(F, e.getSourceManager(), e.getLangOptions());
    std::string d = sp.dump();

    EXPECT_FALSE(d.empty());
    EXPECT_THAT(d, HasSubstr(":"));
}
