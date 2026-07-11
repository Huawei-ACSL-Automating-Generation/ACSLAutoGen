// tests/unit/SpecGenerator/state_test.cpp

#include <cstdint>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <llvm/Support/Casting.h>
#include "state.h"
#include "clang/AST/Decl.h"
#include "Symbolic/expr.h"
#include "Symbolic/aggregateExpr.h" // IWYU pragma: keep
#include "testHelper.h"

using namespace std;
using namespace clang;

namespace acslg::test::unit::analyzer {
    using namespace acslg::analyzer;
    using namespace utils;

    // TEST(PathTest, IsUnchangedState) {
    //     using enum SymbolicExpr::ScalarKind;
    //     TestPath dummyPath;
    //     auto addr = dummyPath.allocMemory();
    //     dummyPath.updateMemory(addr, make_unique<SymbolValue>("test", SymbolicExpr::Type{Int, 4}));

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
            address->clone().release(), BinaryOpExpr::Operator::Add, new detail::LiteralExprNode(1));
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
        detail::LiteralExprNode liter_1(1), liter_2(2U);
        UnaryOpExpr un_1(UnaryOpExpr::Operator::Minus, make_unique<detail::LiteralExprNode>(3));

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

        struct MergeWithTest : public FixtureWithCode {
            MergeWithTest()
                : acslContext(e.getASTContext()),
                  pathA(std::make_unique<Path>(acslContext, defaultPoint)),
                  pathB(std::make_unique<Path>(acslContext, defaultPoint)) {}

            context::ACSLGContext acslContext;
            std::unique_ptr<Path> pathA;
            std::unique_ptr<Path> pathB;

            auto makeLiteral(int v) { return makeLiteralExpr(static_cast<uint64_t>(v)); }
        };
    } // namespace

    TEST_F(MergeWithTest, MergeRejectsDifferentContexts) {
        context::ACSLGContext otherContext(e.getASTContext());
        Path otherPath(otherContext, defaultPoint);

        ASSERT_DEATH(pathA->mergeWith(otherPath), "");
    }

    TEST(ProgramStateTest, StructInitializerListRebuildsFields) {
        auto postState = execOnFirstFunc(R"c(
            struct S {
                int a;
                int b;
            };

            int func(void) {
                struct S s = {1, 2};
                return s.a * 10 + s.b;
            }
        )c");

        symbolic::ExprFactoryScope scope(postState->getExprFactory());
        auto result = getReturnExprOfFirstPath(*postState)->simplifiedExpr();
        auto *lit   = symbolic::cast<symbolic::detail::LiteralExprNode>(result.get().get());
        EXPECT_EQ(lit->getLiteralValue(), 12);

        size_t flatCount = 0;
        for (auto &&[addr, value] : postState->getPaths().front()->getMemoryState().flat()) {
            auto interned = postState->getExprFactory().importAddress(addr.get());
            EXPECT_EQ(&addr.get(), interned.get().get());
            ++flatCount;
        }
        EXPECT_GE(flatCount, 2u);
    }

    TEST(PathTest, ExtractLValueHandleReusesFactoryAddress) {
        ASTExtractor extractor(R"c(
            void func(void) {
                int value;
                value;
            }
        )c");
        auto *func    = extractor.findFirstDecl<FunctionDecl>();
        auto *var     = extractor.findFirstDecl<VarDecl>();
        auto *varExpr = extractor.findFirstStmt<DeclRefExpr>();
        ASSERT_NE(func, nullptr);
        ASSERT_NE(var, nullptr);
        ASSERT_NE(varExpr, nullptr);

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        auto point = symbolic::SourcePoint::fromFuncDecl(
            func, extractor.getSourceManager(), extractor.getLangOptions());
        Path path(context, point);
        auto expected = path.allocMemory(var);

        auto first  = path.extractLValueHandle(varExpr);
        auto second = path.extractLValueHandle(varExpr);
        EXPECT_EQ(first.get().get(), expected.get().get());
        EXPECT_EQ(second.get().get(), first.get().get());
    }

    TEST(PathTest, VariableAddressHandlesSurviveAllocationAndClone) {
        ASTExtractor extractor(R"c(
            void func(void) {
                int value;
            }
        )c");
        auto *func = extractor.findFirstDecl<FunctionDecl>();
        auto *var  = extractor.findFirstDecl<VarDecl>();
        ASSERT_NE(func, nullptr);
        ASSERT_NE(var, nullptr);

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        auto point = symbolic::SourcePoint::fromFuncDecl(
            func, extractor.getSourceManager(), extractor.getLangOptions());
        Path path(context, point);

        auto first  = path.allocMemory(var);
        auto second = path.allocMemory(var);
        auto cloned = path.clone();

        EXPECT_EQ(first.get().get(), second.get().get());
        EXPECT_EQ(cloned->getVarAddr().at(var).get().get(), first.get().get());
        EXPECT_EQ(first.get().get(), context.getExprFactory().variableAddress(var).get().get());
    }

    TEST_F(MemoryModelTest, ReadAfterWrite_VarAddr) {
        MemoryModel mm;

        auto addr     = makeVariableAddr(1);
        auto expr     = makeSymbolValue(42);
        auto saveExpr = cloneExpr(*expr);
        mm.write(addr, std::move(expr));

        ASSERT_EQ(expr.get(), nullptr);
        auto got = mm.read(addr);
        ASSERT_NE(got, nullopt);
        EXPECT_EQ(*got.value(), *saveExpr);
    }

    TEST_F(MemoryModelTest, EqualStoredValuesShareInternedHandle) {
        MemoryModel mm;

        auto expected = makeSymbolValue(42);
        mm.write(makeVariableAddr(1), cloneExpr(*expected));
        mm.write(makeVariableAddr(2), cloneExpr(*expected));
        auto expectedHandle = symbolic::ExprFactoryScope::current().importExpr(*expected);
        mm.write(makeVariableAddr(3), expectedHandle);

        std::vector<const symbolic::SymbolicExpr *> flatValues;
        for (auto &&[addr, value] : mm.flat()) {
            if (*value == *expected)
                flatValues.push_back(value.get());
        }

        ASSERT_EQ(flatValues.size(), 3u);
        EXPECT_EQ(flatValues[0], flatValues[1]);
        EXPECT_EQ(flatValues[0], flatValues[2]);

        auto readBack = mm.read(makeVariableAddr(1));
        ASSERT_TRUE(readBack);
        EXPECT_EQ(*readBack.value(), *expected);
        EXPECT_NE(readBack.value().get().get(), flatValues[0]);

        auto readBackHandle = mm.readHandle(makeVariableAddr(1));
        ASSERT_TRUE(readBackHandle);
        EXPECT_EQ(readBackHandle->get().get(), flatValues[0]);
    }

    TEST_F(MemoryModelTest, AddrHandleReadWriteUsesInternedAddressAndValue) {
        auto &factory = symbolic::ExprFactoryScope::current();
        MemoryModel mm;

        auto legacyAddr = makeRangeAddr(1, makeLiteralExpr(3).into_underlying(), nullptr);
        auto addr       = factory.importAddress(legacyAddr);
        auto value = factory.literal(int64_t{42});

        mm.write(addr, value);
        EXPECT_TRUE(mm.contains(addr));

        auto readBack = mm.readHandle(addr);
        ASSERT_TRUE(readBack);
        EXPECT_EQ(readBack->get().get(), value.get().get());
    }

    TEST_F(MemoryModelTest, Flat_Yields_All_Three_Categories) {
        auto &factory = symbolic::ExprFactoryScope::current();
        MemoryModel mm;

        // noOffset
        auto baseA  = makeVariableAddr(1);
        auto eA     = makeSymbolValue(1);
        auto saveEA = cloneExpr(*eA);
        auto addrAHandle = factory.importAddress(baseA);
        mm.write(baseA, std::move(eA));

        // constantRange
        auto rangeB = makeRangeAddr(2, /*off=*/makeLiteralExpr(4).into_underlying(),
                                    /*len=*/makeLiteralExpr(2).into_underlying());
        auto eB     = makeSymbolValue(2);
        auto saveEB = cloneExpr(*eB);
        auto addrBHandle = factory.importAddress(rangeB);
        mm.write(rangeB, std::move(eB));

        // symbolicRange
        auto rangeC = makeRangeAddr(3, /*off=*/makeSymbolValue(3), nullptr);
        auto eC     = makeSymbolValue(3);
        auto saveEC = cloneExpr(*eC);
        auto addrCHandle = factory.importAddress(rangeC);
        mm.write(rangeC, std::move(eC));

        bool fA = false, fB = false, fC = false;
        for (auto &&[addr, value] : mm.flat()) {
            if (*value == *saveEA) {
                fA = true;
                EXPECT_EQ(&addr.get(), addrAHandle.get().get());
            } else if (*value == *saveEB) {
                fB = true;
                EXPECT_EQ(&addr.get(), addrBHandle.get().get());
            } else if (*value == *saveEC) {
                fC = true;
                EXPECT_EQ(&addr.get(), addrCHandle.get().get());
            }
        }

        EXPECT_TRUE(fA);
        EXPECT_TRUE(fB);
        EXPECT_TRUE(fC);
    }

    TEST_F(MemoryModelTest, ConstRange_CoverageAndOverride) {
        MemoryModel mm;
        const unsigned baseId = 10;

        // A: [0,10)
        auto aRange = makeRangeAddr(baseId, makeLiteralExpr(0U).into_underlying(),
                                    makeLiteralExpr(10U).into_underlying());
        auto eA     = makeSymbolValue(100);
        auto saveA  = cloneExpr(*eA);
        mm.write(aRange, std::move(eA));

        // B: [3,8)
        auto bRange = makeRangeAddr(baseId, makeLiteralExpr(3U).into_underlying(),
                                    makeLiteralExpr(5U).into_underlying());
        auto eB     = makeSymbolValue(200);
        auto saveB  = cloneExpr(*eB);
        mm.write(bRange, std::move(eB));

        // C: [1,3)
        auto cRange = makeRangeAddr(baseId, makeLiteralExpr(1U).into_underlying(),
                                    makeLiteralExpr(2U).into_underlying());
        auto eC     = makeSymbolValue(300);
        auto saveC  = cloneExpr(*eC);
        mm.write(cRange, std::move(eC));

        // D: [7,10)
        auto dRange = makeRangeAddr(baseId, makeLiteralExpr(7U).into_underlying(),
                                    makeLiteralExpr(3U).into_underlying());
        auto eD     = makeSymbolValue(400);
        auto saveD  = cloneExpr(*eD);
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
        auto r     = makeRangeAddr(baseId, makeLiteralExpr(5U).into_underlying(),
                                   makeLiteralExpr(4U).into_underlying());
        auto eX    = makeSymbolValue(500);
        auto saveX = cloneExpr(*eX);
        mm.write(r, std::move(eX));

        // Y: [5,9)
        auto eY    = makeSymbolValue(600);
        auto saveY = cloneExpr(*eY);
        mm.write(r, std::move(eY));

        for (uint64_t off = 5; off < 9; ++off) {
            EXPECT_TRUE(ExpectReadEqAt(mm, baseId, off, *saveY));
        }

        EXPECT_TRUE(ExpectReadNullAt(mm, baseId, 4));
        EXPECT_TRUE(ExpectReadNullAt(mm, baseId, 9));
    }

    TEST_F(MergeWithTest, UnionsVarAddrAndUnknownensMissingMemory) {
        auto var0 = getVarDecl(0);
        auto var1 = getVarDecl(1);

        auto addr0A = pathA->allocMemory(var0);
        auto addr1B = pathB->allocMemory(var1);

        auto val0     = makeSymbolValue(10);
        auto val1     = makeSymbolValue(20);

        pathA->updateMemory(*addr0A, std::move(val0));
        pathB->updateMemory(*addr1B, std::move(val1));

        pathA->mergeWith(*pathB);

        ASSERT_EQ(pathA->getVarAddr().size(), 2u);
        EXPECT_TRUE(pathA->getVarAddr().contains(var0));
        EXPECT_TRUE(pathA->getVarAddr().contains(var1));
        EXPECT_EQ(pathA->getVarAddr().at(var1).get().get(), addr1B.get().get());

        auto gotVal1 = pathA->getMemoryState().read(*pathA->getVarAddr().at(var0));
        ASSERT_TRUE(gotVal1);
        EXPECT_TRUE(gotVal1.value()->isUnknown());

        auto gotVal2 = pathA->getMemoryState().read(*pathA->getVarAddr().at(var1));
        ASSERT_TRUE(gotVal2);
        EXPECT_TRUE(gotVal2.value()->isUnknown());
    }

    TEST_F(MergeWithTest, ConflictingValuesBecomeUnknown) {
        auto var0   = getVarDecl(0);
        auto addr0A = pathA->allocMemory(var0);
        auto addr0B = pathB->allocMemory(var0);

        pathA->updateMemory(*addr0A, makeSymbolValue(1));
        pathB->updateMemory(*addr0B, makeSymbolValue(2));

        pathA->mergeWith(*pathB);

        auto val = pathA->getMemoryState().read(*addr0A);
        ASSERT_TRUE(val);
        EXPECT_NE(symbolic::dyn_cast<symbolic::UnknownExpr>(val->get().get()), nullptr);
    }

    TEST_F(MergeWithTest, PathConditionsIntersect) {
        auto condShared = makeLiteral(1);
        auto condAOnly  = makeLiteral(2);

        pathA->insertPathCondition(cloneExpr(*condShared));
        ASSERT_EQ(pathA->getPathConditions().size(), 1u);
        auto sharedHandle = *pathA->getPathConditions().begin();

        pathA->insertPathCondition(std::move(condAOnly));
        pathB->insertPathCondition(cloneExpr(*condShared));
        ASSERT_EQ(pathB->getPathConditions().size(), 1u);
        EXPECT_EQ(sharedHandle, *pathB->getPathConditions().begin());

        pathA->mergeWith(*pathB);

        ASSERT_EQ(pathA->getPathConditions().size(), 1u);
        const auto &onlyCond = *pathA->getPathConditions().begin();
        EXPECT_EQ(sharedHandle, onlyCond);
        auto lit             = symbolic::dyn_cast<symbolic::detail::LiteralExprNode>(onlyCond.get().get());
        ASSERT_NE(lit, nullptr);
        EXPECT_EQ(*lit, *condShared);
    }

    TEST_F(MergeWithTest, ReturnExprDiffersBecomesUnknown) {
        auto var0 = getVarDecl(0);
        pathA->allocMemory(var0);
        pathB->allocMemory(var0);

        pathA->setPathState(Path::PathState::Return);
        pathB->setPathState(Path::PathState::Return);

        pathA->setReturnExpr(makeLiteral(1));
        pathB->setReturnExpr(makeLiteral(2));

        pathA->mergeWith(*pathB);

        ASSERT_TRUE(pathA->getReturnExpr());
        EXPECT_NE(
            symbolic::dyn_cast<const symbolic::UnknownExpr>(pathA->getReturnExpr().value().get().get()),
            nullptr);
    }

    TEST_F(MergeWithTest, ReturnExprUsesInternedHandlesAcrossCloneAndMerge) {
        pathA->setPathState(Path::PathState::Return);
        pathB->setPathState(Path::PathState::Return);

        pathA->setReturnExpr(makeLiteral(7));
        ASSERT_TRUE(pathA->getReturnExpr());
        auto returnHandle = pathA->getReturnExpr().value();

        auto cloned = pathA->clone();
        ASSERT_TRUE(cloned->getReturnExpr());
        EXPECT_EQ(returnHandle, cloned->getReturnExpr().value());

        pathB->setReturnExpr(makeLiteral(7));
        ASSERT_TRUE(pathB->getReturnExpr());
        EXPECT_EQ(returnHandle, pathB->getReturnExpr().value());

        pathA->mergeWith(*pathB);
        ASSERT_TRUE(pathA->getReturnExpr());
        EXPECT_EQ(returnHandle, pathA->getReturnExpr().value());
    }

    TEST_F(MemoryModelTest, MergeConstantRanges_TouchingSameValue_ShouldCoalesce) {
        MemoryModel mm;
        const unsigned baseId = 21;

        // Two adjacent constant ranges with the same value
        // [0,3) value=V
        auto r1 = makeRangeAddr(baseId, makeLiteralExpr(0U).into_underlying(),
                                makeLiteralExpr(3U).into_underlying());
        auto v  = makeSymbolValue(1000);
        auto sv = cloneExpr(*v);
        mm.write(r1, std::move(v));

        // [3,5) value=V
        auto r2 = makeRangeAddr(baseId, makeLiteralExpr(3U).into_underlying(),
                                makeLiteralExpr(2U).into_underlying());
        auto v2 = makeSymbolValue(1000); // same value
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
        auto r1 = makeRangeAddr(baseId, makeLiteralExpr(0U).into_underlying(),
                                makeLiteralExpr(3U).into_underlying());
        auto v1 = makeSymbolValue(1111);
        auto s1 = cloneExpr(*v1);
        mm.write(r1, std::move(v1));

        // [3,5) value=V2 (different value)
        auto r2 = makeRangeAddr(baseId, makeLiteralExpr(3U).into_underlying(),
                                makeLiteralExpr(2U).into_underlying());
        auto v2 = makeSymbolValue(2222);
        auto s2 = cloneExpr(*v2);
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
        auto r1 = makeRangeAddr(baseId, makeLiteralExpr(0U).into_underlying(),
                                makeLiteralExpr(2U).into_underlying());
        auto r2 = makeRangeAddr(baseId, makeLiteralExpr(2U).into_underlying(),
                                makeLiteralExpr(3U).into_underlying());
        auto r3 = makeRangeAddr(baseId, makeLiteralExpr(5U).into_underlying(),
                                makeLiteralExpr(2U).into_underlying());

        auto v = makeSymbolValue(3333);
        auto s = cloneExpr(*v);
        mm.write(r1, std::move(v));
        mm.write(r2, makeSymbolValue(3333));
        mm.write(r3, makeSymbolValue(3333));

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
        auto r1 = makeRangeAddr(baseId, makeLiteralExpr(0U).into_underlying(),
                                makeLiteralExpr(2U).into_underlying());
        auto r2 = makeRangeAddr(baseId, makeLiteralExpr(2U).into_underlying(),
                                makeLiteralExpr(3U).into_underlying());
        auto r3 = makeRangeAddr(baseId, makeLiteralExpr(5U).into_underlying(),
                                makeLiteralExpr(2U).into_underlying());

        auto v = makeSymbolValue(4444);
        auto s = cloneExpr(*v);
        mm.write(r1, std::move(v));
        mm.write(r2, makeSymbolValue(5555)); // different
        mm.write(r3, makeSymbolValue(4444)); // same as r1

        mm.mergeConstantRanges();

        // Behavioral: 0..1 = 4444, 2..4 = 5555, 5..6 = 4444
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 0, *s));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 1, *s));

        auto w = makeSymbolValue(5555);
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
            return symbolic::makeBinaryExpr(std::move(a), symbolic::BinaryOpExpr::Operator::Add,
                                            std::move(b))
                .into_underlying();
        }
    } // namespace

    TEST_F(MemoryModelTest, MergeSymbolicRanges_ThreeSinglesChainIntoLen3) {
        MemoryModel mm;
        const unsigned baseId = 31;

        // X, X+1, X+2 each represents a single address (non-range → [off, off+1))
        auto X  = makeSymbolValue(901); // symbolic SymbolValue expression (example)
        auto X1 = makeAdd(cloneExpr(*X), makeLiteralExpr(1U));
        auto X2 = makeAdd(cloneExpr(*X), makeLiteralExpr(2U));

        auto a0 = makeRangeAddr(baseId, /*off=*/std::move(X), /*len=*/nullptr);  // single @ X
        auto a1 = makeRangeAddr(baseId, /*off=*/std::move(X1), /*len=*/nullptr); // single @ X+1
        auto a2 = makeRangeAddr(baseId, /*off=*/std::move(X2), /*len=*/nullptr); // single @ X+2

        // Same value
        auto v  = makeSymbolValue(7777);
        auto sv = cloneExpr(*v);
        mm.write(a0, std::move(v));
        mm.write(a1, makeSymbolValue(7777));
        mm.write(a2, makeSymbolValue(7777));

        // Trigger symbolic-range merge (hash-based chaining)
        mm.mergeSymbolicRanges();

        EXPECT_EQ(mm.sizeWithoutFields(), 1);
    }

    TEST_F(MemoryModelTest, MergeSymbolicRanges_SameValueButNonContiguous_ShouldNotMerge) {
        MemoryModel mm;
        const unsigned baseId = 32;

        auto X  = makeSymbolValue(902);
        auto X2 = makeAdd(cloneExpr(*X), makeLiteralExpr(2U));

        auto a0 = makeRangeAddr(baseId, std::move(X), nullptr);  // single @ X
        auto a2 = makeRangeAddr(baseId, std::move(X2), nullptr); // single @ X+2

        auto v  = makeSymbolValue(8888);
        auto sv = cloneExpr(*v);
        mm.write(a0, std::move(v));
        mm.write(a2, makeSymbolValue(8888)); // same value but with a gap of 1

        mm.mergeSymbolicRanges();

        // Two ranges with the same value but non-contiguous → must not merge (expect 2 entries)
        EXPECT_EQ(mm.sizeWithoutFields(), 2);
    }

    TEST_F(MemoryModelTest, MergeSymbolicRanges_ContiguousButDifferentValue_ShouldNotMerge) {
        MemoryModel mm;
        const unsigned baseId = 33;

        auto X  = makeSymbolValue(903);
        auto X1 = makeAdd(cloneExpr(*X), makeLiteralExpr(1U));

        auto a0 = makeRangeAddr(baseId, std::move(X), nullptr);  // single @ X
        auto a1 = makeRangeAddr(baseId, std::move(X1), nullptr); // single @ X+1

        auto v1 = makeSymbolValue(10001);
        auto v2 = makeSymbolValue(10002);
        auto s1 = cloneExpr(*v1);
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
        auto XA  = makeSymbolValue(910);
        auto X1A = makeAdd(cloneExpr(*XA), makeLiteralExpr(1U));
        auto a0A = makeRangeAddr(baseA, std::move(XA), nullptr);
        auto a1A = makeRangeAddr(baseA, std::move(X1A), nullptr);

        auto XB  = makeSymbolValue(910); // same construction but different base
        auto X1B = makeAdd(cloneExpr(*XB), makeLiteralExpr(1U));
        auto a0B = makeRangeAddr(baseB, std::move(XB), nullptr);
        auto a1B = makeRangeAddr(baseB, std::move(X1B), nullptr);

        auto vA  = makeSymbolValue(1212);
        auto svA = cloneExpr(*vA);
        auto vB  = makeSymbolValue(1212);
        auto svB = cloneExpr(*vB);

        mm.write(a0A, std::move(vA));
        mm.write(a1A, makeSymbolValue(1212));
        mm.write(a0B, std::move(vB));
        mm.write(a1B, makeSymbolValue(1212));

        mm.mergeSymbolicRanges();

        // Each base should merge within itself; no cross-base merge
        EXPECT_EQ(mm.sizeWithoutFields(), 2);
        size_t cntA = 0, cntB = 0;
        for (auto &&[addr, value] : mm.flat()) {
            if (*value != *svA && *value != *svB)
                continue;
            auto symbolAddr = symbolic::dyn_cast<symbolic::SymbolAddress>(&addr.get());
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
        auto X  = makeSymbolValue(904);
        auto a0 = makeRangeAddr(baseId, std::move(X), nullptr); // single @ X

        // Successor: [X+1, X+1 + 3) → len = 3
        auto X1 = makeAdd(makeSymbolValue(904), makeLiteralExpr(1U));
        auto a1 = makeRangeAddr(baseId, std::move(X1), makeLiteralExpr(3U).into_underlying());

        // Same value
        auto v  = makeSymbolValue(1313);
        auto sv = cloneExpr(*v);
        mm.write(a0, std::move(v));
        mm.write(a1, makeSymbolValue(1313));

        mm.mergeSymbolicRanges();

        // Approximate check: there should be exactly one entry (start at X, total length = 1 + 3 = 4)
        EXPECT_EQ(mm.sizeWithoutFields(), 1);
        for (auto &&[addr, value] : mm.flat()) {
            auto symbolAddr = symbolic::dyn_cast<symbolic::SymbolAddress>(&addr.get());
            ASSERT_NE(symbolAddr, nullptr);
            if (symbolAddr->getBaseInfo() == a0.getBaseInfo() && *value == *sv) {
                // If length is accessible and constant, also assert == 4
                if (auto &len = symbolAddr->getLength()) {
                    if (auto c = len.value()->tryEvalAsConstant()) {
                        EXPECT_EQ(c.value(), 4);
                        return;
                    }
                    FAIL() << len.value()->dump();
                }
            }
            FAIL() << symbolAddr->dump();
        }
    }
    TEST_F(MemoryModelTest, ConstRange_RangeIndexSubedCorrectly) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        MemoryModel mm(factory);
        const unsigned baseId = 10;

        // A: [0,10) -> i
        auto aRange = makeRangeAddr(baseId, makeLiteralExpr(0U).into_underlying(),
                                    makeLiteralExpr(10U).into_underlying());
        mm.write(aRange, makeRangeIndexExpr("i").into_underlying());

        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 0, symbolic::detail::LiteralExprNode{uint64_t{0}}));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 1, symbolic::detail::LiteralExprNode{uint64_t{1}}));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 2, symbolic::detail::LiteralExprNode{uint64_t{2}}));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 3, symbolic::detail::LiteralExprNode{uint64_t{3}}));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 4, symbolic::detail::LiteralExprNode{uint64_t{4}}));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 5, symbolic::detail::LiteralExprNode{uint64_t{5}}));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 6, symbolic::detail::LiteralExprNode{uint64_t{6}}));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 7, symbolic::detail::LiteralExprNode{uint64_t{7}}));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 8, symbolic::detail::LiteralExprNode{uint64_t{8}}));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 9, symbolic::detail::LiteralExprNode{uint64_t{9}}));

        EXPECT_TRUE(ExpectReadNullAt(mm, baseId, 10));
    }

    TEST_F(MemoryModelTest, SymbolicRange_RangeWithLength1IsNotARange) {
        MemoryModel mm;
        const unsigned baseId = 10;

        auto X = makeSymbolValue(904);
        // A: [X,X+1) -> v
        auto aRange = makeRangeAddr(baseId, cloneExpr(*X).into_underlying(),
                                    makeLiteralExpr(1U).into_underlying());
        auto v      = makeSymbolValue(1313);

        mm.write(aRange, cloneExpr(*v));

        // B: an addr with offset X
        auto bAddr = makeRangeAddr(baseId, cloneExpr(*X).into_underlying(), nullptr);

        // read(A) == read(B) == v
        auto resA = mm.read(aRange);
        ASSERT_TRUE(resA);
        EXPECT_EQ(*resA.value(), *v);
        auto resB = mm.read(bAddr);
        ASSERT_TRUE(resB);
        EXPECT_EQ(*resB.value(), *v);
    }

} // namespace acslg::test::unit::analyzer
