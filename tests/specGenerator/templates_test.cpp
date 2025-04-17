// tests/specGenerator/templates_test.cpp

#include <gtest/gtest.h>
#include <unordered_map>
#include <string>
#include "stringTemplate.h"
#include "funcSpecTemplates.h"
#include "loopInvTemplates.h"
#include "utilityTemplates.h"

using namespace std;

TEST(TemplatesTest, FindMax)
{
    auto funcSpecTempl = ACSL_HEAD + FIND_MAX_FUNC + ACSL_END;
    auto loopInvTempl = ACSL_HEAD + FIND_MAX_LOOP + ACSL_END;
    NameMap mapping = {{"n", "size"}, {"index", "it"}, {"max", "res"}, {"array", "p"}};

    EXPECT_EQ(funcSpecTempl(mapping), string(R"(/*@ 
    requires size > 0 && \valid(p + (0..size-1));
    ensures \forall int i; 0 <= i <= size-1 ==> \result >= p[i];
    ensures \exists int e; 0 <= e <= size-1 && \result == p[e];
*/)"));

    EXPECT_EQ(loopInvTempl(mapping), string(R"(/*@ 
    loop invariant \forall integer j;
        0 <= j < it ==> res >= p[j];
    loop invariant \exists integer j;
        0 <= j < it ==> (\valid(p + j) && p[j] == res);
    loop invariant 0 <= it < size;
    loop invariant p == \at(p, Pre) && size == \at(size, Pre);
    loop invariant \valid(p + (0..size-1));
*/)"));
}