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

    TEST(PostInfoCopyTest, CopiesExpressionsThroughCurrentFactory) {
        ExprFactory factory;
        ExprFactoryScope scope(factory);

        using enum BinaryOpExpr::Operator;
        auto condHandle = factory.binary(factory.literal(int64_t{1}), LessThan,
                                         factory.literal(int64_t{2}));
        auto retHandle = factory.binary(factory.literal(int64_t{3}), Add,
                                        factory.literal(int64_t{4}));

        PostPIInfo piInfo;
        piInfo.pathConds.emplace(condHandle);

        PostPIInfo copiedPI{piInfo};
        ASSERT_EQ(copiedPI.pathConds.size(), 1);
        EXPECT_EQ(*copiedPI.pathConds.begin(), condHandle);
        EXPECT_EQ(copiedPI.pathConds.begin()->get(), condHandle.get());

        PathConditions pathConds;
        pathConds.emplace(condHandle);
        PostPSInfo psInfo({}, std::move(pathConds), Path::PathState::Return,
                          factory.cloneExpr(retHandle));

        PostPSInfo copiedPS{psInfo};
        ASSERT_EQ(copiedPS.pathConds.size(), 1);
        EXPECT_EQ(*copiedPS.pathConds.begin(), condHandle);
        EXPECT_EQ(copiedPS.pathConds.begin()->get(), condHandle.get());
        ASSERT_TRUE(copiedPS.returnExpr);
        EXPECT_EQ(factory.importExpr(*copiedPS.returnExpr.value()), retHandle);
    }

} // namespace acslg::test::unit::spec_generator
