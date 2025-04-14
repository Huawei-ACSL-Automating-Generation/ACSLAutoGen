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

class Test
{
  public:
    int foo(int x) { return x; }
};

class MockTest : public Test
{
  public:
    MOCK_NONVIRTUAL_METHOD(int, foo, (int), (), MockTest);
};

TEST(Test, test)
{
    MockTest testA;
    EXPECT_CALL(testA, foo).WillRepeatedly(Return(0));
    EXPECT_EQ(testA.foo(1), 0);
}
