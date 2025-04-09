// tests/SpecGenerator/stringTemplate.cpp

#include <gtest/gtest.h>
#include "stringTemplate.h"
#include <unordered_map>
#include <string>

using namespace std;

TEST(StringTemplateTest, NoPlaceholderTest)
{
    StringTemplate st("Hello, World!");
    EXPECT_EQ(st.getPlaceholderNum(), 0);

    EXPECT_EQ(st(), "Hello, World!");
}

TEST(StringTemplateTest, SinglePlaceholderTest)
{
    StringTemplate st("Hello, ${name}!");
    EXPECT_EQ(st.getPlaceholderNum(), 1);

    unordered_map<string, string> mapping;
    mapping["name"] = "Alice";

    EXPECT_EQ(st(mapping), "Hello, Alice!");
}

TEST(StringTemplateTest, AppendAndOperatorPlusTest)
{
    StringTemplate st1("Hello, ");
    StringTemplate st2("${name}!");

    StringTemplate combined = st1 + st2;

    EXPECT_EQ(combined.getPlaceholderNum(), st2.getPlaceholderNum());

    unordered_map<string, string> mapping;
    mapping["name"] = "Bob";

    EXPECT_EQ(combined(mapping), "Hello, Bob!");
}
