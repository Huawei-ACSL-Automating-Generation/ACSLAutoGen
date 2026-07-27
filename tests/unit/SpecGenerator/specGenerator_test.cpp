// tests/unit/SpecGenerator/specGenerator_test.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "testHelper.h"

using namespace std;
using namespace clang;

namespace acslg::test::unit::spec_generator {
    using namespace acslg::spec_generator;
    using namespace acslg::analyzer;
    using namespace acslg::analyzer::symbolic;
    using namespace acslg::utils;
    using namespace utils;

    TEST_F(FixtureWithCode, PostInfoCopyCopiesExpressionsThroughCurrentFactory) {
        ExprFactory sourceFactory;
        ExprFactory targetFactory;
        ExprFactoryScope sourceScope(sourceFactory);

        using enum BinaryOp;
        auto condHandle  = binaryHandle(sourceFactory, literalHandle(sourceFactory, int64_t{1}),
                                        LessThan, literalHandle(sourceFactory, int64_t{2}));
        auto retHandle   = binaryHandle(sourceFactory, literalHandle(sourceFactory, int64_t{3}),
                                        Add, literalHandle(sourceFactory, int64_t{4}));
        auto valueHandle = binaryHandle(sourceFactory, literalHandle(sourceFactory, int64_t{5}),
                                        Multiply, literalHandle(sourceFactory, int64_t{6}));
        auto addr       = facadeAddressBox(makeVariableAddr(0));
        auto addrHandle = facadeHandle(addr);

        PostPIInfo piInfo;
        piInfo.memoryMap.emplace(addr, facadeExpr(sourceFactory, valueHandle));
        piInfo.pathConds.emplace(facadeExpr(sourceFactory, condHandle));

        PostMemoryMap memoryMap;
        memoryMap.emplace(addr, facadeExpr(sourceFactory, valueHandle));
        PathConditions pathConds;
        pathConds.emplace(facadeExpr(sourceFactory, condHandle));
        PostPSInfo psInfo(std::move(memoryMap), std::move(pathConds), Path::PathState::Return,
                          facadeExpr(sourceFactory, retHandle));

        ExprFactoryScope targetScope(targetFactory);
        PostPIInfo copiedPI{piInfo};
        ASSERT_EQ(copiedPI.memoryMap.size(), 1);
        auto copiedPIMemoryIt = copiedPI.memoryMap.find(addr);
        ASSERT_NE(copiedPIMemoryIt, copiedPI.memoryMap.end());
        EXPECT_NE(facadeHandle(copiedPIMemoryIt->first), addrHandle);
        EXPECT_TRUE(facadeHandle(copiedPIMemoryIt->first).structurallyEqual(addrHandle));
        EXPECT_EQ(&copiedPIMemoryIt->second.factory(), &targetFactory);
        EXPECT_NE(facadeHandle(copiedPIMemoryIt->second), valueHandle);
        EXPECT_TRUE(facadeHandle(copiedPIMemoryIt->second).structurallyEqual(valueHandle));
        ASSERT_EQ(copiedPI.pathConds.size(), 1);
        EXPECT_NE(facadeHandle(*copiedPI.pathConds.begin()), condHandle);
        EXPECT_TRUE(facadeHandle(*copiedPI.pathConds.begin()).structurallyEqual(condHandle));

        PostPSInfo copiedPS{psInfo};
        ASSERT_EQ(copiedPS.memoryMap.size(), 1);
        auto copiedPSMemoryIt = copiedPS.memoryMap.find(addr);
        ASSERT_NE(copiedPSMemoryIt, copiedPS.memoryMap.end());
        EXPECT_NE(facadeHandle(copiedPSMemoryIt->first), addrHandle);
        EXPECT_TRUE(facadeHandle(copiedPSMemoryIt->first).structurallyEqual(addrHandle));
        EXPECT_EQ(&copiedPSMemoryIt->second.factory(), &targetFactory);
        EXPECT_NE(facadeHandle(copiedPSMemoryIt->second), valueHandle);
        EXPECT_TRUE(facadeHandle(copiedPSMemoryIt->second).structurallyEqual(valueHandle));
        ASSERT_EQ(copiedPS.pathConds.size(), 1);
        EXPECT_NE(facadeHandle(*copiedPS.pathConds.begin()), condHandle);
        EXPECT_TRUE(facadeHandle(*copiedPS.pathConds.begin()).structurallyEqual(condHandle));
        ASSERT_TRUE(copiedPS.returnExpr);
        EXPECT_EQ(&copiedPS.returnExpr->factory(), &targetFactory);
        EXPECT_NE(facadeHandle(*copiedPS.returnExpr), retHandle);
        EXPECT_TRUE(facadeHandle(*copiedPS.returnExpr).structurallyEqual(retHandle));
    }

} // namespace acslg::test::unit::spec_generator
