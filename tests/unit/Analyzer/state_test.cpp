// tests/unit/SpecGenerator/state_test.cpp

#include <cstdint>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <llvm/Support/Casting.h>
#include "state.h"
#include "clang/AST/Decl.h"
#include "Symbolic/expr.h"
#include "Symbolic/aggregateExpr.h" // IWYU pragma: keep
#include "Symbolic/detail/facadeAccess.h"
#include "testHelper.h"

using namespace std;
using namespace clang;

namespace acslg::test::unit::analyzer {
    using namespace acslg::analyzer;
    using namespace utils;

    static symbolic::detail::ExprHandle handle(const symbolic::Expr &expression) {
        return symbolic::detail::FacadeAccess::exprHandle(expression);
    }

    static symbolic::detail::AddrHandle handle(const symbolic::Addr &address) {
        return symbolic::detail::FacadeAccess::addressHandle(address);
    }

    static symbolic::detail::AddrHandle handle(const symbolic::AddressBox &address) {
        return symbolic::detail::FacadeAccess::addressBoxHandle(address);
    }

    template <class ExprPtr> symbolic::detail::ExprHandle internForTest(const ExprPtr &expr) {
        return importExprHandle(symbolic::ExprFactoryScope::current(), expr);
    }

    symbolic::detail::ExprHandle makeLiteralHandle(uint64_t value) {
        return literalHandle(symbolic::ExprFactoryScope::current(), value);
    }

    namespace {
        template <typename Address>
        concept MemoryAddressReadable = requires(const MemoryModel &memory,
                                                 const Address &address) {
            memory.read(address);
            memory.contains(address);
        };

        template <typename Value>
        concept MemoryValueWritable = requires(MemoryModel &memory,
                                               const symbolic::Addr &address,
                                               const Value &value) {
            memory.write(address, value);
        };

        static_assert(MemoryAddressReadable<symbolic::Addr>);
        static_assert(!MemoryAddressReadable<symbolic::detail::AddrHandle>);
        static_assert(MemoryValueWritable<symbolic::Expr>);
        static_assert(!MemoryValueWritable<symbolic::detail::ExprHandle>);
        using FlatMemoryEntry =
            decltype(*std::declval<MemoryModel::flat_view::iterator>());
        static_assert(std::same_as<std::tuple_element_t<0, FlatMemoryEntry>, symbolic::Addr>);
        static_assert(std::same_as<std::tuple_element_t<1, FlatMemoryEntry>, symbolic::Expr>);

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
                return symbolic::LiteralExpr{acslContext.getExprFactory(),
                                             static_cast<uint64_t>(v)};
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
        auto result = simplifyForTest(symbolic::ExprFactoryScope::current(),
                                      getReturnExprOfFirstPath(*postState));
        FacadeExprForTest resultExpr{postState->getExprFactory(), result};
        EXPECT_EQ(symbolic::LiteralExpr{resultExpr}.value(), 12);

        size_t flatCount = 0;
        for (auto &&[addr, value] : postState->getPaths().front()->getMemoryState().flat()) {
            auto interned = importAddressHandle(postState->getExprFactory(), handle(addr));
            EXPECT_EQ(handle(addr), interned);
            auto readBack = postState->getPaths().front()->getMemoryState().read(addr);
            ASSERT_TRUE(readBack);
            EXPECT_EQ(&value.factory(), &postState->getExprFactory());
            EXPECT_EQ(value, *readBack);
            ++flatCount;
        }
        EXPECT_GE(flatCount, 2u);
    }

    TEST(ProgramStateTest, FlatMemoryRecursesThroughNestedStructureFacades) {
        ASTExtractor extractor(R"c(
            struct Inner {
                int value;
            };
            struct Outer {
                struct Inner inner;
                int tail;
            };

            int func(void) {
                struct Outer object;
                return 0;
            }
        )c");

        auto *func   = extractor.findFirstDecl<FunctionDecl>();
        auto *object = extractor.findFirstDecl<VarDecl>();
        ASSERT_NE(func, nullptr);
        ASSERT_NE(object, nullptr);

        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto point = symbolic::SourcePoint::fromFuncDecl(
            func, extractor.getSourceManager(), extractor.getLangOptions());
        auto objectAddress = symbolic::Addr::variable(object);
        auto *record = object->getType()->getAsRecordDecl()->getDefinition();
        auto objectValue = symbolic::detail::FacadeAccess::makeExpr(
            factory, structureHandle(factory, record, handle(objectAddress), point));
        MemoryModel memory(factory);
        memory.write(objectAddress, objectValue);

        size_t flatCount      = 0;
        size_t structureCount = 0;
        for (const auto &[address, value] : memory.flat()) {
            EXPECT_EQ(&address.factory(), &factory);
            EXPECT_EQ(handle(address), importAddressHandle(factory, handle(address)));
            EXPECT_EQ(&value.factory(), &factory);
            structureCount += value.isStructure();
            ++flatCount;
        }

        EXPECT_GE(flatCount, 4u);
        EXPECT_GE(structureCount, 2u);
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

        auto expected = literalHandle(postState->getExprFactory(), 42);
        EXPECT_EQ(handle(*returnExpr), expected);
    }

    TEST(ProgramStateTest, ScopeExitKeepsSurvivingPathConditionFacade) {
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
        auto keepValue  = path.getVarState(func->getParamDecl(0));
        auto localValue = path.getVarState(local);
        symbolic::LiteralExpr zero{context.getExprFactory(), 0};
        auto keepCond  = keepValue.greaterThan(zero);
        auto localCond = localValue.greaterThan(zero);
        path.insertPathCondition(keepCond.logicalAnd(localCond));

        state.step(func->getBody());

        ASSERT_EQ(path.getPathConditions().size(), 1u);
        const auto &surviving = *path.getPathConditions().begin();
        EXPECT_EQ(surviving, keepCond);
        EXPECT_EQ(&surviving.factory(), &context.getExprFactory());
        EXPECT_FALSE(path.getVarAddr().contains(local));
    }

    TEST(InvariantFormulaTest, EqualityNegationBuildsInternedFacadeBranches) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto lhs      = symbolic::LiteralExpr{factory, int64_t{10}};
        auto rhs      = symbolic::LiteralExpr{factory, int64_t{20}};
        auto equality = lhs.equalTo(rhs);

        Formulas formulas{equality};
        auto branches = ::acslg::analyzer::details::negateFormulas(formulas);

        ASSERT_EQ(branches.size(), 2u);
        ASSERT_EQ(branches[0].size(), 1u);
        ASSERT_EQ(branches[1].size(), 1u);
        auto one             = symbolic::LiteralExpr{factory, int64_t{1}};
        auto expectedGreater = lhs.greaterEqual(rhs + one);
        auto expectedLess    = lhs.lessEqual(rhs - one);
        EXPECT_EQ(branches[0][0], expectedGreater);
        EXPECT_EQ(branches[1][0], expectedLess);
    }

    TEST(InvariantFormulaTest, PreprocessingRebuildsIntegerBoundsWithFacades) {
        symbolic::ExprFactory factory;
        symbolic::ExprFactoryScope scope(factory);
        auto lhs = symbolic::LiteralExpr{factory, int64_t{10}};
        auto rhs = symbolic::LiteralExpr{factory, int64_t{20}};
        auto one = symbolic::LiteralExpr{factory, int64_t{1}};

        Formulas formulas{lhs.greaterThan(rhs).logicalAnd(!lhs.greaterEqual(rhs))};
        auto preprocessed = ::acslg::analyzer::details::preprocessConjConds(formulas);

        ASSERT_EQ(preprocessed.size(), 2u);
        EXPECT_EQ(preprocessed[0], lhs.greaterEqual(rhs + one));
        EXPECT_EQ(preprocessed[1], lhs.lessEqual(rhs - one));
        EXPECT_EQ(&preprocessed[0].factory(), &factory);
        EXPECT_EQ(&preprocessed[1].factory(), &factory);
    }

    TEST(InvariantFormulaTest, PostStateConditionsKeepContextFactory) {
        ASTExtractor extractor(R"c(
            void func(int value) {}
        )c");
        auto *func = extractor.findFirstDecl<FunctionDecl>();
        ASSERT_NE(func, nullptr);
        ASSERT_EQ(func->getNumParams(), 1u);

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        auto point = symbolic::SourcePoint::fromFuncDecl(func, extractor.getSourceManager(),
                                                         extractor.getLangOptions());
        Path path(context, point);
        path.allocMemory(func->getParamDecl(0), true);

        auto vm = VarManager::fromPath(path);
        ASSERT_EQ(vm.numVars, 2u);
        Parma_Polyhedra_Library::C_Polyhedron poly{vm.numVars, Parma_Polyhedra_Library::UNIVERSE};
        using Parma_Polyhedra_Library::Variable;
        poly.add_constraint(Variable(0) == Variable(1));
        poly.add_constraint(Variable(0) >= 0);

        auto [postMemory, postConditions] =
            ::acslg::analyzer::details::buildPostState(poly, path, vm);
        ASSERT_FALSE(postMemory.empty());
        ASSERT_FALSE(postConditions.empty());
        for (const auto &[address, value] : postMemory) {
            EXPECT_EQ(handle(address),
                      importAddressHandle(context.getExprFactory(), handle(address)));
            EXPECT_EQ(&value.factory(), &context.getExprFactory());
            EXPECT_EQ(handle(value), importExprHandle(context.getExprFactory(), handle(value)));
        }
        for (const auto &condition : postConditions) {
            EXPECT_EQ(&condition.factory(), &context.getExprFactory());
            EXPECT_EQ(handle(condition),
                      importExprHandle(context.getExprFactory(), handle(condition)));
        }
    }

    TEST(InvariantFormulaTest, PostStateResolutionUsesFacadeArithmetic) {
        ASTExtractor extractor(R"c(
            void func(int value) {}
        )c");
        auto *func = extractor.findFirstDecl<FunctionDecl>();
        ASSERT_NE(func, nullptr);

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        auto point = symbolic::SourcePoint::fromFuncDecl(func, extractor.getSourceManager(),
                                                         extractor.getLangOptions());
        Path path(context, point);
        path.allocMemory(func->getParamDecl(0), true);

        auto vm = VarManager::fromPath(path);
        ASSERT_EQ(vm.numVars, 2u);
        Parma_Polyhedra_Library::C_Polyhedron poly{vm.numVars, Parma_Polyhedra_Library::UNIVERSE};
        using Parma_Polyhedra_Library::Variable;
        poly.add_constraint(2 * Variable(0) == Variable(1) + 4);

        auto [postMemory, postConditions] =
            ::acslg::analyzer::details::buildPostState(poly, path, vm);
        auto address = symbolic::AddressBox{path.getVarAddr().at(func->getParamDecl(0))};
        auto value   = postMemory.find(address);
        ASSERT_NE(value, postMemory.end());

        auto &factory = context.getExprFactory();
        auto oldValue = path.getVarState(func->getParamDecl(0));
        auto expected = (symbolic::LiteralExpr{factory, int64_t{4}} -
                         symbolic::LiteralExpr{factory, int64_t{-1}} * oldValue) /
                        symbolic::LiteralExpr{factory, int64_t{2}};
        EXPECT_EQ(value->second, expected);
        EXPECT_EQ(&value->second.factory(), &factory);
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

        auto x        = state.getPaths().front()->getVarState(func->getParamDecl(0));
        auto y        = state.getPaths().front()->getVarState(func->getParamDecl(1));
        auto expected = x.binary(symbolic::BinaryOp::Add, y);

        state.step(assign);

        ASSERT_EQ(state.getPaths().size(), 1u);
        auto actual = state.getPaths().front()->getVarState(func->getParamDecl(0));
        EXPECT_EQ(actual, expected);
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
        auto expected = literalHandle(postState->getExprFactory(), 42);
        EXPECT_EQ(handle(*value), expected);
        ASSERT_TRUE(path.getReturnExpr().has_value());
        EXPECT_EQ(handle(*path.getReturnExpr()), expected);
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
        auto input = state.getPaths().front()->getVarState(func->getParamDecl(0));

        state.step(func->getBody());

        ASSERT_EQ(state.getPaths().size(), 1u);
        const auto &result = state.getPaths().front()->getReturnExpr();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(handle(*result), handle(input));
    }

    TEST(ProgramStateTest, SwitchCasesKeepExpressionFacadesInContextFactory) {
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

        PathConditions returns;
        for (const auto &path : state.getPaths()) {
            ASSERT_TRUE(path->getReturnExpr().has_value());
            EXPECT_EQ(&path->getReturnExpr()->factory(), &context.getExprFactory());
            returns.emplace(*path->getReturnExpr());
            for (const auto &cond : path->getPathConditions()) {
                EXPECT_EQ(&cond.factory(), &context.getExprFactory());
                auto interned = importExprHandle(context.getExprFactory(), handle(cond));
                EXPECT_EQ(handle(cond), interned);
            }
        }

        EXPECT_TRUE(returns.contains(
            FacadeExprForTest{context.getExprFactory(), literalHandle(context.getExprFactory(), 10)}));
        EXPECT_TRUE(returns.contains(
            FacadeExprForTest{context.getExprFactory(), literalHandle(context.getExprFactory(), 20)}));
        EXPECT_TRUE(returns.contains(
            FacadeExprForTest{context.getExprFactory(), literalHandle(context.getExprFactory(), 30)}));
    }

    TEST(PathTest, ExtractLValueReusesFactoryAddressFacade) {
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

        auto first  = path.extractLValue(varExpr);
        auto second = path.extractLValue(varExpr);
        EXPECT_EQ(&first.factory(), &context.getExprFactory());
        EXPECT_EQ(first, expected);
        EXPECT_EQ(second, first);
    }

    TEST(PathTest, ExtractLValueReturnsFieldAddressFacade) {
        ASTExtractor extractor(R"c(
            struct Item { int field; };
            void func(void) {
                struct Item item;
                item.field;
            }
        )c");
        auto *func       = extractor.findFirstDecl<FunctionDecl>();
        auto *item       = extractor.findFirstDecl<VarDecl>();
        auto *memberExpr = extractor.findFirstStmt<MemberExpr>();
        ASSERT_NE(func, nullptr);
        ASSERT_NE(item, nullptr);
        ASSERT_NE(memberExpr, nullptr);

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        auto point = symbolic::SourcePoint::fromFuncDecl(func, extractor.getSourceManager(),
                                                         extractor.getLangOptions());
        Path path(context, point);
        path.allocMemory(item, true);

        auto field = path.extractLValue(memberExpr);
        EXPECT_EQ(&field.factory(), &context.getExprFactory());
        EXPECT_TRUE(field.isFieldAddress());
        EXPECT_EQ(field, path.extractLValue(memberExpr));
    }

    TEST(PathTest, ExtractLValueReturnsIndexedAddressFacade) {
        ASTExtractor extractor(R"c(
            void func(int *items, int index) {
                items[index];
            }
        )c");
        auto *func     = extractor.findFirstDecl<FunctionDecl>();
        auto *arraySub = extractor.findFirstStmt<ArraySubscriptExpr>();
        ASSERT_NE(func, nullptr);
        ASSERT_NE(arraySub, nullptr);
        ASSERT_EQ(func->getNumParams(), 2u);

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        auto point = symbolic::SourcePoint::fromFuncDecl(func, extractor.getSourceManager(),
                                                         extractor.getLangOptions());
        Path path(context, point);
        path.allocMemory(func->getParamDecl(0), true);
        path.allocMemory(func->getParamDecl(1), true);

        auto indexed = path.extractLValue(arraySub);
        EXPECT_EQ(&indexed.factory(), &context.getExprFactory());
        EXPECT_TRUE(indexed.isSymbolAddress());
        EXPECT_EQ(indexed, path.extractLValue(arraySub));
    }

    TEST(PathTest, VariableAddressFacadesSurviveAllocationAndClone) {
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

        EXPECT_EQ(first, second);
        EXPECT_EQ(cloned->getVarAddr().at(var), first);
        EXPECT_EQ(&first.factory(), &context.getExprFactory());
        EXPECT_EQ(&cloned->getVarAddr().at(var).factory(), &context.getExprFactory());
        EXPECT_EQ(handle(cloned->getVarAddr().at(var)), handle(first));
        EXPECT_EQ(handle(first), variableAddressHandle(context.getExprFactory(), var));
        auto pathValue   = path.getVarState(var);
        auto clonedValue = cloned->getVarState(var);
        EXPECT_EQ(&pathValue.factory(), &context.getExprFactory());
        EXPECT_EQ(&clonedValue.factory(), &context.getExprFactory());
        EXPECT_EQ(pathValue, *stored);
        EXPECT_EQ(clonedValue, *stored);
    }

    TEST(PathTest, UpdateVarStateImportsForeignExpressionFacade) {
        ASTExtractor extractor(R"c(
            void func(int value) {}
        )c");
        auto *func = extractor.findFirstDecl<FunctionDecl>();
        ASSERT_NE(func, nullptr);
        auto *var = func->getParamDecl(0);

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        auto point = symbolic::SourcePoint::fromFuncDecl(func, extractor.getSourceManager(),
                                                         extractor.getLangOptions());
        Path path(context, point);
        path.allocMemory(var);

        symbolic::ExprFactory foreignFactory;
        symbolic::LiteralExpr foreignValue{foreignFactory, int64_t{42}};
        path.updateVarState(var, foreignValue);

        auto stored = path.getVarState(var);
        EXPECT_EQ(&stored.factory(), &context.getExprFactory());
        EXPECT_NE(handle(stored), handle(foreignValue));
        EXPECT_TRUE(stored.structurallyEqual(foreignValue));
    }

    TEST(PathTest, UpdateMemoryImportsForeignAddressAndExpressionFacades) {
        ASTExtractor extractor(R"c(
            void func(int value) {}
        )c");
        auto *func = extractor.findFirstDecl<FunctionDecl>();
        ASSERT_NE(func, nullptr);
        auto *var = func->getParamDecl(0);

        context::ACSLGContext context(extractor.getASTContext());
        symbolic::ExprFactoryScope scope(context.getExprFactory());
        auto point = symbolic::SourcePoint::fromFuncDecl(func, extractor.getSourceManager(),
                                                         extractor.getLangOptions());
        Path path(context, point);
        auto localAddr = path.allocMemory(var);

        symbolic::ExprFactory foreignFactory;
        FacadeAddrForTest foreignAddr{foreignFactory, variableAddressHandle(foreignFactory, var)};
        symbolic::LiteralExpr foreignValue{foreignFactory, int64_t{42}};
        path.updateMemory(foreignAddr, foreignValue);

        auto stored = path.getMemoryState().read(localAddr);
        ASSERT_TRUE(stored);
        EXPECT_EQ(&stored->factory(), &context.getExprFactory());
        EXPECT_NE(handle(*stored), handle(foreignValue));
        EXPECT_TRUE(stored->structurallyEqual(foreignValue));
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

        auto one = literalHandle(context.getExprFactory(), 1);
        auto two = literalHandle(context.getExprFactory(), 2);
        PathConditions nodes;
        for (const auto &value : result.second) {
            EXPECT_EQ(&value.factory(), &context.getExprFactory());
            nodes.emplace(value);
        }
        EXPECT_TRUE(nodes.contains(FacadeExprForTest{context.getExprFactory(), one}));
        EXPECT_TRUE(nodes.contains(FacadeExprForTest{context.getExprFactory(), two}));
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

        auto oldValue    = path.getVarState(var);
        auto expectedNew = oldValue.binary(symbolic::BinaryOp::Add,
                                           symbolic::LiteralExpr{context.getExprFactory(), 1});
        auto result = path.evalExpr(inc);

        ASSERT_TRUE(result.first.empty());
        ASSERT_EQ(result.second.size(), 1u);
        EXPECT_EQ(&result.second[0].factory(), &context.getExprFactory());
        EXPECT_EQ(result.second[0], oldValue);
        EXPECT_EQ(path.getVarState(var), expectedNew);
    }

    TEST_F(MemoryModelTest, ReadAfterWrite_VarAddr) {
        MemoryModel mm;

        auto addr     = makeVariableAddr(1);
        auto expr     = makeSymbolValue(42);
        auto saveExpr = internForTest(expr);
        mm.write(FacadeAddrForTest{addr}, FacadeExprForTest{saveExpr});

        auto got = mm.read(FacadeAddrForTest{addr});
        ASSERT_NE(got, nullopt);
        EXPECT_EQ(&got->factory(), &symbolic::ExprFactoryScope::current());
        EXPECT_TRUE(handle(*got).structurallyEqual(saveExpr));
    }

    TEST_F(MemoryModelTest, EqualStoredValuesShareInternedHandle) {
        MemoryModel mm;

        auto expected = makeSymbolValue(42);
        auto equalValue1 = makeSymbolValue(42);
        auto equalValue2 = makeSymbolValue(42);
        mm.write(FacadeAddrForTest{makeVariableAddr(1)}, FacadeExprForTest{internForTest(equalValue1)});
        mm.write(FacadeAddrForTest{makeVariableAddr(2)}, FacadeExprForTest{internForTest(equalValue2)});
        auto expectedHandle = importExprHandle(symbolic::ExprFactoryScope::current(), expected);
        mm.write(FacadeAddrForTest{makeVariableAddr(3)}, FacadeExprForTest{expectedHandle});

        std::vector<symbolic::detail::ExprHandle> flatValues;
        for (auto &&[addr, value] : mm.flat()) {
            if (handle(value).structurallyEqual(expected))
                flatValues.push_back(handle(value));
        }

        ASSERT_EQ(flatValues.size(), 3u);
        EXPECT_EQ(flatValues[0], flatValues[1]);
        EXPECT_EQ(flatValues[0], flatValues[2]);

        auto readBack = mm.read(FacadeAddrForTest{makeVariableAddr(1)});
        ASSERT_TRUE(readBack);
        EXPECT_TRUE(handle(*readBack).structurallyEqual(expected));
        EXPECT_EQ(handle(*readBack), flatValues[0]);
    }

    TEST_F(MemoryModelTest, FacadeAddressReadWriteUsesInternedAddressAndValue) {
        auto &factory = symbolic::ExprFactoryScope::current();
        MemoryModel mm;

        auto addr = makeRangeAddr(1, makeLiteralHandle(3), std::nullopt);
        auto value = literalHandle(factory, int64_t{42});

        mm.write(FacadeAddrForTest{addr}, FacadeExprForTest{value});
        EXPECT_TRUE(mm.contains(FacadeAddrForTest{addr}));

        auto readBack = mm.read(FacadeAddrForTest{addr});
        ASSERT_TRUE(readBack);
        EXPECT_EQ(handle(*readBack), value);
    }

    TEST_F(MemoryModelTest, FacadeWriteImportsForeignValueIntoMemoryFactory) {
        auto &factory = symbolic::ExprFactoryScope::current();
        MemoryModel mm{factory};
        auto addr = makeVariableAddr(1);

        symbolic::ExprFactory foreignFactory;
        FacadeExprForTest foreignValue{foreignFactory, literalHandle(foreignFactory, 42)};
        mm.write(FacadeAddrForTest{addr}, FacadeExprForTest{foreignValue});

        auto readBack = mm.read(FacadeAddrForTest{addr});
        ASSERT_TRUE(readBack);
        EXPECT_EQ(&readBack->factory(), &factory);
        EXPECT_NE(handle(*readBack), handle(foreignValue));
        EXPECT_TRUE(readBack->structurallyEqual(foreignValue));
    }

    TEST_F(MemoryModelTest, AssignmentOwnsCrossFactoryRangeGraph) {
        symbolic::ExprFactory targetFactory;
        MemoryModel target{targetFactory};
        auto targetBase = FacadeAddrForTest{
            targetFactory, variableAddressHandle(targetFactory, getVarDecl(1))};
        symbolic::LiteralExpr targetOffset{targetFactory, 3};
        auto targetPoint =
            symbolic::Addr::symbol(QualType{}, targetBase, defaultPoint, targetOffset);

        {
            symbolic::ExprFactory sourceFactory;
            MemoryModel source{sourceFactory};
            auto sourceBase = FacadeAddrForTest{
                sourceFactory, variableAddressHandle(sourceFactory, getVarDecl(1))};
            symbolic::LiteralExpr sourceOffset{sourceFactory, 3};
            symbolic::LiteralExpr sourceLength{sourceFactory, 2};
            auto sourceRange = symbolic::Addr::symbol(QualType{}, sourceBase, defaultPoint,
                                                      sourceOffset, sourceLength);
            symbolic::LiteralExpr sourceValue{sourceFactory, 42};
            source.write(sourceRange, sourceValue);

            target = source;
        }

        auto readBack = target.read(targetPoint);
        ASSERT_TRUE(readBack);
        EXPECT_EQ(&readBack->factory(), &targetFactory);
        EXPECT_EQ(readBack->tryEvalAsConstant(), 42);
        for (const auto &[address, value] : target.flat()) {
            EXPECT_EQ(handle(address), importAddressHandle(targetFactory, handle(address)));
            EXPECT_EQ(&value.factory(), &targetFactory);
        }
    }

    TEST_F(MemoryModelTest, Flat_Yields_All_Three_Categories) {
        MemoryModel mm;

        // noOffset
        auto baseA  = makeVariableAddr(1);
        auto eA     = makeSymbolValue(1);
        auto saveEA = internForTest(eA);
        auto addrAHandle = baseA;
        mm.write(FacadeAddrForTest{baseA}, FacadeExprForTest{internForTest(eA)});

        // constantRange
        auto rangeB = makeRangeAddr(2, /*off=*/makeLiteralHandle(4),
                                    /*len=*/makeLiteralHandle(2));
        auto eB     = makeSymbolValue(2);
        auto saveEB = internForTest(eB);
        auto addrBHandle = rangeB;
        mm.write(FacadeAddrForTest{rangeB}, FacadeExprForTest{internForTest(eB)});

        // symbolicRange
        auto rangeC =
            makeRangeAddr(3, /*off=*/internForTest(makeSymbolValue(3)), std::nullopt);
        auto eC     = makeSymbolValue(3);
        auto saveEC = internForTest(eC);
        auto addrCHandle = rangeC;
        mm.write(FacadeAddrForTest{rangeC}, FacadeExprForTest{internForTest(eC)});

        bool fA = false, fB = false, fC = false;
        for (auto &&[addr, value] : mm.flat()) {
            EXPECT_EQ(&value.factory(), &symbolic::ExprFactoryScope::current());
            if (handle(value).structurallyEqual(saveEA)) {
                fA = true;
                EXPECT_EQ(handle(addr), addrAHandle);
            } else if (handle(value).structurallyEqual(saveEB)) {
                fB = true;
                EXPECT_EQ(handle(addr), addrBHandle);
            } else if (handle(value).structurallyEqual(saveEC)) {
                fC = true;
                EXPECT_EQ(handle(addr), addrCHandle);
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
        mm.write(FacadeAddrForTest{aRange}, FacadeExprForTest{saveA});

        // B: [3,8)
        auto bRange = makeRangeAddr(baseId, makeLiteralHandle(3U), makeLiteralHandle(5U));
        auto eB     = makeSymbolValue(200);
        auto saveB  = internForTest(eB);
        mm.write(FacadeAddrForTest{bRange}, FacadeExprForTest{saveB});

        // C: [1,3)
        auto cRange = makeRangeAddr(baseId, makeLiteralHandle(1U), makeLiteralHandle(2U));
        auto eC     = makeSymbolValue(300);
        auto saveC  = internForTest(eC);
        mm.write(FacadeAddrForTest{cRange}, FacadeExprForTest{saveC});

        // D: [7,10)
        auto dRange = makeRangeAddr(baseId, makeLiteralHandle(7U), makeLiteralHandle(3U));
        auto eD     = makeSymbolValue(400);
        auto saveD  = internForTest(eD);
        mm.write(FacadeAddrForTest{dRange}, FacadeExprForTest{saveD});

        //  [0] -> A
        //  [1,2] -> C
        //  [3,6] -> B
        //  [7,9] -> D
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 0, saveA));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 1, saveC));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 2, saveC));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 3, saveB));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 4, saveB));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 5, saveB));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 6, saveB));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 7, saveD));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 8, saveD));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 9, saveD));

        EXPECT_TRUE(ExpectReadNullAt(mm, baseId, 10));
    }

    TEST_F(MemoryModelTest, ConstRange_ExactOverrideSameInterval) {
        MemoryModel mm;
        const unsigned baseId = 11;

        // X: [5,9)
        auto r = makeRangeAddr(baseId, makeLiteralHandle(5U), makeLiteralHandle(4U));
        auto eX    = makeSymbolValue(500);
        auto saveX = internForTest(eX);
        mm.write(FacadeAddrForTest{r}, FacadeExprForTest{saveX});

        // Y: [5,9)
        auto eY    = makeSymbolValue(600);
        auto saveY = internForTest(eY);
        mm.write(FacadeAddrForTest{r}, FacadeExprForTest{saveY});

        for (uint64_t off = 5; off < 9; ++off) {
            EXPECT_TRUE(ExpectReadEqAt(mm, baseId, off, saveY));
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

        pathA->updateMemory(addr0A, FacadeExprForTest{acslContext.getExprFactory(), val0});
        pathB->updateMemory(addr1B, FacadeExprForTest{acslContext.getExprFactory(), val1});

        pathA->mergeWith(*pathB);

        ASSERT_EQ(pathA->getVarAddr().size(), 2u);
        EXPECT_TRUE(pathA->getVarAddr().contains(var0));
        EXPECT_TRUE(pathA->getVarAddr().contains(var1));
        EXPECT_EQ(pathA->getVarAddr().at(var1), addr1B);

        auto gotVal1 = pathA->getMemoryState().read(pathA->getVarAddr().at(var0));
        ASSERT_TRUE(gotVal1);
        EXPECT_TRUE(gotVal1.value().isUnknown());

        auto gotVal2 = pathA->getMemoryState().read(pathA->getVarAddr().at(var1));
        ASSERT_TRUE(gotVal2);
        EXPECT_TRUE(gotVal2.value().isUnknown());
    }

    TEST_F(MergeWithTest, ConflictingValuesBecomeUnknown) {
        auto var0   = getVarDecl(0);
        auto addr0A = pathA->allocMemory(var0);
        auto addr0B = pathB->allocMemory(var0);

        auto valueA = makeSymbolValue(1);
        auto valueB = makeSymbolValue(2);
        pathA->updateMemory(addr0A, FacadeExprForTest{acslContext.getExprFactory(), valueA});
        pathB->updateMemory(addr0B, FacadeExprForTest{acslContext.getExprFactory(), valueB});

        pathA->mergeWith(*pathB);

        auto val = pathA->getMemoryState().read(addr0A);
        ASSERT_TRUE(val);
        EXPECT_TRUE(val->isUnknown());
    }

    TEST_F(MergeWithTest, PathConditionsIntersect) {
        auto condShared = makeLiteral(1);
        auto condAOnly  = makeLiteral(2);

        pathA->insertPathCondition(condShared);
        ASSERT_EQ(pathA->getPathConditions().size(), 1u);
        auto sharedExpr = *pathA->getPathConditions().begin();

        pathA->insertPathCondition(condAOnly);
        pathB->insertPathCondition(condShared);
        ASSERT_EQ(pathB->getPathConditions().size(), 1u);
        EXPECT_EQ(sharedExpr, *pathB->getPathConditions().begin());

        pathA->mergeWith(*pathB);

        ASSERT_EQ(pathA->getPathConditions().size(), 1u);
        const auto &onlyCond = *pathA->getPathConditions().begin();
        EXPECT_EQ(sharedExpr, onlyCond);
        auto literal = symbolic::LiteralExpr::tryFrom(onlyCond);
        ASSERT_TRUE(literal.has_value());
        EXPECT_EQ(handle(*literal), handle(condShared));
    }

    TEST_F(MergeWithTest, PathConditionFacadeImportsForeignFactoryAndSurvivesClone) {
        symbolic::ExprFactory foreignFactory;
        FacadeExprForTest foreignCond{foreignFactory, literalHandle(foreignFactory, 77)};

        pathA->insertPathCondition(foreignCond);

        ASSERT_EQ(pathA->getPathConditions().size(), 1u);
        const auto &stored = *pathA->getPathConditions().begin();
        EXPECT_EQ(&stored.factory(), &acslContext.getExprFactory());
        EXPECT_NE(handle(stored), handle(foreignCond));
        EXPECT_TRUE(stored.structurallyEqual(foreignCond));

        auto cloned = pathA->clone();
        ASSERT_EQ(cloned->getPathConditions().size(), 1u);
        const auto &clonedCond = *cloned->getPathConditions().begin();
        EXPECT_EQ(&clonedCond.factory(), &acslContext.getExprFactory());
        EXPECT_EQ(handle(clonedCond), handle(stored));
    }

    TEST_F(MergeWithTest, ReturnExprDiffersBecomesUnknown) {
        auto var0 = getVarDecl(0);
        pathA->allocMemory(var0);
        pathB->allocMemory(var0);

        pathA->setPathState(Path::PathState::Return);
        pathB->setPathState(Path::PathState::Return);

        auto returnA = makeLiteral(1);
        auto returnB = makeLiteral(2);
        pathA->setReturnExpr(returnA);
        pathB->setReturnExpr(returnB);

        pathA->mergeWith(*pathB);

        ASSERT_TRUE(pathA->getReturnExpr());
        EXPECT_TRUE(pathA->getReturnExpr().value().isUnknown());
    }

    TEST_F(MergeWithTest, ReturnExprUsesInternedFacadesAcrossCloneAndMerge) {
        pathA->setPathState(Path::PathState::Return);
        pathB->setPathState(Path::PathState::Return);

        auto returnA = makeLiteral(7);
        pathA->setReturnExpr(returnA);
        ASSERT_TRUE(pathA->getReturnExpr());
        auto returnHandle = pathA->getReturnExpr().value();

        auto cloned = pathA->clone();
        ASSERT_TRUE(cloned->getReturnExpr());
        EXPECT_EQ(returnHandle, cloned->getReturnExpr().value());

        auto returnB = makeLiteral(7);
        pathB->setReturnExpr(returnB);
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
        mm.write(FacadeAddrForTest{r1}, FacadeExprForTest{sv});

        // [3,5) value=V
        auto r2 = makeRangeAddr(baseId, makeLiteralHandle(3U), makeLiteralHandle(2U));
        auto v2 = makeSymbolValue(1000); // same value
        mm.write(FacadeAddrForTest{r2}, FacadeExprForTest{internForTest(v2)});

        // Trigger constant-range merge
        mm.mergeConstantRanges();

        // Behavioral check: read [0..4] should all yield the same value
        for (uint64_t off = 0; off < 5; ++off) {
            EXPECT_TRUE(ExpectReadEqAt(mm, baseId, off, sv));
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
        mm.write(FacadeAddrForTest{r1}, FacadeExprForTest{s1});

        // [3,5) value=V2 (different value)
        auto r2 = makeRangeAddr(baseId, makeLiteralHandle(3U), makeLiteralHandle(2U));
        auto v2 = makeSymbolValue(2222);
        auto s2 = internForTest(v2);
        mm.write(FacadeAddrForTest{r2}, FacadeExprForTest{s2});

        mm.mergeConstantRanges();

        // Behavioral: left and right parts remain separate
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 0, s1));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 1, s1));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 2, s1));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 3, s2));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 4, s2));
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
        mm.write(FacadeAddrForTest{r1}, FacadeExprForTest{s});
        mm.write(FacadeAddrForTest{r2}, FacadeExprForTest{internForTest(makeSymbolValue(3333))});
        mm.write(FacadeAddrForTest{r3}, FacadeExprForTest{internForTest(makeSymbolValue(3333))});

        mm.mergeConstantRanges();

        for (uint64_t off = 0; off < 7; ++off) {
            EXPECT_TRUE(ExpectReadEqAt(mm, baseId, off, s));
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
        mm.write(FacadeAddrForTest{r1}, FacadeExprForTest{s});
        mm.write(FacadeAddrForTest{r2},
                 FacadeExprForTest{internForTest(makeSymbolValue(5555))}); // different
        mm.write(FacadeAddrForTest{r3},
                 FacadeExprForTest{internForTest(makeSymbolValue(4444))}); // same as r1

        mm.mergeConstantRanges();

        // Behavioral: 0..1 = 4444, 2..4 = 5555, 5..6 = 4444
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 0, s));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 1, s));

        auto w = makeSymbolValue(5555);
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 2, w));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 3, w));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 4, w));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 5, s));
        EXPECT_TRUE(ExpectReadEqAt(mm, baseId, 6, s));
        EXPECT_TRUE(ExpectReadNullAt(mm, baseId, 7));

        EXPECT_EQ(mm.sizeWithoutFields(), 3);
    }

    namespace {
        auto makeAdd(symbolic::detail::ExprHandle a, symbolic::detail::ExprHandle b) {
            auto &factory = symbolic::ExprFactoryScope::current();
            return binaryHandle(factory, a, symbolic::BinaryOp::Add, b);
        }
    } // namespace

    TEST_F(MemoryModelTest, MergeSymbolicRanges_ThreeSinglesChainIntoLen3) {
        MemoryModel mm;
        const unsigned baseId = 31;

        // X, X+1, X+2 each represents a single address (non-range → [off, off+1))
        auto X = internForTest(makeSymbolValue(901));
        auto &factory = symbolic::ExprFactoryScope::current();
        auto X1 = makeAdd(X, literalHandle(factory, 1U));
        auto X2 = makeAdd(X, literalHandle(factory, 2U));

        auto a0 = makeRangeAddr(baseId, X, std::nullopt);  // single @ X
        auto a1 = makeRangeAddr(baseId, X1, std::nullopt); // single @ X+1
        auto a2 = makeRangeAddr(baseId, X2, std::nullopt); // single @ X+2

        // Same value
        auto v = makeSymbolValue(7777);
        mm.write(FacadeAddrForTest{a0}, FacadeExprForTest{internForTest(v)});
        mm.write(FacadeAddrForTest{a1}, FacadeExprForTest{internForTest(makeSymbolValue(7777))});
        mm.write(FacadeAddrForTest{a2}, FacadeExprForTest{internForTest(makeSymbolValue(7777))});

        // Trigger symbolic-range merge (hash-based chaining)
        mm.mergeSymbolicRanges();

        EXPECT_EQ(mm.sizeWithoutFields(), 1);
    }

    TEST_F(MemoryModelTest, MergeSymbolicRanges_SameValueButNonContiguous_ShouldNotMerge) {
        MemoryModel mm;
        const unsigned baseId = 32;

        auto X = internForTest(makeSymbolValue(902));
        auto &factory = symbolic::ExprFactoryScope::current();
        auto X2 = makeAdd(X, literalHandle(factory, 2U));

        auto a0 = makeRangeAddr(baseId, X, std::nullopt);  // single @ X
        auto a2 = makeRangeAddr(baseId, X2, std::nullopt); // single @ X+2

        auto v = makeSymbolValue(8888);
        mm.write(FacadeAddrForTest{a0}, FacadeExprForTest{internForTest(v)});
        mm.write(FacadeAddrForTest{a2}, FacadeExprForTest{internForTest(
                                         makeSymbolValue(8888))}); // same value but with a gap of 1

        mm.mergeSymbolicRanges();

        // Two ranges with the same value but non-contiguous → must not merge (expect 2 entries)
        EXPECT_EQ(mm.sizeWithoutFields(), 2);
    }

    TEST_F(MemoryModelTest, MergeSymbolicRanges_ContiguousButDifferentValue_ShouldNotMerge) {
        MemoryModel mm;
        const unsigned baseId = 33;

        auto X = internForTest(makeSymbolValue(903));
        auto &factory = symbolic::ExprFactoryScope::current();
        auto X1 = makeAdd(X, literalHandle(factory, 1U));

        auto a0 = makeRangeAddr(baseId, X, std::nullopt);  // single @ X
        auto a1 = makeRangeAddr(baseId, X1, std::nullopt); // single @ X+1

        auto v1 = makeSymbolValue(10001);
        auto v2 = makeSymbolValue(10002);
        auto s1 = internForTest(v1);
        mm.write(FacadeAddrForTest{a0}, FacadeExprForTest{internForTest(v1)});
        mm.write(FacadeAddrForTest{a1}, FacadeExprForTest{internForTest(v2)}); // different value

        mm.mergeSymbolicRanges();

        // Should remain as two separate entries
        EXPECT_EQ(mm.sizeWithoutFields(), 2);
        size_t countV1 = 0, countV2 = 0;
        for (auto &&[addr, value] : mm.flat()) {
            if (handle(value).structurallyEqual(s1))
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
        auto X1A = makeAdd(XA, literalHandle(factory, 1U));
        auto a0A = makeRangeAddr(baseA, XA, std::nullopt);
        auto a1A = makeRangeAddr(baseA, X1A, std::nullopt);

        auto XB = internForTest(makeSymbolValue(910)); // same construction but different base
        auto X1B = makeAdd(XB, literalHandle(factory, 1U));
        auto a0B = makeRangeAddr(baseB, XB, std::nullopt);
        auto a1B = makeRangeAddr(baseB, X1B, std::nullopt);

        auto vA  = makeSymbolValue(1212);
        auto svA = internForTest(vA);
        auto vB  = makeSymbolValue(1212);
        auto svB = internForTest(vB);

        mm.write(FacadeAddrForTest{a0A}, FacadeExprForTest{internForTest(vA)});
        mm.write(FacadeAddrForTest{a1A}, FacadeExprForTest{internForTest(makeSymbolValue(1212))});
        mm.write(FacadeAddrForTest{a0B}, FacadeExprForTest{internForTest(vB)});
        mm.write(FacadeAddrForTest{a1B}, FacadeExprForTest{internForTest(makeSymbolValue(1212))});

        mm.mergeSymbolicRanges();

        // Each base should merge within itself; no cross-base merge
        EXPECT_EQ(mm.sizeWithoutFields(), 2);
        size_t cntA = 0, cntB = 0;
        for (auto &&[addr, value] : mm.flat()) {
            if (!handle(value).structurallyEqual(svA) &&
                !handle(value).structurallyEqual(svB))
                continue;
            auto symbolAddr = symbolic::SymbolAddress::tryFrom(addr);
            ASSERT_TRUE(symbolAddr);
            if (symbolAddr->baseInfo() == symbolic::SymbolAddress{FacadeAddrForTest{a0A}}.baseInfo())
                ++cntA;
            if (symbolAddr->baseInfo() == symbolic::SymbolAddress{FacadeAddrForTest{a0B}}.baseInfo())
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
        auto X1 = makeAdd(X, literalHandle(factory, 1U));
        auto a1 = makeRangeAddr(baseId, X1, literalHandle(factory, 3U));

        // Same value
        auto v  = makeSymbolValue(1313);
        auto sv = internForTest(v);
        mm.write(FacadeAddrForTest{a0}, FacadeExprForTest{internForTest(v)});
        mm.write(FacadeAddrForTest{a1}, FacadeExprForTest{internForTest(makeSymbolValue(1313))});

        mm.mergeSymbolicRanges();

        // Approximate check: there should be exactly one entry (start at X, total length = 1 + 3 = 4)
        EXPECT_EQ(mm.sizeWithoutFields(), 1);
        for (auto &&[addr, value] : mm.flat()) {
            auto symbolAddr = symbolic::SymbolAddress::tryFrom(addr);
            ASSERT_TRUE(symbolAddr);
            if (symbolAddr->baseInfo() ==
                    symbolic::SymbolAddress{FacadeAddrForTest{a0}}.baseInfo() &&
                handle(value).structurallyEqual(sv)) {
                // If length is accessible and constant, also assert == 4
                if (auto len = symbolAddr->length()) {
                    if (auto c = len.value().tryEvalAsConstant()) {
                        EXPECT_EQ(c.value(), 4);
                        return;
                    }
                    FAIL() << len.value().dump();
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
        auto rangeIndex = rangeIndexHandle(symbolic::ExprFactoryScope::current(), "i");
        mm.write(FacadeAddrForTest{aRange}, FacadeExprForTest{rangeIndex});

        for (uint64_t offset = 0; offset < 10; ++offset)
            EXPECT_TRUE(ExpectReadEqAt(mm, baseId, offset, literalHandle(factory, offset)));

        EXPECT_TRUE(ExpectReadNullAt(mm, baseId, 10));
    }

    TEST_F(MemoryModelTest, SymbolicRange_RangeWithLength1IsNotARange) {
        MemoryModel mm;
        const unsigned baseId = 10;

        auto X = internForTest(makeSymbolValue(904));
        auto &factory = symbolic::ExprFactoryScope::current();
        // A: [X,X+1) -> v
        auto aRange = makeRangeAddr(baseId, X, literalHandle(factory, 1U));
        auto v = internForTest(makeSymbolValue(1313));

        mm.write(FacadeAddrForTest{aRange}, FacadeExprForTest{v});

        // B: an addr with offset X
        auto bAddr = makeRangeAddr(baseId, X, std::nullopt);

        // read(A) == read(B) == v
        auto resA = mm.read(FacadeAddrForTest{aRange});
        ASSERT_TRUE(resA);
        EXPECT_TRUE(handle(*resA).structurallyEqual(v));
        auto resB = mm.read(FacadeAddrForTest{bAddr});
        ASSERT_TRUE(resB);
        EXPECT_TRUE(handle(*resB).structurallyEqual(v));
    }

} // namespace acslg::test::unit::analyzer
