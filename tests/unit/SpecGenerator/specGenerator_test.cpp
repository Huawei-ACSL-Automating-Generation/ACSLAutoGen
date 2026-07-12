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
        ExprFactory factory;
        ExprFactoryScope scope(factory);

        using enum BinaryOpExpr::Operator;
        auto condHandle = factory.binary(factory.literal(int64_t{1}), LessThan,
                                         factory.literal(int64_t{2}));
        auto retHandle = factory.binary(factory.literal(int64_t{3}), Add,
                                        factory.literal(int64_t{4}));
        auto valueHandle = factory.binary(factory.literal(int64_t{5}), Multiply,
                                          factory.literal(int64_t{6}));
        AddressBox addr{makeVariableAddr(0)};

        PostPIInfo piInfo;
        piInfo.memoryMap.emplace(addr, valueHandle);
        piInfo.pathConds.emplace(condHandle);

        PostPIInfo copiedPI{piInfo};
        ASSERT_EQ(copiedPI.memoryMap.size(), 1);
        auto copiedPIMemoryIt = copiedPI.memoryMap.find(addr);
        ASSERT_NE(copiedPIMemoryIt, copiedPI.memoryMap.end());
        EXPECT_EQ(copiedPIMemoryIt->second, valueHandle);
        EXPECT_EQ(copiedPIMemoryIt->second.get(), valueHandle.get());
        ASSERT_EQ(copiedPI.pathConds.size(), 1);
        EXPECT_EQ(*copiedPI.pathConds.begin(), condHandle);
        EXPECT_EQ(copiedPI.pathConds.begin()->get(), condHandle.get());

        PostMemoryMap memoryMap;
        memoryMap.emplace(addr, valueHandle);
        PathConditions pathConds;
        pathConds.emplace(condHandle);
        PostPSInfo psInfo(std::move(memoryMap), std::move(pathConds), Path::PathState::Return,
                          retHandle);

        PostPSInfo copiedPS{psInfo};
        ASSERT_EQ(copiedPS.memoryMap.size(), 1);
        auto copiedPSMemoryIt = copiedPS.memoryMap.find(addr);
        ASSERT_NE(copiedPSMemoryIt, copiedPS.memoryMap.end());
        EXPECT_EQ(copiedPSMemoryIt->second, valueHandle);
        EXPECT_EQ(copiedPSMemoryIt->second.get(), valueHandle.get());
        ASSERT_EQ(copiedPS.pathConds.size(), 1);
        EXPECT_EQ(*copiedPS.pathConds.begin(), condHandle);
        EXPECT_EQ(copiedPS.pathConds.begin()->get(), condHandle.get());
        ASSERT_TRUE(copiedPS.returnExpr);
        EXPECT_EQ(copiedPS.returnExpr.value(), retHandle);
        EXPECT_EQ(copiedPS.returnExpr.value().get(), retHandle.get());
    }

} // namespace acslg::test::unit::spec_generator
