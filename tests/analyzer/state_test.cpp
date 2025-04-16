// tests/specGenerator/state_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "xmock.h"
#include "state.h"
#include <unordered_map>
#include <string>
#include "ASTExtractor.h"

using ::testing::Return;
using namespace std;

TEST(PathTest, DumpEmptyPath)
{
    Path path;
    EXPECT_NO_THROW(path.dump());
}
