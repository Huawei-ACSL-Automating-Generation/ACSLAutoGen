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

    template <class ExprPtr> symbolic::ExprHandle internForTest(const ExprPtr &expr) {
        return symbolic::ExprFactoryScope::current().importExpr(*expr);
    }

    symbolic::ExprHandle makeLiteralHandle(uint64_t value) {
        return symbolic::ExprFactoryScope::current().literal(value);
    }

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

            auto makeLiteral(int v) {
                return acslContext.getExprFactory().literal(static_cast<uint64_t>(v));
            }
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

    TEST(ProgramStateTest, ReturnEvaluationStoresFactoryHandle) {
        auto postState = execOnFirstFunc(R"c(
            int func(void) {
                return 42;
            }
        )c");
        ASSERT_EQ(postState->getPaths().size(), 1u);
        const auto &returnExpr = postState->getPaths().front()->getReturnExpr();
        ASSERT_TRUE(returnExpr.has_value());

        auto expected = postState->getExprFactory().literal(42);
        EXPECT_EQ(returnExpr->get().get(), expected.get().get());
    }

    TEST(ProgramStateTest, ScopeExitReusesSurvivingPathConditionHandle) {
        ASTExtractor extractor(R"c(
            void func(int keep) {
                int local;
            }
        )c");
        auto *func     = extractor.findFirstDecl<FunctionDecl>();
        auto *declStmt = extractor.findFirstStmt<DeclStmt>();
        ASSERT_NE(func, nullptr);
        ASSERT_NE(declStmt, nullptr);
        ASSERT_EQ(func->getNumParams(), 1u);
        auto *local = dyn_cast<VarDecl>(declStmt->getSingleDecl());
        ASSERT_NE(local, nullptr);

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        ProgramState state(std::make_unique<ACSLFunction>(func), context);
        state.init();
        ASSERT_EQ(state.getPaths().size(), 1u);

        auto &path = *state.getPaths().front();
        path.allocMemory(local, true);
        auto keepValue  = path.getVarStateHandle(func->getParamDecl(0));
        auto localValue = path.getVarStateHandle(local);
        symbolic::LiteralExpr zero{context.getExprFactory(), 0};
        auto keepCond = symbolic::Expr{context.getExprFactory(), keepValue}.greaterThan(zero);
        auto localCond = symbolic::Expr{context.getExprFactory(), localValue}.greaterThan(zero);
        path.insertPathCondition(keepCond.logicalAnd(localCond).handle());

        state.step(func->getBody());

        ASSERT_EQ(path.getPathConditions().size(), 1u);
        EXPECT_EQ(path.getPathConditions().begin()->get().get(), keepCond.handle().get().get());
        EXPECT_FALSE(path.getVarAddr().contains(local));
    }

    TEST(InvariantFormulaTest, EqualityNegationBuildsInternedHandleBranches) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto lhs = factory.literal(int64_t{10});
        auto rhs = factory.literal(int64_t{20});
        auto equality = factory.binary(lhs, symbolic::BinaryOpExpr::Operator::Equal, rhs);

        Formulas formulas{equality};
        auto branches = ::acslg::analyzer::details::negateFormulas(formulas);

        ASSERT_EQ(branches.size(), 2u);
        ASSERT_EQ(branches[0].size(), 1u);
        ASSERT_EQ(branches[1].size(), 1u);
        auto expectedGreater = factory.binary(
            lhs, symbolic::BinaryOpExpr::Operator::GreaterEqual,
            factory.binary(rhs, symbolic::BinaryOpExpr::Operator::Add,
                           factory.literal(int64_t{1})));
        auto expectedLess = factory.binary(
            lhs, symbolic::BinaryOpExpr::Operator::LessEqual,
            factory.binary(rhs, symbolic::BinaryOpExpr::Operator::Subtract,
                           factory.literal(int64_t{1})));
        EXPECT_EQ(branches[0][0].get().get(), expectedGreater.get().get());
        EXPECT_EQ(branches[1][0].get().get(), expectedLess.get().get());
    }

    TEST(ProgramStateTest, CompoundAssignmentStoresInternedOperation) {
        ASTExtractor extractor(R"c(
            int func(int x, int y) {
                x += y;
                return x;
            }
        )c");
        auto *func   = extractor.findFirstDecl<FunctionDecl>();
        auto *assign = extractor.findFirstStmt<CompoundAssignOperator>();
        ASSERT_NE(func, nullptr);
        ASSERT_NE(assign, nullptr);
        ASSERT_EQ(func->getNumParams(), 2u);

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        ProgramState state(std::make_unique<ACSLFunction>(func), context);
        state.init();
        ASSERT_EQ(state.getPaths().size(), 1u);

        auto x = state.getPaths().front()->getVarStateHandle(func->getParamDecl(0));
        auto y = state.getPaths().front()->getVarStateHandle(func->getParamDecl(1));
        auto expected = symbolic::Expr{context.getExprFactory(), x}
                            .binary(symbolic::BinaryOpExpr::Operator::Add,
                                    symbolic::Expr{context.getExprFactory(), y})
                            .handle();

        state.step(assign);

        ASSERT_EQ(state.getPaths().size(), 1u);
        auto actual = state.getPaths().front()->getVarStateHandle(func->getParamDecl(0));
        EXPECT_EQ(actual.get().get(), expected.get().get());
    }

    TEST(ProgramStateTest, DeclarationInitializerStoresFactoryHandle) {
        auto postState = execOnFirstFunc(R"c(
            int func(void) {
                int value = 42;
                return value;
            }
        )c");
        ASSERT_EQ(postState->getPaths().size(), 1u);

        const auto &path = *postState->getPaths().front();
        ASSERT_EQ(path.getVarAddr().size(), 1u);
        auto value = path.getMemoryState().read(path.getVarAddr().begin()->second);
        ASSERT_TRUE(value.has_value());
        auto expected = postState->getExprFactory().literal(42);
        EXPECT_EQ(value->get().get(), expected.get().get());
        ASSERT_TRUE(path.getReturnExpr().has_value());
        EXPECT_EQ(path.getReturnExpr()->get().get(), expected.get().get());
    }

    TEST(ProgramStateTest, InlineCallPreservesArgumentHandle) {
        ASTExtractor extractor(R"c(
            int identity(int value) {
                return value;
            }

            int func(int input) {
                return identity(input);
            }
        )c");
        auto *func = extractor.findFunc("func");
        ASSERT_NE(func, nullptr);
        ASSERT_EQ(func->getNameAsString(), "func");
        ASSERT_EQ(func->getNumParams(), 1u);

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        ProgramState state(std::make_unique<ACSLFunction>(func), context);
        state.init();
        ASSERT_EQ(state.getPaths().size(), 1u);
        auto input = state.getPaths().front()->getVarStateHandle(func->getParamDecl(0));

        state.step(func->getBody());

        ASSERT_EQ(state.getPaths().size(), 1u);
        const auto &result = state.getPaths().front()->getReturnExpr();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->get().get(), input.get().get());
    }

    TEST(ProgramStateTest, SwitchCasesStoreFactoryHandles) {
        ASTExtractor extractor(R"c(
            int func(int value) {
                switch (value) {
                    case 1: return 10;
                    case 2: return 20;
                    default: return 30;
                }
            }
        )c");
        auto *func = extractor.findFirstDecl<FunctionDecl>();
        ASSERT_NE(func, nullptr);

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        ProgramState state(std::make_unique<ACSLFunction>(func), context);
        state.init();
        state.step(func->getBody());

        std::unordered_set<const symbolic::SymbolicExpr *> returns;
        for (const auto &path : state.getPaths()) {
            ASSERT_TRUE(path->getReturnExpr().has_value());
            returns.emplace(path->getReturnExpr()->get().get());
            for (auto cond : path->getPathConditions()) {
                auto interned = context.getExprFactory().importExpr(*cond);
                EXPECT_EQ(cond.get().get(), interned.get().get());
            }
        }

        EXPECT_TRUE(returns.contains(context.getExprFactory().literal(10).get().get()));
        EXPECT_TRUE(returns.contains(context.getExprFactory().literal(20).get().get()));
        EXPECT_TRUE(returns.contains(context.getExprFactory().literal(30).get().get()));
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
        auto stored = path.getMemoryState().read(first);
        ASSERT_TRUE(stored.has_value());

        EXPECT_EQ(first.get().get(), second.get().get());
        EXPECT_EQ(cloned->getVarAddr().at(var).get().get(), first.get().get());
        EXPECT_EQ(first.get().get(), context.getExprFactory().variableAddress(var).get().get());
        EXPECT_EQ(path.getVarStateHandle(var).get().get(), stored->get().get());
        EXPECT_EQ(cloned->getVarStateHandle(var).get().get(), stored->get().get());
    }

    TEST(PathTest, EvalExprPreservesInternedConditionalResults) {
        ASTExtractor extractor(R"c(
            int func(int value) {
                return value ? 1 : 2;
            }
        )c");
        auto *func = extractor.findFirstDecl<FunctionDecl>();
        auto *var  = extractor.findFirstDecl<ParmVarDecl>();
        auto *cond = extractor.findFirstStmt<ConditionalOperator>();
        ASSERT_NE(func, nullptr);
        ASSERT_NE(var, nullptr);
        ASSERT_NE(cond, nullptr);

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        auto point = symbolic::SourcePoint::fromFuncDecl(
            func, extractor.getSourceManager(), extractor.getLangOptions());
        Path path(context, point);
        path.allocMemory(var, true);

        auto result = path.evalExpr(cond);
        ASSERT_EQ(result.first.size(), 1u);
        ASSERT_EQ(result.second.size(), 2u);

        auto one = context.getExprFactory().literal(1);
        auto two = context.getExprFactory().literal(2);
        std::unordered_set<const symbolic::SymbolicExpr *> nodes;
        for (auto value : result.second)
            nodes.emplace(value.get().get());
        EXPECT_TRUE(nodes.contains(one.get().get()));
        EXPECT_TRUE(nodes.contains(two.get().get()));
    }

    TEST(PathTest, PostIncrementReturnsOldHandleAndStoresNewHandle) {
        ASTExtractor extractor(R"c(
            int func(int value) {
                value++;
                return value;
            }
        )c");
        auto *func = extractor.findFirstDecl<FunctionDecl>();
        auto *var  = extractor.findFirstDecl<ParmVarDecl>();
        auto *inc  = extractor.findFirstStmt<UnaryOperator>();
        ASSERT_NE(func, nullptr);
        ASSERT_NE(var, nullptr);
        ASSERT_NE(inc, nullptr);
        ASSERT_TRUE(inc->isPostfix());

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        auto point = symbolic::SourcePoint::fromFuncDecl(
            func, extractor.getSourceManager(), extractor.getLangOptions());
        Path path(context, point);
        path.allocMemory(var, true);

        auto oldValue = path.getVarStateHandle(var);
        auto expectedNew = symbolic::Expr{context.getExprFactory(), oldValue}
                               .binary(symbolic::BinaryOpExpr::Operator::Add,
                                       symbolic::LiteralExpr{context.getExprFactory(), 1})
                               .handle();
        auto result = path.evalExpr(inc);

        ASSERT_TRUE(result.first.empty());
        ASSERT_EQ(result.second.size(), 1u);
        EXPECT_EQ(result.second[0].get().get(), oldValue.get().get());
        EXPECT_EQ(path.getVarStateHandle(var).get().get(), expectedNew.get().get());
    }

    TEST_F(MemoryModelTest, ReadAfterWrite_VarAddr) {
        MemoryModel mm;

        auto addr     = makeVariableAddr(1);
        auto expr     = makeSymbolValue(42);
        auto saveExpr = internForTest(expr);
        mm.write(addr, saveExpr);

        ASSERT_NE(expr.get(), nullptr);
        auto got = mm.read(addr);
        ASSERT_NE(got, nullopt);
        EXPECT_EQ(*got.value(), *saveExpr);
    }

    TEST_F(MemoryModelTest, EqualStoredValuesShareInternedHandle) {
        MemoryModel mm;

        auto expected = makeSymbolValue(42);
        auto equalValue1 = makeSymbolValue(42);
        auto equalValue2 = makeSymbolValue(42);
        mm.write(makeVariableAddr(1), internForTest(equalValue1));
        mm.write(makeVariableAddr(2), internForTest(equalValue2));
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
        EXPECT_EQ(readBack.value().get().get(), flatValues[0]);
    }

    TEST_F(MemoryModelTest, AddrHandleReadWriteUsesInternedAddressAndValue) {
        auto &factory = symbolic::ExprFactoryScope::current();
        MemoryModel mm;

        auto legacyAddr = makeRangeAddr(1, makeLiteralHandle(3), std::nullopt);
        auto addr       = factory.importAddress(legacyAddr);
        auto value = factory.literal(int64_t{42});

        mm.write(addr, value);
        EXPECT_TRUE(mm.contains(addr));

        auto readBack = mm.read(addr);
        ASSERT_TRUE(readBack);
        EXPECT_EQ(readBack->get().get(), value.get().get());
    }

    TEST_F(MemoryModelTest, Flat_Yields_All_Three_Categories) {
        auto &factory = symbolic::ExprFactoryScope::current();
        MemoryModel mm;

        // noOffset
        auto baseA  = makeVariableAddr(1);
        auto eA     = makeSymbolValue(1);
        auto saveEA = internForTest(eA);
        auto addrAHandle = baseA;
        mm.write(baseA, internForTest(eA));

        // constantRange
        auto rangeB = makeRangeAddr(2, /*off=*/makeLiteralHandle(4),
                                    /*len=*/makeLiteralHandle(2));
        auto eB     = makeSymbolValue(2);
        auto saveEB = internForTest(eB);
        auto addrBHandle = factory.importAddress(rangeB);
        mm.write(rangeB, internForTest(eB));

        // symbolicRange
        auto rangeC =
            makeRangeAddr(3, /*off=*/internForTest(makeSymbolValue(3)), std::nullopt);
        auto eC     = makeSymbolValue(3);
        auto saveEC = internForTest(eC);
        auto addrCHandle = factory.importAddress(rangeC);
        mm.write(rangeC, internForTest(eC));

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
        auto aRange = makeRangeAddr(baseId, makeLiteralHandle(0U), makeLiteralHandle(10U));
        auto eA     = makeSymbolValue(100);
        auto saveA  = internForTest(eA);
        mm.write(aRange, saveA);

        // B: [3,8)
        auto bRange = makeRangeAddr(baseId, makeLiteralHandle(3U), makeLiteralHandle(5U));
        auto eB     = makeSymbolValue(200);
        auto saveB  = internForTest(eB);
        mm.write(bRange, saveB);

        // C: [1,3)
        auto cRange = makeRangeAddr(baseId, makeLiteralHandle(1U), makeLiteralHandle(2U));
        auto eC     = makeSymbolValue(300);
        auto saveC  = internForTest(eC);
        mm.write(cRange, saveC);

        // D: [7,10)
        auto dRange = makeRangeAddr(baseId, makeLiteralHandle(7U), makeLiteralHandle(3U));
        auto eD     = makeSymbolValue(400);
        auto saveD  = internForTest(eD);
        mm.write(dRange, saveD);

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
        auto r = makeRangeAddr(baseId, makeLiteralHandle(5U), makeLiteralHandle(4U));
        auto eX    = makeSymbolValue(500);
        auto saveX = internForTest(eX);
        mm.write(r, saveX);

        // Y: [5,9)
        auto eY    = makeSymbolValue(600);
        auto saveY = internForTest(eY);
        mm.write(r, saveY);

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

        pathA->updateMemory(*addr0A, acslContext.getExprFactory().importExpr(*val0));
        pathB->updateMemory(*addr1B, acslContext.getExprFactory().importExpr(*val1));

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

        auto valueA = makeSymbolValue(1);
        auto valueB = makeSymbolValue(2);
        pathA->updateMemory(*addr0A, acslContext.getExprFactory().importExpr(*valueA));
        pathB->updateMemory(*addr0B, acslContext.getExprFactory().importExpr(*valueB));

        pathA->mergeWith(*pathB);

        auto val = pathA->getMemoryState().read(*addr0A);
        ASSERT_TRUE(val);
        EXPECT_NE(symbolic::dyn_cast<symbolic::UnknownExpr>(val->get().get()), nullptr);
    }

    TEST_F(MergeWithTest, PathConditionsIntersect) {
        auto condShared = makeLiteral(1);
        auto condAOnly  = makeLiteral(2);

        pathA->insertPathCondition(acslContext.getExprFactory().importExpr(*condShared));
        ASSERT_EQ(pathA->getPathConditions().size(), 1u);
        auto sharedHandle = *pathA->getPathConditions().begin();

        pathA->insertPathCondition(acslContext.getExprFactory().importExpr(*condAOnly));
        pathB->insertPathCondition(acslContext.getExprFactory().importExpr(*condShared));
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

        auto returnA = makeLiteral(1);
        auto returnB = makeLiteral(2);
        pathA->setReturnExpr(acslContext.getExprFactory().importExpr(*returnA));
        pathB->setReturnExpr(acslContext.getExprFactory().importExpr(*returnB));

        pathA->mergeWith(*pathB);

        ASSERT_TRUE(pathA->getReturnExpr());
        EXPECT_NE(
            symbolic::dyn_cast<const symbolic::UnknownExpr>(pathA->getReturnExpr().value().get().get()),
            nullptr);
    }

    TEST_F(MergeWithTest, ReturnExprUsesInternedHandlesAcrossCloneAndMerge) {
        pathA->setPathState(Path::PathState::Return);
        pathB->setPathState(Path::PathState::Return);

        auto returnA = makeLiteral(7);
        pathA->setReturnExpr(acslContext.getExprFactory().importExpr(*returnA));
        ASSERT_TRUE(pathA->getReturnExpr());
        auto returnHandle = pathA->getReturnExpr().value();

        auto cloned = pathA->clone();
        ASSERT_TRUE(cloned->getReturnExpr());
        EXPECT_EQ(returnHandle, cloned->getReturnExpr().value());

        auto returnB = makeLiteral(7);
        pathB->setReturnExpr(acslContext.getExprFactory().importExpr(*returnB));
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
        auto r1 = makeRangeAddr(baseId, makeLiteralHandle(0U), makeLiteralHandle(3U));
        auto v  = makeSymbolValue(1000);
        auto sv = internForTest(v);
        mm.write(r1, sv);

        // [3,5) value=V
        auto r2 = makeRangeAddr(baseId, makeLiteralHandle(3U), makeLiteralHandle(2U));
        auto v2 = makeSymbolValue(1000); // same value
        mm.write(r2, internForTest(v2));

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
        auto r1 = makeRangeAddr(baseId, makeLiteralHandle(0U), makeLiteralHandle(3U));
        auto v1 = makeSymbolValue(1111);
        auto s1 = internForTest(v1);
        mm.write(r1, s1);

        // [3,5) value=V2 (different value)
        auto r2 = makeRangeAddr(baseId, makeLiteralHandle(3U), makeLiteralHandle(2U));
        auto v2 = makeSymbolValue(2222);
        auto s2 = internForTest(v2);
        mm.write(r2, s2);

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
        auto r1 = makeRangeAddr(baseId, makeLiteralHandle(0U), makeLiteralHandle(2U));
        auto r2 = makeRangeAddr(baseId, makeLiteralHandle(2U), makeLiteralHandle(3U));
        auto r3 = makeRangeAddr(baseId, makeLiteralHandle(5U), makeLiteralHandle(2U));

        auto v = makeSymbolValue(3333);
        auto s = internForTest(v);
        mm.write(r1, s);
        mm.write(r2, internForTest(makeSymbolValue(3333)));
        mm.write(r3, internForTest(makeSymbolValue(3333)));

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
        auto r1 = makeRangeAddr(baseId, makeLiteralHandle(0U), makeLiteralHandle(2U));
        auto r2 = makeRangeAddr(baseId, makeLiteralHandle(2U), makeLiteralHandle(3U));
        auto r3 = makeRangeAddr(baseId, makeLiteralHandle(5U), makeLiteralHandle(2U));

        auto v = makeSymbolValue(4444);
        auto s = internForTest(v);
        mm.write(r1, s);
        mm.write(r2, internForTest(makeSymbolValue(5555))); // different
        mm.write(r3, internForTest(makeSymbolValue(4444))); // same as r1

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
        auto makeAdd(symbolic::ExprHandle a, symbolic::ExprHandle b) {
            auto &factory = symbolic::ExprFactoryScope::current();
            return factory.binary(a, symbolic::BinaryOpExpr::Operator::Add, b);
        }
    } // namespace

    TEST_F(MemoryModelTest, MergeSymbolicRanges_ThreeSinglesChainIntoLen3) {
        MemoryModel mm;
        const unsigned baseId = 31;

        // X, X+1, X+2 each represents a single address (non-range → [off, off+1))
        auto X = internForTest(makeSymbolValue(901));
        auto &factory = symbolic::ExprFactoryScope::current();
        auto X1 = makeAdd(X, factory.literal(1U));
        auto X2 = makeAdd(X, factory.literal(2U));

        auto a0 = makeRangeAddr(baseId, X, std::nullopt);  // single @ X
        auto a1 = makeRangeAddr(baseId, X1, std::nullopt); // single @ X+1
        auto a2 = makeRangeAddr(baseId, X2, std::nullopt); // single @ X+2

        // Same value
        auto v = makeSymbolValue(7777);
        mm.write(a0, internForTest(v));
        mm.write(a1, internForTest(makeSymbolValue(7777)));
        mm.write(a2, internForTest(makeSymbolValue(7777)));

        // Trigger symbolic-range merge (hash-based chaining)
        mm.mergeSymbolicRanges();

        EXPECT_EQ(mm.sizeWithoutFields(), 1);
    }

    TEST_F(MemoryModelTest, MergeSymbolicRanges_SameValueButNonContiguous_ShouldNotMerge) {
        MemoryModel mm;
        const unsigned baseId = 32;

        auto X = internForTest(makeSymbolValue(902));
        auto &factory = symbolic::ExprFactoryScope::current();
        auto X2 = makeAdd(X, factory.literal(2U));

        auto a0 = makeRangeAddr(baseId, X, std::nullopt);  // single @ X
        auto a2 = makeRangeAddr(baseId, X2, std::nullopt); // single @ X+2

        auto v = makeSymbolValue(8888);
        mm.write(a0, internForTest(v));
        mm.write(a2, internForTest(makeSymbolValue(8888))); // same value but with a gap of 1

        mm.mergeSymbolicRanges();

        // Two ranges with the same value but non-contiguous → must not merge (expect 2 entries)
        EXPECT_EQ(mm.sizeWithoutFields(), 2);
    }

    TEST_F(MemoryModelTest, MergeSymbolicRanges_ContiguousButDifferentValue_ShouldNotMerge) {
        MemoryModel mm;
        const unsigned baseId = 33;

        auto X = internForTest(makeSymbolValue(903));
        auto &factory = symbolic::ExprFactoryScope::current();
        auto X1 = makeAdd(X, factory.literal(1U));

        auto a0 = makeRangeAddr(baseId, X, std::nullopt);  // single @ X
        auto a1 = makeRangeAddr(baseId, X1, std::nullopt); // single @ X+1

        auto v1 = makeSymbolValue(10001);
        auto v2 = makeSymbolValue(10002);
        auto s1 = internForTest(v1);
        mm.write(a0, internForTest(v1));
        mm.write(a1, internForTest(v2)); // different value

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
        auto XA = internForTest(makeSymbolValue(910));
        auto &factory = symbolic::ExprFactoryScope::current();
        auto X1A = makeAdd(XA, factory.literal(1U));
        auto a0A = makeRangeAddr(baseA, XA, std::nullopt);
        auto a1A = makeRangeAddr(baseA, X1A, std::nullopt);

        auto XB = internForTest(makeSymbolValue(910)); // same construction but different base
        auto X1B = makeAdd(XB, factory.literal(1U));
        auto a0B = makeRangeAddr(baseB, XB, std::nullopt);
        auto a1B = makeRangeAddr(baseB, X1B, std::nullopt);

        auto vA  = makeSymbolValue(1212);
        auto svA = internForTest(vA);
        auto vB  = makeSymbolValue(1212);
        auto svB = internForTest(vB);

        mm.write(a0A, internForTest(vA));
        mm.write(a1A, internForTest(makeSymbolValue(1212)));
        mm.write(a0B, internForTest(vB));
        mm.write(a1B, internForTest(makeSymbolValue(1212)));

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
        auto X = internForTest(makeSymbolValue(904));
        auto a0 = makeRangeAddr(baseId, X, std::nullopt); // single @ X

        // Successor: [X+1, X+1 + 3) → len = 3
        auto &factory = symbolic::ExprFactoryScope::current();
        auto X1 = makeAdd(X, factory.literal(1U));
        auto a1 = makeRangeAddr(baseId, X1, factory.literal(3U));

        // Same value
        auto v  = makeSymbolValue(1313);
        auto sv = internForTest(v);
        mm.write(a0, internForTest(v));
        mm.write(a1, internForTest(makeSymbolValue(1313)));

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
        auto aRange = makeRangeAddr(baseId, makeLiteralHandle(0U), makeLiteralHandle(10U));
        auto rangeIndex = symbolic::ExprFactoryScope::current().rangeIndex("i");
        mm.write(aRange, rangeIndex);

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

        auto X = internForTest(makeSymbolValue(904));
        auto &factory = symbolic::ExprFactoryScope::current();
        // A: [X,X+1) -> v
        auto aRange = makeRangeAddr(baseId, X, factory.literal(1U));
        auto v = internForTest(makeSymbolValue(1313));

        mm.write(aRange, v);

        // B: an addr with offset X
        auto bAddr = makeRangeAddr(baseId, X, std::nullopt);

        // read(A) == read(B) == v
        auto resA = mm.read(aRange);
        ASSERT_TRUE(resA);
        EXPECT_EQ(*resA.value(), *v);
        auto resB = mm.read(bAddr);
        ASSERT_TRUE(resB);
        EXPECT_EQ(*resB.value(), *v);
    }

} // namespace acslg::test::unit::analyzer
