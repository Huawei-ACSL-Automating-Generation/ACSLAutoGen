// tests/unit/SpecGenerator/specGenerator_test.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "testHelper.h"

using namespace std;
using namespace clang;
using namespace Symbolic;

namespace {
    unique_ptr<SymbolicExpr> makeConstU64(uint64_t v) { return make_unique<LiteralExpr>(v); }

    unique_ptr<SymbolicExpr> makeAdd(unique_ptr<SymbolicExpr> a, unique_ptr<SymbolicExpr> b) {
        return make_unique<BinaryOpExpr>(std::move(a), BinaryOpExpr::Operator::Add, std::move(b));
    }

    template <class T> static not_null<unique_ptr<T>> makeNotNull(unique_ptr<T> p) {
        return not_null<unique_ptr<T>>(std::move(p));
    }

    struct SubstituteTest : public FixtureWithCode {
      protected:
        SubstituteTest()
            : acslContext(e.getASTContext()), path(make_unique<Path>(acslContext, defaultPoint)),
              mm(path->getMutMemoryState()) {}

        SourcePoint getSourcePoint(unsigned int id) {
            auto newFuncDecl = getFuncDecl(id);
            return SourcePoint::fromFuncDeclBefore(newFuncDecl, e.getSourceManager(),
                                                   e.getLangOptions());
        }

      private:
        ACSLContext acslContext;

      protected:
        std::unique_ptr<Path> path;
        MemoryModel &mm;
    };

} // namespace

TEST_F(SubstituteTest, VarWithMatchingFromIsReplacedFromLoopEntry) {
    auto var0Addr = makeVariableAddr(0);
    mm.write(var0Addr, makeConstU64(42));

    auto point   = getSourcePoint(0);
    auto varNode = makeVariable(0, point);
    auto expr    = makeNotNull(unique_ptr<SymbolicExpr>(varNode.release()));

    substituteSymbols(expr, *path, point);

    ASSERT_EQ(*expr, *makeConstU64(42));
}

TEST_F(SubstituteTest, VarWithDifferentFromIsKeptUnchanged) {
    auto var0Addr = makeVariableAddr(0);
    mm.write(var0Addr, makeConstU64(7));

    auto point   = getSourcePoint(0);
    auto varNode = makeVariable(0, point);

    auto exprBefore = varNode->clone();
    auto expr       = makeNotNull(unique_ptr<SymbolicExpr>(varNode.release()));

    substituteSymbols(expr, *path, getSourcePoint(42));

    ASSERT_EQ(*expr, *exprBefore);
}

TEST_F(SubstituteTest, CompositeExprIsSubstitutedRecursively) {
    // loop-entry：g1 -> 1, g2 -> 2
    mm.write(makeVariableAddr(1), makeConstU64(1));
    mm.write(makeVariableAddr(2), makeConstU64(2));

    auto point = getSourcePoint(0);

    // expr = Var(g1, point) + Var(g2, point)
    auto aVar = makeVariable(1, point);
    auto bVar = makeVariable(2, point);
    auto expr = makeNotNull(makeAdd(std::move(aVar), std::move(bVar)));

    substituteSymbols(expr, *path, point);

    auto expected = makeAdd(makeConstU64(1), makeConstU64(2));
    ASSERT_EQ(*expr, *expected);
}

TEST_F(SubstituteTest, SymbolAddrResolvedBaseAndOffsetApplied) {
    auto originAddr = makeVariableAddr(3);
    auto realAddr   = makeSimpleSymbolAddr(4);
    mm.write(originAddr, realAddr.clone());

    // Var(g5) = 3
    mm.write(makeVariableAddr(5), makeConstU64(3));

    auto point = getSourcePoint(0);

    // symAddr: base=origin(g3), offset=(Var(g5,point) + 4), from=point
    auto vVar   = makeVariable(5, point);
    auto offset = makeAdd(std::unique_ptr<SymbolicExpr>(vVar.release()), makeConstU64(4));
    auto sym    = makeRangeAddr(/*origin id*/ 3, std::move(offset), nullptr, point);

    // expect: realAddr (g4) + 7
    auto expected = makePointAddr(/*real id*/ 4, /*off*/ 7).simplifiedExpr();

    auto out = getSubstitutedAddr(sym, *path, point)->simplifiedExpr();
    ASSERT_EQ(*out, *expected);
}

TEST_F(SubstituteTest, SymbolAddrUnresolvedReturnsClone) {
    auto sym = makeSimpleSymbolAddr(/*origin id*/ 6);

    auto out = getSubstitutedAddr(sym, *path, defaultPoint);

    ASSERT_EQ(*out, sym);
}

TEST_F(SubstituteTest, NonSymbolAddrIsCloned) {
    auto varAddr = makeVariableAddr(7);

    auto out = getSubstitutedAddr(varAddr, *path, defaultPoint);

    ASSERT_EQ(*out, varAddr);
}

TEST_F(SubstituteTest, FromPointMismatchReturnsUnchangedSymbolAddr) {
    auto originAddr = makeVariableAddr(8);
    auto realAddr   = makeSimpleSymbolAddr(9);
    mm.write(originAddr, realAddr.clone());

    auto point = getSourcePoint(0);
    auto sym   = makeRangeAddr(/*origin id*/ 8, makeConstU64(1), nullptr, point);

    auto otherPoint = getSourcePoint(1);
    auto out        = getSubstitutedAddr(sym, *path, otherPoint);

    ASSERT_EQ(*out, sym);
}

TEST_F(SubstituteTest, ResolvedValueNotAddressShouldError) {
    auto originAddr = makeVariableAddr(10);
    mm.write(originAddr, makeConstU64(5));

    auto point = getSourcePoint(0);
    auto sym   = makeRangeAddr(/*origin id*/ 10, makeConstU64(0), nullptr, point);

    ASSERT_DEATH(getSubstitutedAddr(sym, *path, point), "");
    SUCCEED();
}