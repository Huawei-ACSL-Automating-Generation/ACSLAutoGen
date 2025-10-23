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

} // namespace acslg::test::unit::spec_generator