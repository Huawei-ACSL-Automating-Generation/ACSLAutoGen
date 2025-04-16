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
    NameMap mapping = {{"n", "size"}, {"i", "it"}, {"e", "pt"}, {"max", "res"}, {"array", "p"}};

    EXPECT_EQ(funcSpecTempl(mapping), string(R"(/*@ 
    requires size > 0 && \valid(p + (0..size-1));
    ensures \forall int i; 0 <= i <= size-1 ==> \result >= p[i];
    ensures \exists int e; 0 <= e <= size-1 && \result == p[e];
*/)"));

    EXPECT_EQ(loopInvTempl(mapping), string(R"(/*@ 
    ghost int pt = 0;
    loop invariant \forall integer j;
        0 <= j < it ==> res >= p[j];
    loop invariant \valid(p + pt) && p[pt] == res;
    loop invariant about_it: 0 <= it <= size;
    loop invariant 0 <= pt < size;
    loop invariant p == \at(p, Pre) && size == \at(size, Pre);
    loop invariant \valid(p + (0..size-1));
*/)"));

    EXPECT_EQ(R"(//@ ghost ${e} = ${i};)"_st(mapping), string(R"(//@ ghost pt = it;)"));
}