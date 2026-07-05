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

    TEST_F(SubstituteTest, ScopedVarReplacementImportsThroughFactory) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto var0Addr = makeVariableAddr(0);
        mm.write(var0Addr, makeConstU64(42));

        auto point   = getSourcePoint(0);
        auto varNode = makeSymbolValue(0, point);
        auto result  = varNode->getSubstitutedExpr(*path, point);

        EXPECT_EQ(factory.importExpr(*result), factory.literal(uint64_t{42}));
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

    TEST_F(SubstituteTest, ScopedFromPointMismatchImportsUnchangedSymbolAddr) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto originAddr = makeVariableAddr(8);
        auto realAddr   = makeSimpleSymbolAddr(9);
        mm.write(originAddr, realAddr.clone());

        auto point = getSourcePoint(0);
        auto sym   = makeRangeAddr(/*origin id*/ 8, makeConstU64(1), nullptr, point);

        auto otherPoint = getSourcePoint(1);
        auto result = sym.getSubstitutedExpr(*path, otherPoint);
        auto *resultAddr = symbolic::cast<symbolic::Address>(result.get().get());

        EXPECT_EQ(factory.importAddress(*resultAddr), factory.importAddress(sym));
    }

    TEST_F(SubstituteTest, ScopedNoBaseSymbolAddrSubstitutionUsesFactory) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto var = getVarDecl(0);
        auto point = getSourcePoint(0);
        symbolic::SymbolAddress sym{var->getType(), std::nullopt, point};

        auto result = sym.getSubstitutedExpr(*path, point);
        auto *resultAddr = symbolic::cast<symbolic::Address>(result.get().get());

        EXPECT_EQ(factory.importAddress(*resultAddr),
                  factory.symbolAddress(var->getType(), std::nullopt, point));
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
        auto exprSimple = symbolic::makeBinaryExpr(
            makeLiteralExpr(5), BinaryOpExpr::Operator::Add, makeLiteralExpr(3));
        auto resSimple  = exprSimple->getACSL(config);
        ASSERT_TRUE(resSimple);
        EXPECT_EQ(resSimple.value().first, "5 + 3");

        // Nested addition (left-child nested): (1 + 2) + 3 -> "1 + 2 + 3"
        auto innerLeft = symbolic::makeBinaryExpr(
            makeLiteralExpr(1), BinaryOpExpr::Operator::Add, makeLiteralExpr(2));
        auto exprLeft = symbolic::makeBinaryExpr(
            std::move(innerLeft), BinaryOpExpr::Operator::Add, makeLiteralExpr(3));
        auto resLeft = exprLeft->getACSL(config);
        ASSERT_TRUE(resLeft);
        EXPECT_EQ(resLeft.value().first, "1 + 2 + 3");

        // Nested addition (right-child nested): 1 + (2 + 3) -> "1 + (2 + 3)"
        auto innerRight = symbolic::makeBinaryExpr(
            makeLiteralExpr(2), BinaryOpExpr::Operator::Add, makeLiteralExpr(3));
        auto exprRight = symbolic::makeBinaryExpr(
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

        // Prefix increment (e.g., ++x)
        auto preInc = symbolic::makeUnaryExpr(UnaryOpExpr::Operator::PreInc, std::move(symVal0));
        auto resPre = preInc->getACSL(config);
        ASSERT_TRUE(resPre);
        EXPECT_EQ(resPre.value().first, "++" + name0);

        auto var1 = getVarDecl(1);
        ASSERT_NE(var1, nullptr);
        std::string name1 = var1->getNameAsString();
        auto symVal1      = makeSymbolValue(1);

        // Postfix increment (e.g., x++)
        auto postInc = symbolic::makeUnaryExpr(UnaryOpExpr::Operator::PostInc, std::move(symVal1));
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
        EXPECT_EQ(factory.importExpr(*rightBound.value()), expected);
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

        class NonLinearBinaryProbe final : public symbolic::detail::BinaryOpExprNode {
          public:
            using symbolic::detail::BinaryOpExprNode::BinaryOpExprNode;

            ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>
            callSimplifiedExprIfLinear() const {
                return simplifiedExprIfLinear();
            }
        };

        class BaseSimplifiedProbe final : public symbolic::SymbolicExpr {
          public:
            BaseSimplifiedProbe()
                : SymbolicExpr(ExprKind::K_UnknownExpr, Type{ScalarKind::Void, 0}) {}

            ::acslg::utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> clone()
                const override {
                return symbolic::makeBinaryExpr(
                    symbolic::makeLiteralExpr(1), symbolic::BinaryOpExpr::Operator::Add,
                    symbolic::makeLiteralExpr(2));
            }

            std::string dump() const override { return "base-simplified-probe"; }

            bool equal(const symbolic::SymbolicExpr &other) const override {
                return symbolic::dyn_cast<const BaseSimplifiedProbe>(&other) != nullptr;
            }

            std::size_t hash() const override { return 314159; }

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

    TEST(ExprFactoryTest, WithValTypeDoesNotMutateFactorySharedOperation) {
        symbolic::ExprFactory factory;

        auto one = factory.literal(1);
        auto two = factory.literal(2);
        auto sum = factory.binary(one, symbolic::BinaryOpExpr::Operator::Add, two);

        auto targetType = symbolic::SymbolicExpr::Type{
            symbolic::SymbolicExpr::ScalarKind::UInt, 64};
        auto typedSum = sum->withValType(targetType);

        EXPECT_EQ(sum->getValType().kind, symbolic::SymbolicExpr::ScalarKind::Int);
        EXPECT_EQ(sum->getValType().bitWidth, 32);
        EXPECT_EQ(typedSum->getValType().kind, symbolic::SymbolicExpr::ScalarKind::UInt);
        EXPECT_EQ(typedSum->getValType().bitWidth, 64);

        const auto *typedSumNode =
            symbolic::cast<symbolic::BinaryOpExpr>(typedSum.get().get());
        EXPECT_EQ(typedSumNode->getLeft().get(), one.get().get());
        EXPECT_EQ(typedSumNode->getRight().get(), two.get().get());

        auto importedTypedSum = factory.importExpr(*typedSum);
        EXPECT_NE(importedTypedSum, sum);
        EXPECT_EQ(importedTypedSum, factory.importExpr(*typedSum));
        EXPECT_EQ(importedTypedSum->getValType().kind,
                  symbolic::SymbolicExpr::ScalarKind::UInt);
        EXPECT_EQ(importedTypedSum->getValType().bitWidth, 64);

        const auto &importedTypedSumNode =
            importedTypedSum.cast<symbolic::BinaryOpExpr>();
        EXPECT_EQ(importedTypedSumNode.getLeft().get(), one.get().get());
        EXPECT_EQ(importedTypedSumNode.getRight().get(), two.get().get());
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

    TEST(ExprFactoryTest, ScopedLegacyWithValTypeUsesFactory) {
        symbolic::ExprFactory factory;

        auto one = factory.literal(1);
        auto two = factory.literal(2);
        auto sum = factory.binary(one, symbolic::BinaryOpExpr::Operator::Add, two);
        auto targetType = symbolic::SymbolicExpr::Type{
            symbolic::SymbolicExpr::ScalarKind::UInt, 64};

        symbolic::ExprFactoryScope scope(factory);
        auto typedSum = sum->withValType(targetType);
        auto expected = factory.withValType(sum, targetType);

        EXPECT_EQ(factory.importExpr(*typedSum), expected);
        EXPECT_EQ(typedSum->getValType(), targetType);
        const auto *typedSumNode =
            symbolic::cast<symbolic::BinaryOpExpr>(typedSum.get().get());
        EXPECT_EQ(typedSumNode->getLeft().get(), one.get().get());
        EXPECT_EQ(typedSumNode->getRight().get(), two.get().get());
    }

    TEST(ExprFactoryTest, ScopedValueSubstitutionHashHitImportsReplacement) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);

        auto one = factory.literal(int64_t{1});
        auto two = factory.literal(int64_t{2});
        auto original = factory.binary(one, symbolic::BinaryOpExpr::Operator::Add, two);
        auto replacement =
            factory.binary(two, symbolic::BinaryOpExpr::Operator::Subtract, one);

        symbolic::SymbolicExpr::HashExprMap substitutions;
        substitutions.emplace(original.hash(), factory.cloneExpr(replacement));

        auto substituted = original->getSubstitutedValueExpr(substitutions);
        EXPECT_EQ(factory.importExpr(*substituted), replacement);
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
        auto varAddr = factory.variableAddress(var);
        auto x       = factory.symbolValue(symbolic::deriveType(var->getType()),
                                           varAddr, point);
        auto two     = factory.literal(2);
        auto sum     = factory.binary(x, symbolic::BinaryOpExpr::Operator::Add, two);

        symbolic::ExprFactoryScope scope(factory);
        auto simplified = sum->simplifiedExpr();
        auto *rebuilt = symbolic::cast<symbolic::BinaryOpExpr>(simplified.get().get());

        EXPECT_EQ(rebuilt->getLeft().get(),
                  factory.importExpr(*rebuilt->getLeft().get()).get().get());
        EXPECT_EQ(rebuilt->getRight().get(),
                  factory.importExpr(*rebuilt->getRight().get()).get().get());
        EXPECT_EQ(factory.importExpr(*simplified), factory.importExpr(*simplified->clone()));
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
            x->clone(), symbolic::BinaryOpExpr::Operator::Multiply, x->clone()};

        auto simplified = legacyProduct.callSimplifiedExprIfLinear();
        auto *product = symbolic::cast<symbolic::BinaryOpExpr>(simplified.get().get());

        EXPECT_EQ(product->getLeft().get(),
                  factory.importExpr(*product->getLeft().get()).get().get());
        EXPECT_EQ(product->getRight().get(),
                  factory.importExpr(*product->getRight().get()).get().get());
        EXPECT_EQ(factory.importExpr(*simplified), factory.importExpr(legacyProduct));
    }

    TEST(ExprFactoryTest, ScopedDefaultSimplifiedExprImportsCloneThroughFactory) {
        BaseSimplifiedProbe legacy;

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto simplified = legacy.simplifiedExpr();
        auto expected = symbolic::makeBinaryExpr(
            symbolic::makeLiteralExpr(1), symbolic::BinaryOpExpr::Operator::Add,
            symbolic::makeLiteralExpr(2));

        EXPECT_EQ(factory.importExpr(*simplified), factory.importExpr(*expected));
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
        auto predicate = symbolic::makeBinaryExpr(
            x->clone(), symbolic::BinaryOpExpr::Operator::Equal, symbolic::makeLiteralExpr(0));
        auto wrapped = symbolic::makeBinaryExpr(
            std::move(predicate), symbolic::BinaryOpExpr::Operator::Equal,
            symbolic::makeLiteralExpr(1));

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

    TEST(ExprFactoryTest, ImportsLegacyOperationTreesIntoInternedDag) {
        symbolic::ExprFactory factory;

        auto legacy = symbolic::makeBinaryExpr(
            symbolic::makeUnaryExpr(symbolic::UnaryOpExpr::Operator::Minus,
                                    symbolic::makeLiteralExpr(1)),
            symbolic::BinaryOpExpr::Operator::Add,
            symbolic::makeLiteralExpr(2));

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
        auto substituted = legacy.getRangeIndexSubstituted(rangeBase, *replacement.get());
        const auto &node = *symbolic::cast<symbolic::BinaryOpExpr>(substituted.get().get());

        EXPECT_EQ(node.getLeft().get(), one.get().get());
        EXPECT_EQ(node.getRight().get(), two.get().get());
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
        auto substitutedLiteral = literal->getRangeIndexSubstituted(rangeBase, *index.get());
        EXPECT_EQ(factory.importExpr(*substitutedLiteral), literal);

        symbolic::SymbolicExpr::HashExprMap emptySubstitutions;
        auto valueSubstitutedLiteral = literal->getSubstitutedValueExpr(emptySubstitutions);
        EXPECT_EQ(factory.importExpr(*valueSubstitutedLiteral), literal);

        auto varAddr = factory.variableAddress(var);
        auto substitutedAddr = varAddr->getRangeIndexSubstituted(rangeBase, *index.get());
        const auto *addr = symbolic::cast<symbolic::Address>(substitutedAddr.get().get());
        EXPECT_EQ(factory.importAddress(*addr), varAddr);
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
                symbolic::cloneSymbolAddress(range.handle());
            return ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolAddress>>{
                std::move(constRange)};
        };

        auto makePred =
            []() -> ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>> {
            auto pred = symbolic::makeBinaryExpr(
                symbolic::makeRangeIndexExpr("i"),
                symbolic::BinaryOpExpr::Operator::LessThan,
                symbolic::makeLiteralExpr(3));
            std::unique_ptr<const symbolic::SymbolicExpr> constPred =
                std::move(pred).into_underlying();
            return ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>>{
                std::move(constPred)};
        };

        symbolic::SumOverRange sum{makeRange(), "i", point};
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
        auto rangeHandle = factory.symbolAddress(
            var->getType(), factory.variableAddress(var), point);
        rangeHandle = factory.withOffset(rangeHandle, factory.rangeIndex("i"));
        rangeHandle = factory.withLength(rangeHandle, factory.literal(int64_t{3}));
        auto range = symbolic::cloneSymbolAddress(rangeHandle);
        auto rangeBase = range->getBaseInfo();

        std::unique_ptr<const symbolic::SymbolAddress> constRange = std::move(range);
        symbolic::SumOverRange sum{
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolAddress>>{
                std::move(constRange)},
            "i",
            point};

        auto clone = sum.clone();
        EXPECT_EQ(*clone, sum);

        auto index = symbolic::makeLiteralExpr(1);
        auto substituted = sum.getRangeIndexSubstituted(rangeBase, *index);
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
        auto rangeHandle = factory.symbolAddress(
            var->getType(), factory.variableAddress(var), point);
        rangeHandle = factory.withLength(rangeHandle, factory.literal(int64_t{3}));
        auto range = symbolic::cloneSymbolAddress(rangeHandle);
        auto rangeBase = range->getBaseInfo();

        auto pred = symbolic::makeBinaryExpr(
            symbolic::makeRangeIndexExpr("i"),
            symbolic::BinaryOpExpr::Operator::LessThan,
            symbolic::makeLiteralExpr(3));

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

        auto index = symbolic::makeLiteralExpr(1);
        auto substituted = quantifier.getRangeIndexSubstituted(rangeBase, *index);
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
        auto rangeHandle = factory.symbolAddress(
            var->getType(), factory.variableAddress(var), point);
        rangeHandle = factory.withLength(rangeHandle, factory.literal(int64_t{3}));
        auto range = symbolic::cloneSymbolAddress(rangeHandle);
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

        auto index = symbolic::makeLiteralExpr(1);
        auto substituted = max.getRangeIndexSubstituted(rangeBase, *index);
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
            return symbolic::cloneSymbolAddress(rangeHandle);
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
        symbolic::SumOverRange sum{makeConstRange(std::move(sumRange)), "i", point};
        auto index = symbolic::makeLiteralExpr(1);
        auto substitutedSum = sum.getRangeIndexSubstituted(rangeBase, *index);
        const auto &sumNode =
            *symbolic::cast<symbolic::SumOverRange>(substitutedSum.get().get());
        EXPECT_EQ(sumNode.getRange().getOffset().get(), one.get().get());
        ASSERT_TRUE(sumNode.getRange().getLength());
        EXPECT_EQ(sumNode.getRange().getLength().value().get().get(), three.get().get());

        auto quantRange = makeRange();
        symbolic::QuantifierOverRange quantifier{
            makeConstRange(std::move(quantRange)),
            "i",
            symbolic::QuantifierOverRange::Quantifier::ForAll,
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>>{
                symbolic::makeRangeIndexExpr("i").into_underlying()}};
        auto substitutedQuantifier = quantifier.getRangeIndexSubstituted(rangeBase, *index);
        const auto &quantifierNode =
            *symbolic::cast<symbolic::QuantifierOverRange>(substitutedQuantifier.get().get());
        EXPECT_EQ(&quantifierNode.getPredicate(), one.get().get());

        auto maxRange = makeRange();
        symbolic::MaxMinOverRange max{
            makeConstRange(std::move(maxRange)),
            "i",
            symbolic::MaxMinOverRange::Extremum::Max,
            ::acslg::utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>>{
                symbolic::makeRangeIndexExpr("i").into_underlying()},
            point};
        auto substitutedMax = max.getRangeIndexSubstituted(rangeBase, *index);
        const auto &maxNode =
            *symbolic::cast<symbolic::MaxMinOverRange>(substitutedMax.get().get());
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
            auto range = factory.symbolAddress(
                var->getType(), factory.variableAddress(var), point,
                factory.rangeIndex("i"), factory.literal(int64_t{3}));
            return symbolic::cloneSymbolAddress(range);
        };

        auto sum = symbolic::makeSumOverRangeExpr(makeRange(), "i", point);
        const auto &sumNode = *symbolic::cast<symbolic::SumOverRange>(sum.get().get());
        EXPECT_EQ(factory.importAddress(sumNode.getRange()).get().get(), &sumNode.getRange());

        auto quantifier = symbolic::makeQuantifierOverRangeExpr(
            makeRange(), "i",
            symbolic::QuantifierOverRange::Quantifier::ForAll,
            symbolic::makeRangeIndexExpr("i"));
        const auto &quantifierNode =
            *symbolic::cast<symbolic::QuantifierOverRange>(quantifier.get().get());
        EXPECT_EQ(factory.importAddress(quantifierNode.getRange()).get().get(),
                  &quantifierNode.getRange());
        EXPECT_EQ(factory.importExpr(quantifierNode.getPredicate()).get().get(),
                  &quantifierNode.getPredicate());

        auto max = symbolic::makeMaxMinOverRangeExpr(
            makeRange(), "i",
            symbolic::MaxMinOverRange::Extremum::Max, point);
        const auto &maxNode = *symbolic::cast<symbolic::MaxMinOverRange>(max.get().get());
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

        auto defaultSymAddr = factory.symbolAddress(
            firstField->getType(), std::optional<symbolic::AddrHandle>{varAddrA}, point);
        const auto &defaultSymAddrNode = defaultSymAddr.cast<symbolic::SymbolAddress>();
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
        auto varAddr = factory.variableAddress(var);
        auto structure = factory.structure(record, layout, varAddr, point);
        const auto &structureNode = structure.cast<symbolic::Structure>();

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
        auto makeFrom = [&](const VarDecl *var) {
            std::unique_ptr<const symbolic::Address> addr =
                factory.variableAddress(var)->addressClone().into_underlying();
            return std::optional<
                ::acslg::utils::not_null<std::unique_ptr<const symbolic::Address>>>{
                ::acslg::utils::not_null<std::unique_ptr<const symbolic::Address>>{
                    std::move(addr)}};
        };

        auto scalarSym = symbolic::getSymbol(scalar->getType(), makeFrom(scalar), point);
        auto ptrSym = symbolic::getSymbol(ptr->getType(), makeFrom(ptr), point);
        auto arrSym = symbolic::getSymbol(arr->getType(), makeFrom(arr), point);
        auto stSym = symbolic::getSymbol(st->getType(), makeFrom(st), point);

        EXPECT_EQ(factory.importExpr(*scalarSym),
                  factory.symbolValue(symbolic::deriveType(scalar->getType()),
                                      factory.variableAddress(scalar), point));
        auto pointerType = llvm::cast<PointerType>(ptr->getType());
        EXPECT_EQ(factory.importExpr(*ptrSym),
                  factory.symbolAddress(pointerType->getPointeeType(),
                                        factory.variableAddress(ptr), point)
                      .asExpr());
        auto arrayType = llvm::cast<ArrayType>(arr->getType());
        EXPECT_EQ(factory.importExpr(*arrSym),
                  factory.symbolAddress(arrayType->getElementType(),
                                        factory.variableAddress(arr), point)
                      .asExpr());

        auto *record = st->getType()->getAsRecordDecl();
        ASSERT_NE(record, nullptr);
        ASSERT_TRUE(record->isCompleteDefinition());
        record = record->getDefinition();
        auto &layout = record->getASTContext().getASTRecordLayout(record);
        EXPECT_EQ(factory.importExpr(*stSym),
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

    TEST(ExprFactoryTest, ScopedMakeUnknownStructureUsesFactoryStructure) {
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
        auto structureExpr = symbolic::makeUnknownStructure(
            var->getType(),
            symbolic::cloneVariableAddress(symbolic::Addr::variable(var).handle()),
            point);
        EXPECT_GT(factory.size(), sizeBefore);

        auto expected = factory.structure(record, layout, factory.variableAddress(var), point);
        EXPECT_EQ(factory.importExpr(*structureExpr), expected);
        const auto *structure =
            symbolic::cast<symbolic::Structure>(structureExpr.get().get());
        const auto &expectedStructure = expected.cast<symbolic::Structure>();
        for (size_t i = 0; i < expectedStructure.getNumFields(); ++i)
            EXPECT_EQ(structure->getFieldValue(i).get(),
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

        auto indexedClone = symbolic::cloneSymbolAddress(indexedAddr);
        auto substitutedAddr =
            indexedClone->getRangeIndexSubstituted(rangeBase, *index.get());
        EXPECT_EQ(factory.importExpr(*substitutedAddr),
                  factory.symbolAddress(x->getType(), varAddr, point, index, index).asExpr());

        auto fieldAddr = factory.fieldAddress(
            firstField->getType(), record, factory.variableAddress(s), 0);
        auto fieldClone = symbolic::cloneFieldAddress(fieldAddr);
        auto substitutedField =
            fieldClone->getRangeIndexSubstituted(rangeBase, *index.get());
        EXPECT_EQ(factory.importExpr(*substitutedField), fieldAddr.asExpr());
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
        auto legacyPtr = symbolic::cloneSymbolAddress(
            symbolic::Addr::symbol(setupFactory, var->getType(), point)
                .withOffset(symbolic::LiteralExpr{setupFactory, int64_t{4}})
                .handle());
        const auto &legacy = *legacyPtr;

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto sizeBefore = factory.size();
        auto evaluated = legacy.tryEvalAsSymbolAddr();
        ASSERT_TRUE(evaluated);
        EXPECT_GT(factory.size(), sizeBefore);
        EXPECT_EQ(factory.importAddress(*evaluated.value()),
                  factory.symbolAddress(var->getType(), std::nullopt, point,
                                        factory.literal(int64_t{4})));
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
        auto legacyPtr = symbolic::cloneSymbolAddress(
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

        std::unique_ptr<symbolic::SymbolAddress> original;
        {
            symbolic::ExprFactory factory;
            symbolic::ExprFactoryScope scope(factory);
            auto originalAddr = symbolic::Addr::symbol(factory, var->getType(), point);
            original = symbolic::cloneSymbolAddress(originalAddr.handle());
        }
        auto withOffset =
            original->withOffset(symbolic::makeLiteralExpr(5));
        auto withLength =
            withOffset->withLength(symbolic::makeLiteralExpr(3));
        auto addedOffset = withOffset->withAddedOffset(symbolic::makeLiteralExpr(2));
        auto subtractedOffset = withOffset->withSubtractedOffset(symbolic::makeLiteralExpr(2));
        auto addedLengthFromScalar = original->withAddedLength(symbolic::makeLiteralExpr(2));
        auto addedLength = withLength->withAddedLength(symbolic::makeLiteralExpr(2));
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

        EXPECT_EQ(symbolic::cast<symbolic::detail::LiteralExprNode>(addedOffset->getOffset().get())
                      ->getLiteralValue(),
                  7);
        EXPECT_FALSE(addedOffset->getLength());

        EXPECT_EQ(symbolic::cast<symbolic::detail::LiteralExprNode>(
                      subtractedOffset->getOffset().get())
                      ->getLiteralValue(),
                  3);
        EXPECT_FALSE(subtractedOffset->getLength());

        ASSERT_TRUE(addedLengthFromScalar->getLength());
        EXPECT_EQ(symbolic::cast<symbolic::detail::LiteralExprNode>(
                      addedLengthFromScalar->getLength().value().get().get())
                      ->getLiteralValue(),
                  3);

        ASSERT_TRUE(addedLength->getLength());
        EXPECT_EQ(symbolic::cast<symbolic::detail::LiteralExprNode>(
                      addedLength->getLength().value().get().get())
                      ->getLiteralValue(),
                  5);

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

    TEST(SymbolAddressRebuildTest, ScopedRangeUpdatesReuseFactoryChildren) {
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

        auto originalAddr = symbolic::Addr::symbol(factory, var->getType(), point);
        auto original     = symbolic::cloneSymbolAddress(originalAddr.handle());
        auto offset = factory.literal(5);
        auto length = factory.literal(3);
        auto extra  = factory.unknown();

        auto sizeBefore = factory.size();
        auto withOffset = original->withOffset(factory.cloneExpr(offset));
        EXPECT_GT(factory.size(), sizeBefore);
        EXPECT_EQ(*withOffset->getOffset().get(), *offset.get().get());
        EXPECT_EQ(factory.importAddress(*withOffset),
                  factory.withOffset(factory.importAddress(*original), offset));

        sizeBefore = factory.size();
        auto withLength = withOffset->withLength(factory.cloneExpr(length));
        EXPECT_GT(factory.size(), sizeBefore);
        ASSERT_TRUE(withLength->getLength());
        EXPECT_EQ(*withLength->getLength().value().get().get(), *length.get().get());
        EXPECT_EQ(factory.importAddress(*withLength),
                  factory.withLength(factory.importAddress(*withOffset), length));

        sizeBefore = factory.size();
        auto addedOffset = withOffset->withAddedOffset(factory.cloneExpr(extra));
        EXPECT_GT(factory.size(), sizeBefore);
        auto expectedOffset =
            factory.simplifiedBinary(offset, symbolic::BinaryOpExpr::Operator::Add, extra);
        EXPECT_EQ(*addedOffset->getOffset().get(), *expectedOffset.get().get());

        sizeBefore = factory.size();
        auto addedLength = withLength->withAddedLength(factory.cloneExpr(extra));
        EXPECT_GT(factory.size(), sizeBefore);
        auto expectedLength =
            factory.simplifiedBinary(length, symbolic::BinaryOpExpr::Operator::Add, extra);
        ASSERT_TRUE(addedLength->getLength());
        EXPECT_EQ(*addedLength->getLength().value().get().get(), *expectedLength.get().get());

        sizeBefore = factory.size();
        auto resetOffset = withLength->withResetOffset();
        EXPECT_GT(factory.size(), sizeBefore);
        EXPECT_EQ(*resetOffset->getOffset().get(), *factory.literal(int64_t{0}).get().get());

        auto withoutLength = withLength->withoutLength();
        EXPECT_FALSE(withoutLength->getLength());
        EXPECT_EQ(*withoutLength->getOffset().get(), *offset.get().get());
        EXPECT_EQ(factory.importAddress(*withoutLength),
                  factory.withoutLength(factory.importAddress(*withLength)));
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
        auto structureExpr = [&]() {
            symbolic::ExprFactoryScope scope(factory);
            return makeStructureCloneWithFacade(factory, var->getType(), var, point);
        }();
        auto *structure = symbolic::cast<symbolic::Structure>(structureExpr.get().get());
        auto originalField0 = structure->getFieldValue(0)->clone();
        auto originalField1 = structure->getFieldValue(1)->clone();

        auto updated = structure->withFieldValue(0, symbolic::makeLiteralExpr(42));

        EXPECT_EQ(*structure->getFieldValue(0), *originalField0);
        EXPECT_EQ(*structure->getFieldValue(1), *originalField1);

        auto *updatedField0 =
            symbolic::cast<symbolic::detail::LiteralExprNode>(
                updated->getFieldValue(0).get());
        EXPECT_EQ(updatedField0->getLiteralValue(), 42);
        EXPECT_EQ(*updated->getFieldValue(1), *originalField1);
    }

    TEST(StructureRebuildTest, ScopedFieldUpdateRebuildsThroughFactory) {
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

        auto sizeBefore = factory.size();
        auto updated = structure->withFieldValue(0, factory.cloneExpr(replacement));
        EXPECT_GT(factory.size(), sizeBefore);

        auto importedOriginal = factory.importExpr(*structure);
        auto expectedUpdated = factory.withField(importedOriginal, 0, replacement);
        EXPECT_EQ(factory.importExpr(*updated), expectedUpdated);
        EXPECT_EQ(*updated->getFieldValue(0).get(), *replacement.get().get());
        EXPECT_EQ(*updated->getFieldValue(1).get(), *structure->getFieldValue(1).get());
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

        symbolic::SymbolicExpr::HashExprMap substitutions;
        substitutions.emplace(structure->getFieldValue(0)->hash(),
                              factory.cloneExpr(replacement));

        auto substituted = structure->getSubstitutedValueExpr(substitutions);
        const auto &substitutedStructure =
            *symbolic::cast<symbolic::Structure>(substituted.get().get());

        EXPECT_EQ(substitutedStructure.getFieldValue(0).get(), replacement.get().get());
        EXPECT_EQ(substitutedStructure.getFieldValue(1).get(),
                  factory.importExpr(*structure->getFieldValue(1)).get().get());
    }
} // namespace acslg::test::unit::analyzer
