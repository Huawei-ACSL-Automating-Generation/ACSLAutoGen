// tests/specGenerator/stringTemplate_test.cpp

#include <gtest/gtest.h>
#include "stringTemplate.h"
#include <unordered_map>
#include <string>

using namespace std;

TEST(StringTemplateTest, NoPlaceholder)
{
    StringTemplate st("Hello, World!");
    EXPECT_EQ(st.getPlaceholderNum(), 0);

    EXPECT_EQ(st.to_string(), "Hello, World!");
}

TEST(StringTemplateTest, SinglePlaceholder)
{
    StringTemplate st("Hello, ${name}!");
    EXPECT_EQ(st.getPlaceholderNum(), 1);

    NameMap mapping;
    mapping["name"] = "Alice";

    EXPECT_EQ(st.to_string(mapping), "Hello, Alice!");
}

TEST(StringTemplateTest, Append)
{
    StringTemplate st_1("Hello, ");
    StringTemplate st_2("${name_1}!");

    st_1.append(st_2);

    EXPECT_EQ(st_1.getPlaceholderNum(), st_2.getPlaceholderNum());

    NameMap mapping;
    mapping["name_1"] = "Alice";

    EXPECT_EQ(st_1.to_string(mapping), "Hello, Alice!");

    st_1.append(" and ${name_2}!"_st);
    mapping["name_2"] = "Rabbit";

    EXPECT_EQ(st_1.to_string(mapping), "Hello, Alice! and Rabbit!");
}

TEST(StringTemplateTest, OperatorPlus)
{
    StringTemplate st_1("Hello, ");
    StringTemplate st_2("${name_1}!");

    StringTemplate combined_1 = st_1 + st_2;

    EXPECT_EQ(combined_1.getPlaceholderNum(), st_2.getPlaceholderNum());

    NameMap mapping;
    mapping["name_1"] = "Alice";

    EXPECT_EQ(combined_1.to_string(mapping), "Hello, Alice!");

    auto combined_2 =
        st_1 + st_2 + " and ${name_2}!"_st + " and ${name_3}!"_st + " and ${name_4}!"_st;
    mapping["name_2"] = "March";
    mapping["name_3"] = "Hatter";
    mapping["name_4"] = "Mouse";

    EXPECT_EQ(combined_2.to_string(mapping), "Hello, Alice! and March! and Hatter! and Mouse!");
}

TEST(StringTemplateTest, Remap)
{
    auto st_1 = "Hello, ${name}!"_st;

    EXPECT_EQ(st_1.remap(NameMap({{"name", "name_1"}})), 1);

    NameMap mapping;
    mapping["name_1"] = "Alice";

    EXPECT_EQ(st_1.to_string(mapping), "Hello, Alice!");

    auto st_2 = " and ${name}!"_st;

    EXPECT_EQ(st_2.remap(NameMap({{"name", "name_2"}})), 1);

    auto combine      = st_1 + st_2;
    mapping["name_2"] = "Rabbit";

    EXPECT_EQ(combine.to_string(mapping), "Hello, Alice! and Rabbit!");

    EXPECT_EQ(combine.remap(NameMap({{"name_1", "name"}, {"name_2", "name"}})), 2);

    mapping.clear();
    mapping["name"] = "Cat";

    EXPECT_EQ(combine.to_string(mapping), "Hello, Cat! and Cat!");
}

TEST(StringTemplateTest, Initialize)
{
    auto st = "$}$Hello, $${name_1}! and ${${name_2}}! and ${}! and ${"_st;
    EXPECT_EQ(st.getPlaceholderNum(), 3);

    EXPECT_EQ(st.to_string(NameMap({{"name_1", "Alice"}, {"${name_2", "Rabbit"}, {"", "Cat"}})),
        "$}$Hello, $Alice! and Rabbit}! and Cat! and ${");
}

TEST(StringTemplateTest, OverLapReMap)
{
    auto st = "Hello, ${name_1}! and ${name_2}! and ${name_3}!"_st;
    EXPECT_EQ(
        st.remap(NameMap({{"name_1", "name_2"}, {"name_2", "name_1"}, {"name_3", "name_1"}})), 3);

    EXPECT_EQ(st.to_string(NameMap({{"name_1", "Cat"}, {"name_2", "Alice"}})),
        "Hello, Alice! and Cat! and Cat!");
}

TEST(StringTemplateTest, ReMapAndWrongMapping)
{
    auto st = "Hello, ${name_1}! and ${name_2}! and ${name_3}!"_st;
    EXPECT_EQ(
        st.remap(NameMap{{{"name_1", "name_0"}, {"name_2", "name_1"}, {"name_3", "name_2"}}}), 3);
    EXPECT_EQ(
        st.to_string(NameMap({{"name", "Queen"}, {"name_0", "Alice"}, {"name_3", "Carroll"}})),
        "Hello, Alice! and name_1! and name_2!");
}

TEST(StringTemplateTest, PlusString)
{
    StringTemplate st = "";
    st += "Hello, ";
    st += "${name}"_st + "!";
    EXPECT_EQ(st.to_string(NameMap({{"name", "Alice"}})), "Hello, Alice!");
}