// tests/unit/SpecGenerator/state_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <unordered_map>
#include <string>
#include "xmock.h"
#include "state.h"
#include "ASTExtractor.h"
#include "clang/AST/AST.h"
#include "clang/AST/Decl.h"
#include "expr.h"
#include "testHelper.h"

using ::testing::Return;
using namespace std;
using namespace clang;

namespace acslg::test::unit::analyzer {
    using namespace acslg::analyzer;
    using namespace utils;

    class TestPath : public Path {
      public:
        // MOCK_NONVIRTUAL_METHOD(
        //     unique_ptr<SymbolicExpr>, evalExpr, (const Expr *), (), TestPath);
        MOCK_NONVIRTUAL_METHOD(acslg::utils::not_null<unique_ptr<symbolic::Address>>,
                               extractLValue,
                               (const Expr *),
                               (),
                               TestPath);
    };

    // TEST(PathTest, IsUnchangedState) {
    //     using enum SymbolicExpr::ScalarKind;
    //     TestPath dummyPath;
    //     auto addr = dummyPath.allocMemory();
    //     dummyPath.updateMemory(addr, make_unique<Variable>("test", SymbolicExpr::Type{Int, 4}));

    //     ASSERT_DEATH(dummyPath.isUnchangedState(*addr), "");
    // }

    /*
    TEST(PathTest, ExtractAddress)
    {
        TestPath path;

        EXPECT_CALL(path, extractLValue)
            .WillOnce(Return(make_unique<Address>(1, SymbolicExpr::makeNull())));

        auto address = path.extractAddress((Expr *)1);
        INFO(address);
        ASSERT_DEATH(address->clone(), "");
    }

    TEST(PathTest, DumpEmptyPath)
    {
        Path path;
        EXPECT_NO_THROW(path.dump());
    }

    TEST(PathTest, AllocMemory)
    {
        ASTExtractor e(R"(
          void foo(){
            int x;
          }
        )");
        auto varDecl = e.findFirstDecl<VarDecl>();

        TestPath path;
        auto address = path.allocMemory(varDecl);

        EXPECT_EQ(path.getVarAddr().size(), 1);
        EXPECT_EQ(*path.getVarAddr().at(varDecl), *address);

        ASSERT_DEATH(path.allocMemory(varDecl), "");

        TestPath path2;
        // EXPECT_NE(*address, *path2.allocMemory());
    }

    TEST(PathTest, InsertVarState)
    {
        ASTExtractor e(R"(
          void foo(){
            int x;
            x = x + 1;
          }
        )");
        auto varDecl = e.findFirstDecl<VarDecl>();
        Expr *expr = (Expr *)1;

        TestPath path;

        ASSERT_DEATH(path.insertVarState(varDecl, expr), "");

        auto address = path.allocMemory(varDecl);
        BinaryOpExpr binaryExpr(
            address->clone().release(), BinaryOpExpr::Operator::Add, new LiteralExpr(1));
        EXPECT_CALL(path, convertExpr).WillOnce(Return(binaryExpr.clone()));

        path.insertVarState(varDecl, expr);
        unique_ptr<SymbolicExpr> state;
        EXPECT_NO_THROW(state = path.getVarState(varDecl));
        EXPECT_EQ(state->dump(), "(Address(0) + 1)");

        TestPath emptyPath;
        auto anotherAddr = emptyPath.allocMemory();

        EXPECT_NO_THROW(path.insertVarState(anotherAddr, nullptr));

        // EXPECT_NE(path.getMemoryState().size(), 1);
    }

    TEST(PathTest, InsertPathCondition)
    {
        auto expr = (Expr *)1;
        NullExpr symExpr;
        TestPath path;

        EXPECT_CALL(path, convertExpr)
            .WillOnce(Return(symExpr.clone()))
            .WillOnce(Return(symExpr.clone()))
            .WillOnce(Return(symExpr.clone()))
            .WillOnce(Return(symExpr.clone()))
            .WillOnce(Return(symExpr.clone()));

        path.insertPathCondition(expr);

        EXPECT_EQ(path.getPathConditions().size(), 1);

        path.insertPathCondition(expr, BinaryOpExpr::Operator::Add, expr);
        EXPECT_EQ(path.getPathConditions().size(), 2);

        path.insertPathCondition(nullptr);

        EXPECT_EQ(path.getPathConditions().size(), 2);

        vector<const Expr *> conds(2, (Expr *)1);
        conds.resize(5, nullptr);

        path.insertDefaultPathConds(conds);

        EXPECT_EQ(path.getPathConditions().size(), 4);
    }

    TEST(PathTest, SetReturnExpr)
    {
        TestPath path;
        Expr *expr = (Expr *)1;
        NullExpr symExpr;

        EXPECT_CALL(path, convertExpr).WillOnce(Return(symExpr.clone()));

        path.setReturnExpr(expr);

        EXPECT_NE(path.getReturnExpr(), nullptr);
    }

    TEST(PathTest, SetPathState)
    {
        TestPath path;
        path.setPathState(Path::PathState::Break);

        EXPECT_EQ(path.getPathState(), Path::PathState::Break);
        EXPECT_EQ(path.isActive(), false);

        path.setPathState(Path::PathState::Step);
        EXPECT_EQ(path.isActive(), true);
    }

    TEST(PathTest, UpdateVarState)
    {
        // TODO
    }

    TEST(PathTest, Clone)
    {
        auto equal = [](const Path &LHS, const Path &RHS) {
            if (auto &LVA = LHS.getVarAddr(), &RVA = RHS.getVarAddr(); LVA.size() == RVA.size())
            {
                for (auto &kv : LVA)
                {
                    if (*RVA.at(kv.first) != *kv.second)
                        return false;
                }
            }
            else
                return false;

            if (auto &LMS = LHS.getMemoryState(), &RMS = RHS.getMemoryState(); LMS.size() ==
    RMS.size())
            {
                for (auto &kv : LMS)
                {
                    if (*RMS.at(kv.first) != *kv.second)
                        return false;
                }
            }
            else
                return false;

            if (auto &LPC = LHS.getPathConditions(), &RPC = RHS.getPathConditions();
                LPC.size() == RPC.size())
            {
                for (size_t i = 0; i < LPC.size(); i++)
                {
                    if (*LPC[i] != *RPC[i])
                        return false;
                }
            }
            else
                return false;
            if (LHS.getPathState() != RHS.getPathState())
                return false;
            if (*LHS.getReturnExpr() != *RHS.getReturnExpr())
                return false;
            if (LHS.getAddrCounter() != RHS.getAddrCounter())
                return false;
            return true;
        };

        ASTExtractor e(R"(
          void foo(){
            int x;
          }
        )");
        auto varDecl = e.findFirstDecl<VarDecl>();

        TestPath path;
        Expr *expr = (Expr *)1;
        LiteralExpr liter_1(1), liter_2(2U);
        UnaryOpExpr un_1(UnaryOpExpr::Operator::Minus, make_unique<LiteralExpr>(3));

        auto addr = path.allocMemory(varDecl);

        BinaryOpExpr bin_1(addr->clone(), BinaryOpExpr::Operator::Equal, liter_1.clone());

        EXPECT_CALL(path, convertExpr)
            .WillOnce(Return(un_1.clone()))
            .WillOnce(Return(bin_1.clone()))
            .WillOnce(Return(liter_2.clone()));

        path.insertVarState(addr, expr);
        path.insertPathCondition(expr);
        path.setReturnExpr(expr);
        path.setPathState(Path::PathState::Step);

        auto path_2 = path.clone(), path_3 = path.clone();

        EXPECT_EQ(equal(path, *path_2), true);

        path_2->insertVarState(addr, nullptr);

        EXPECT_EQ(equal(path, *path_2), false);
        path_2.release()->~Path();

        EXPECT_EQ(equal(path, *path_3), true);
    }
        */

    namespace {
        struct MemoryModelTest : public FixtureWithCode {};
    } // namespace

    TEST_F(MemoryModelTest, ReadAfterWrite_VarAddr) {
        MemoryModel mm;

        auto addr     = makeVariableAddr(1);
        auto expr     = makeVariable(42);
        auto saveExpr = expr->clone();
        mm.write(addr, std::move(expr));

        ASSERT_EQ(expr.get(), nullptr);
        auto got = mm.read(addr);
        ASSERT_NE(got, nullopt);
        EXPECT_EQ(*got.value(), *saveExpr);
    }

    TEST_F(MemoryModelTest, Flat_Yields_All_Three_Categories) {
        MemoryModel mm;

        // noOffset
        auto baseA  = makeVariableAddr(1);
        auto eA     = makeVariable(1);
        auto saveEA = eA->clone();
        mm.write(baseA, std::move(eA));

        // constantRange
        auto rangeB = makeRangeAddr(2, /*off=*/make_unique<symbolic::LiteralExpr>(4),
                                    /*len=*/make_unique<symbolic::LiteralExpr>(2));
        auto eB     = makeVariable(2);
        auto saveEB = eB->clone();
        mm.write(rangeB, std::move(eB));

        // symbolicRange
        auto rangeC = makeRangeAddr(3, /*off=*/makeVariable(3), nullptr);
        auto eC     = makeVariable(3);
        auto saveEC = eC.get();
        mm.write(rangeC, std::move(eC));

        bool fA = false, fB = false, fC = false;
        for (auto &&[addr, value] : mm.flat()) {
            if (*value == *saveEA)
                fA = true;
            else if (*value == *saveEB)
                fB = true;
            else if (*value == *saveEC)
                fC = true;
        }

        EXPECT_TRUE(fA);
        EXPECT_TRUE(fB);
        EXPECT_TRUE(fC);
    }

    TEST_F(MemoryModelTest, ConstRange_CoverageAndOverride) {
        MemoryModel mm;
        const unsigned baseId = 10;

        // A: [0,10)
        auto aRange = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(0U),
                                    make_unique<symbolic::LiteralExpr>(10U));
        auto eA     = makeVariable(100);
        auto saveA  = eA->clone();
        mm.write(aRange, std::move(eA));

        // B: [3,8)
        auto bRange = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(3U),
                                    make_unique<symbolic::LiteralExpr>(5U));
        auto eB     = makeVariable(200);
        auto saveB  = eB->clone();
        mm.write(bRange, std::move(eB));

        // C: [1,3)
        auto cRange = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(1U),
                                    make_unique<symbolic::LiteralExpr>(2U));
        auto eC     = makeVariable(300);
        auto saveC  = eC->clone();
        mm.write(cRange, std::move(eC));

        // D: [7,10)
        auto dRange = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(7U),
                                    make_unique<symbolic::LiteralExpr>(3U));
        auto eD     = makeVariable(400);
        auto saveD  = eD->clone();
        mm.write(dRange, std::move(eD));

        //  [0] -> A
        //  [1,2] -> C
        //  [3,6] -> B
        //  [7,9] -> D
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 0, *saveA));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 1, *saveC));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 2, *saveC));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 3, *saveB));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 4, *saveB));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 5, *saveB));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 6, *saveB));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 7, *saveD));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 8, *saveD));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 9, *saveD));

        EXPECT_TRUE(ExpectReadNullAt(mm, baseId, 10));
    }

    TEST_F(MemoryModelTest, ConstRange_ExactOverrideSameInterval) {
        MemoryModel mm;
        const unsigned baseId = 11;

        // X: [5,9)
        auto r     = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(5U),
                                   make_unique<symbolic::LiteralExpr>(4U));
        auto eX    = makeVariable(500);
        auto saveX = eX->clone();
        mm.write(r, std::move(eX));

        // Y: [5,9)
        auto eY    = makeVariable(600);
        auto saveY = eY->clone();
        mm.write(r, std::move(eY));

        for (uint64_t off = 5; off < 9; ++off) {
            EXPECT_TRUE(ExpectReadEqAt(mm, baseId, off, *saveY));
        }

        EXPECT_TRUE(ExpectReadNullAt(mm, baseId, 4));
        EXPECT_TRUE(ExpectReadNullAt(mm, baseId, 9));
    }

    TEST_F(MemoryModelTest, MergeConstantRanges_TouchingSameValue_ShouldCoalesce) {
        MemoryModel mm;
        const unsigned baseId = 21;

        // Two adjacent constant ranges with the same value
        // [0,3) value=V
        auto r1 = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(0U),
                                make_unique<symbolic::LiteralExpr>(3U));
        auto v  = makeVariable(1000);
        auto sv = v->clone();
        mm.write(r1, std::move(v));

        // [3,5) value=V
        auto r2 = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(3U),
                                make_unique<symbolic::LiteralExpr>(2U));
        auto v2 = makeVariable(1000); // same value
        mm.write(r2, std::move(v2));

        // Trigger constant-range merge
        mm.mergeConstantRanges();

        // Behavioral check: read [0..4] should all yield the same value
        for (uint64_t off = 0; off < 5; ++off) {
            EXPECT_TRUE(ExpectReadEqAt(mm, baseId, off, *sv));
        }
        EXPECT_TRUE(ExpectReadNullAt(mm, baseId, 5));

        EXPECT_EQ(mm.sizeWithoutFields(), 1);
    }

    TEST_F(MemoryModelTest, MergeConstantRanges_TouchingDifferentValue_ShouldNotCoalesce) {
        MemoryModel mm;
        const unsigned baseId = 22;

        // [0,3) value=V1
        auto r1 = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(0U),
                                make_unique<symbolic::LiteralExpr>(3U));
        auto v1 = makeVariable(1111);
        auto s1 = v1->clone();
        mm.write(r1, std::move(v1));

        // [3,5) value=V2 (different value)
        auto r2 = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(3U),
                                make_unique<symbolic::LiteralExpr>(2U));
        auto v2 = makeVariable(2222);
        auto s2 = v2->clone();
        mm.write(r2, std::move(v2));

        mm.mergeConstantRanges();

        // Behavioral: left and right parts remain separate
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 0, *s1));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 1, *s1));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 2, *s1));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 3, *s2));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 4, *s2));
        EXPECT_TRUE(ExpectReadNullAt(mm, baseId, 5));

        EXPECT_EQ(mm.sizeWithoutFields(), 2);
    }

    TEST_F(MemoryModelTest, MergeConstantRanges_ThreeTouchingIntoOne) {
        MemoryModel mm;
        const unsigned baseId = 23;

        // [0,2) + [2,5) + [5,7) with the same value
        auto r1 = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(0U),
                                make_unique<symbolic::LiteralExpr>(2U));
        auto r2 = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(2U),
                                make_unique<symbolic::LiteralExpr>(3U));
        auto r3 = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(5U),
                                make_unique<symbolic::LiteralExpr>(2U));

        auto v = makeVariable(3333);
        auto s = v->clone();
        mm.write(r1, std::move(v));
        mm.write(r2, makeVariable(3333));
        mm.write(r3, makeVariable(3333));

        mm.mergeConstantRanges();

        for (uint64_t off = 0; off < 7; ++off) {
            EXPECT_TRUE(ExpectReadEqAt(mm, baseId, off, *s));
        }
        EXPECT_TRUE(ExpectReadNullAt(mm, baseId, 7));

        EXPECT_EQ(mm.sizeWithoutFields(), 1);
    }

    TEST_F(MemoryModelTest, MergeConstantRanges_BlockByDifferentMiddleValue) {
        MemoryModel mm;
        const unsigned baseId = 24;

        // [0,2) V, [2,5) W, [5,7) V → cannot merge into one due to the middle different value
        auto r1 = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(0U),
                                make_unique<symbolic::LiteralExpr>(2U));
        auto r2 = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(2U),
                                make_unique<symbolic::LiteralExpr>(3U));
        auto r3 = makeRangeAddr(baseId, make_unique<symbolic::LiteralExpr>(5U),
                                make_unique<symbolic::LiteralExpr>(2U));

        auto v = makeVariable(4444);
        auto s = v->clone();
        mm.write(r1, std::move(v));
        mm.write(r2, makeVariable(5555)); // different
        mm.write(r3, makeVariable(4444)); // same as r1

        mm.mergeConstantRanges();

        // Behavioral: 0..1 = 4444, 2..4 = 5555, 5..6 = 4444
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 0, *s));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 1, *s));

        auto w = makeVariable(5555);
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 2, *w));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 3, *w));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 4, *w));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 5, *s));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 6, *s));
        EXPECT_TRUE(ExpectReadNullAt(mm, baseId, 7));

        EXPECT_EQ(mm.sizeWithoutFields(), 3);
    }

    namespace {
        using ExprUP = ::acslg::utils::not_null<unique_ptr<symbolic::SymbolicExpr>>;
        auto makeAdd(ExprUP a, ExprUP b) {
            return make_unique<symbolic::BinaryOpExpr>(
                std::move(a), symbolic::BinaryOpExpr::Operator::Add, std::move(b));
        }
    } // namespace

    TEST_F(MemoryModelTest, MergeSymbolicRanges_ThreeSinglesChainIntoLen3) {
        MemoryModel mm;
        const unsigned baseId = 31;

        // X, X+1, X+2 each represents a single address (non-range → [off, off+1))
        auto X  = makeVariable(901); // symbolic variable expression (example)
        auto X1 = makeAdd(X->clone(), make_unique<symbolic::LiteralExpr>(1U));
        auto X2 = makeAdd(X->clone(), make_unique<symbolic::LiteralExpr>(2U));

        auto a0 = makeRangeAddr(baseId, /*off=*/std::move(X), /*len=*/nullptr);  // single @ X
        auto a1 = makeRangeAddr(baseId, /*off=*/std::move(X1), /*len=*/nullptr); // single @ X+1
        auto a2 = makeRangeAddr(baseId, /*off=*/std::move(X2), /*len=*/nullptr); // single @ X+2

        // Same value
        auto v  = makeVariable(7777);
        auto sv = v->clone();
        mm.write(a0, std::move(v));
        mm.write(a1, makeVariable(7777));
        mm.write(a2, makeVariable(7777));

        // Trigger symbolic-range merge (hash-based chaining)
        mm.mergeSymbolicRanges();

        EXPECT_EQ(mm.sizeWithoutFields(), 1);
    }

    TEST_F(MemoryModelTest, MergeSymbolicRanges_SameValueButNonContiguous_ShouldNotMerge) {
        MemoryModel mm;
        const unsigned baseId = 32;

        auto X  = makeVariable(902);
        auto X2 = makeAdd(X->clone(), make_unique<symbolic::LiteralExpr>(2U));

        auto a0 = makeRangeAddr(baseId, std::move(X), nullptr);  // single @ X
        auto a2 = makeRangeAddr(baseId, std::move(X2), nullptr); // single @ X+2

        auto v  = makeVariable(8888);
        auto sv = v->clone();
        mm.write(a0, std::move(v));
        mm.write(a2, makeVariable(8888)); // same value but with a gap of 1

        mm.mergeSymbolicRanges();

        // Two ranges with the same value but non-contiguous → must not merge (expect 2 entries)
        EXPECT_EQ(mm.sizeWithoutFields(), 2);
    }

    TEST_F(MemoryModelTest, MergeSymbolicRanges_ContiguousButDifferentValue_ShouldNotMerge) {
        MemoryModel mm;
        const unsigned baseId = 33;

        auto X  = makeVariable(903);
        auto X1 = makeAdd(X->clone(), make_unique<symbolic::LiteralExpr>(1U));

        auto a0 = makeRangeAddr(baseId, std::move(X), nullptr);  // single @ X
        auto a1 = makeRangeAddr(baseId, std::move(X1), nullptr); // single @ X+1

        auto v1 = makeVariable(10001);
        auto v2 = makeVariable(10002);
        auto s1 = v1->clone();
        mm.write(a0, std::move(v1));
        mm.write(a1, std::move(v2)); // different value

        mm.mergeSymbolicRanges();

        // Should remain as two separate entries
        EXPECT_EQ(mm.sizeWithoutFields(), 2);
        size_t countV1 = 0, countV2 = 0;
        for (auto &&[addr, value] : mm.flat()) {
            if (*value == *s1)
                ++countV1;
            else
                ++countV2;
        }
        EXPECT_EQ(countV1, 1u);
        EXPECT_EQ(countV2, 1u);
    }

    TEST_F(MemoryModelTest, MergeSymbolicRanges_DifferentBases_ShouldNeverMergeAcross) {
        MemoryModel mm;
        const unsigned baseA = 41;
        const unsigned baseB = 42;

        // For two different bases, write X and X+1 with the same values
        auto XA  = makeVariable(910);
        auto X1A = makeAdd(XA->clone(), make_unique<symbolic::LiteralExpr>(1U));
        auto a0A = makeRangeAddr(baseA, std::move(XA), nullptr);
        auto a1A = makeRangeAddr(baseA, std::move(X1A), nullptr);

        auto XB  = makeVariable(910); // same construction but different base
        auto X1B = makeAdd(XB->clone(), make_unique<symbolic::LiteralExpr>(1U));
        auto a0B = makeRangeAddr(baseB, std::move(XB), nullptr);
        auto a1B = makeRangeAddr(baseB, std::move(X1B), nullptr);

        auto vA  = makeVariable(1212);
        auto svA = vA->clone();
        auto vB  = makeVariable(1212);
        auto svB = vB->clone();

        mm.write(a0A, std::move(vA));
        mm.write(a1A, makeVariable(1212));
        mm.write(a0B, std::move(vB));
        mm.write(a1B, makeVariable(1212));

        mm.mergeSymbolicRanges();

        // Each base should merge within itself; no cross-base merge
        EXPECT_EQ(mm.sizeWithoutFields(), 2);
        size_t cntA = 0, cntB = 0;
        for (auto &&[addr, value] : mm.flat()) {
            if (*value != *svA && *value != *svB)
                continue;
            auto symbolAddr = llvm::dyn_cast<symbolic::SymbolAddress>(&addr.get());
            ASSERT_NE(symbolAddr, nullptr);
            if (symbolAddr->getBaseInfo() == a0A.getBaseInfo())
                ++cntA;
            if (symbolAddr->getBaseInfo() == a0B.getBaseInfo())
                ++cntB;
        }
        EXPECT_EQ(cntA, 1u);
        EXPECT_EQ(cntB, 1u);
    }

    TEST_F(MemoryModelTest, MergeSymbolicRanges_NonRangeFollowedByRange_ShouldChainCorrectly) {
        MemoryModel mm;
        const unsigned baseId = 34;

        // Start: a single address X
        auto X  = makeVariable(904);
        auto a0 = makeRangeAddr(baseId, std::move(X), nullptr); // single @ X

        // Successor: [X+1, X+1 + 3) → len = 3
        auto X1 = makeAdd(makeVariable(904), make_unique<symbolic::LiteralExpr>(1U));
        auto a1 = makeRangeAddr(baseId, std::move(X1), make_unique<symbolic::LiteralExpr>(3U));

        // Same value
        auto v  = makeVariable(1313);
        auto sv = v->clone();
        mm.write(a0, std::move(v));
        mm.write(a1, makeVariable(1313));

        mm.mergeSymbolicRanges();

        // Approximate check: there should be exactly one entry (start at X, total length = 1 + 3 = 4)
        EXPECT_EQ(mm.sizeWithoutFields(), 1);
        for (auto &&[addr, value] : mm.flat()) {
            auto symbolAddr = llvm::dyn_cast<symbolic::SymbolAddress>(&addr.get());
            ASSERT_NE(symbolAddr, nullptr);
            if (symbolAddr->getBaseInfo() == a0.getBaseInfo() && *value == *sv) {
                // If length is accessible and constant, also assert == 4
                if (symbolAddr->isRange()) {
                    if (auto c = symbolAddr->getLength()->tryEvalAsConstant()) {
                        EXPECT_EQ(c.value(), 4);
                        return;
                    }
                    FAIL() << symbolAddr->getLength()->dump();
                }
            }
            FAIL() << symbolAddr->dump();
        }
    }

} // namespace acslg::test::unit::analyzer