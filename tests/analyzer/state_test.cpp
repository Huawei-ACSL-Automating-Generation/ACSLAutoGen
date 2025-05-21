// tests/specGenerator/state_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <unordered_map>
#include <string>
#include "xmock.h"
#include "state.h"
#include "ASTExtractor.h"
#include "clang/AST/AST.h"
#include "clang/AST/Decl.h"
#include "symbolic.h"

using ::testing::Return;
using namespace std;

class TestPath : public Path
{
  public:
    // MOCK_NONVIRTUAL_METHOD(
    //     std::unique_ptr<SymbolicExpr>, evalExpr, (const clang::Expr *), (), TestPath);
    MOCK_NONVIRTUAL_METHOD(LValueTarget, extractLValue, (const clang::Expr *), (), TestPath);
};

TEST(PathTest, IsUnchangedState)
{
    using enum SymbolicExpr::ScalarKind;
    TestPath dummyPath;
    auto addr = dummyPath.allocMemory();
    dummyPath.updateMemory(addr, make_unique<Variable>("test", SymbolicExpr::Type{Int, 4}));

    ASSERT_DEATH(dummyPath.isUnchangedState(*addr), "");
}

/*
TEST(PathTest, ExtractAddress)
{
    TestPath path;

    EXPECT_CALL(path, extractLValue)
        .WillOnce(Return(make_unique<Address>(1, SymbolicExpr::makeNull())));

    auto address = path.extractAddress((clang::Expr *)1);
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
    auto varDecl = e.findFirstDecl<clang::VarDecl>();

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
    auto varDecl = e.findFirstDecl<clang::VarDecl>();
    clang::Expr *expr = (clang::Expr *)1;

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
    auto expr = (clang::Expr *)1;
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

    vector<const clang::Expr *> conds(2, (clang::Expr *)1);
    conds.resize(5, nullptr);

    path.insertDefaultPathConds(conds);

    EXPECT_EQ(path.getPathConditions().size(), 4);
}

TEST(PathTest, SetReturnExpr)
{
    TestPath path;
    clang::Expr *expr = (clang::Expr *)1;
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

        if (auto &LMS = LHS.getMemoryState(), &RMS = RHS.getMemoryState(); LMS.size() == RMS.size())
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
    auto varDecl = e.findFirstDecl<clang::VarDecl>();

    TestPath path;
    clang::Expr *expr = (clang::Expr *)1;
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