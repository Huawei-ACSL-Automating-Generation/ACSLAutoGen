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

using ::testing::Return;
using namespace std;
using namespace clang;

class TestPath : public Path {
  public:
    // MOCK_NONVIRTUAL_METHOD(
    //     std::unique_ptr<SymbolicExpr>, evalExpr, (const clang::Expr *), (), TestPath);
    MOCK_NONVIRTUAL_METHOD(not_null<unique_ptr<Address>>,
                           extractLValue,
                           (const clang::Expr *),
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

namespace {

    class MemoryModelTest : public ::testing::Test {
      protected:
        MemoryModelTest() {
            std::string code{};
            for (auto i : views::iota(0u, 20u)) {
                code += "int g" + to_string(i) + ";";
            }

            e.init(code);
            for (auto d : e.getASTContext().getTranslationUnitDecl()->decls()) {
                if (auto vd = llvm::dyn_cast<clang::VarDecl>(d))
                    varDecls.push_back(vd);
            }
        }

        not_null<const clang::VarDecl *> getVarDecl(unsigned int id) {
            if (!idCountMap.contains(id)) {
                assert(count < varDecls.size() && "Need more varDecl? Change the for loop above!");
                idCountMap[id] = count++;
            }
            return varDecls.at(idCountMap.at(id));
        }

        VariableAddress makeVariableAddr(unsigned int id) {
            return VariableAddress{getVarDecl(id)};
        }

        Symbolic::SymbolAddress makeRangeAddr(unsigned int id,
                                              unique_ptr<const SymbolicExpr> offset,
                                              unique_ptr<const SymbolicExpr> len) {
            auto defaultPoint = SourcePoint::fromDefault(e.getSourceManager());
            auto baseAddr     = makeVariableAddr(id);
            if (len != nullptr)
                return Symbolic::SymbolAddress{baseAddr.addressClone().into_underlying(),
                                               defaultPoint, std::move(offset), std::move(len)};
            return Symbolic::SymbolAddress{baseAddr.addressClone().into_underlying(), defaultPoint,
                                           std::move(offset), nullopt};
        }

        unique_ptr<Symbolic::Variable> makeVariable(unsigned int id) {
            auto defaultPoint = SourcePoint::fromDefault(e.getSourceManager());
            return std::make_unique<Symbolic::Variable>(
                SymbolicExpr::Type{SymbolicExpr::ScalarKind::UInt, id},
                make_unique<VariableAddress>(getVarDecl(id)), defaultPoint);
        }

        Symbolic::SymbolAddress makePointAddr(unsigned int id, std::uint64_t off) {
            return makeRangeAddr(id, std::make_unique<LiteralExpr>(static_cast<std::uint64_t>(off)),
                                 nullptr);
        }

        void ExpectReadEqAt(MemoryModel &mm,
                            unsigned id,
                            std::uint64_t off,
                            const Symbolic::SymbolicExpr &expected) {
            auto addr = makePointAddr(id, off);
            auto got  = mm.read(addr);
            ASSERT_NE(got, nullopt) << "read returned null at off=" << off;
            EXPECT_EQ(*got.value(), expected) << "mismatch at off=" << off;
        }

        void ExpectReadNullAt(MemoryModel &mm, unsigned id, std::uint64_t off) {
            auto addr = makePointAddr(id, off);
            EXPECT_EQ(mm.read(addr), nullopt) << "expected null at off=" << off;
        }

      private:
        ASTExtractor e;
        std::vector<const clang::VarDecl *> varDecls;
        size_t count{0};
        unordered_map<unsigned int, size_t> idCountMap{};
    };

}; // namespace

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
    auto rangeB = makeRangeAddr(2, /*off=*/make_unique<LiteralExpr>(4),
                                /*len=*/make_unique<LiteralExpr>(2));
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
    auto aRange = makeRangeAddr(baseId, std::make_unique<LiteralExpr>(0U),
                                std::make_unique<LiteralExpr>(10U));
    auto eA     = makeVariable(100);
    auto saveA  = eA->clone();
    mm.write(aRange, std::move(eA));

    // B: [3,8)
    auto bRange =
        makeRangeAddr(baseId, std::make_unique<LiteralExpr>(3U), std::make_unique<LiteralExpr>(5U));
    auto eB    = makeVariable(200);
    auto saveB = eB->clone();
    mm.write(bRange, std::move(eB));

    // C: [1,3)
    auto cRange =
        makeRangeAddr(baseId, std::make_unique<LiteralExpr>(1U), std::make_unique<LiteralExpr>(2U));
    auto eC    = makeVariable(300);
    auto saveC = eC->clone();
    mm.write(cRange, std::move(eC));

    // D: [7,10)
    auto dRange =
        makeRangeAddr(baseId, std::make_unique<LiteralExpr>(7U), std::make_unique<LiteralExpr>(3U));
    auto eD    = makeVariable(400);
    auto saveD = eD->clone();
    mm.write(dRange, std::move(eD));

    //  [0] -> A
    //  [1,2] -> C
    //  [3,6] -> B
    //  [7,9] -> D
    ExpectReadEqAt(mm, baseId, 0, *saveA);
    ExpectReadEqAt(mm, baseId, 1, *saveC);
    ExpectReadEqAt(mm, baseId, 2, *saveC);
    ExpectReadEqAt(mm, baseId, 3, *saveB);
    ExpectReadEqAt(mm, baseId, 4, *saveB);
    ExpectReadEqAt(mm, baseId, 5, *saveB);
    ExpectReadEqAt(mm, baseId, 6, *saveB);
    ExpectReadEqAt(mm, baseId, 7, *saveD);
    ExpectReadEqAt(mm, baseId, 8, *saveD);
    ExpectReadEqAt(mm, baseId, 9, *saveD);

    ExpectReadNullAt(mm, baseId, 10);
}

TEST_F(MemoryModelTest, ConstRange_ExactOverrideSameInterval) {
    MemoryModel mm;
    const unsigned baseId = 11;

    // X: [5,9)
    auto r =
        makeRangeAddr(baseId, std::make_unique<LiteralExpr>(5U), std::make_unique<LiteralExpr>(4U));
    auto eX    = makeVariable(500);
    auto saveX = eX->clone();
    mm.write(r, std::move(eX));

    // Y: [5,9)
    auto eY    = makeVariable(600);
    auto saveY = eY->clone();
    mm.write(r, std::move(eY));

    for (std::uint64_t off = 5; off < 9; ++off) {
        ExpectReadEqAt(mm, baseId, off, *saveY);
    }

    ExpectReadNullAt(mm, baseId, 4);
    ExpectReadNullAt(mm, baseId, 9);
}

TEST_F(MemoryModelTest, ConstRange_TouchingIntervals_NoOverlap) {
    MemoryModel mm;
    const unsigned baseId = 12;

    // [0,3) and [3,5)
    auto r1 =
        makeRangeAddr(baseId, std::make_unique<LiteralExpr>(0U), std::make_unique<LiteralExpr>(3U));
    auto r2 =
        makeRangeAddr(baseId, std::make_unique<LiteralExpr>(3U), std::make_unique<LiteralExpr>(2U));

    auto e1 = makeVariable(700);
    auto e2 = makeVariable(800);
    auto s1 = e1->clone();
    auto s2 = e2->clone();
    mm.write(r1, std::move(e1));
    mm.write(r2, std::move(e2));

    // [0,2]
    ExpectReadEqAt(mm, baseId, 0, *s1);
    ExpectReadEqAt(mm, baseId, 1, *s1);
    ExpectReadEqAt(mm, baseId, 2, *s1);

    // [3,4]
    ExpectReadEqAt(mm, baseId, 3, *s2);
    ExpectReadEqAt(mm, baseId, 4, *s2);

    ExpectReadNullAt(mm, baseId, 5);
}
TEST_F(MemoryModelTest, EraseExpiredLocals) {
    auto code = R"(
void func(int param) {
    int x;
}

void test_mm_erase(int param) { 
    int x = 1;

    {
        int y = 2;
        int z = 3;
    }

    for (int i = 0; i < 2; ++i) {
        int t = i;
    }

    {
        int x = 42;
    }

    func(x);
}
)";

    ASTExtractor e;
    e.init(code);

    auto context       = ACSLContext{e.getASTContext()};
    auto func          = e.findNthDecl<clang::FunctionDecl>(2);
    auto symbolicState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context);
    symbolicState->init();
    EXPECT_EQ(symbolicState->getPaths().at(0)->getMemoryState().sizeWithoutFields(), 1);
    for (clang::Stmt *stmt : func->getBody()->children()) {
        symbolicState->step(stmt);
        EXPECT_EQ(symbolicState->getPaths().at(0)->getMemoryState().sizeWithoutFields(), 2)
            << symbolicState->dump();
    }
}