// tests/unit/SpecGenerator/templates_test.cpp

#include <gtest/gtest.h>
#include <string>
#include "stringTemplate.h"
#include "funcSpecTemplates.h"
#include "loopInvTemplates.h"
#include "utilityTemplates.h"

using namespace std;

namespace acslg::test::unit::spec_generator {
    using namespace acslg::spec_generator;

    //     TEST(TemplatesTest, FindMax) {
    //         auto funcSpecTempl = ACSL_HEAD + "    " + FIND_MAX_FUNC + "\n" + ACSL_END;
    //         auto loopInvTempl  = ACSL_HEAD + "    " + FIND_MAX_LOOP_WITH_VAR_BOUND + "\n" +
    //         ACSL_END; NameMap mapping    = {{"n", "size"}, {"index", "it"}, {"m", "res"},
    //         {"array", "p"}};

    //         EXPECT_EQ(funcSpecTempl.to_string(mapping), string(R"(/*@
    //     requires size > 0 && \valid(p + (0..size-1));
    //     ensures \forall int i; 0 <= i <= size-1 ==> \result >= p[i];
    //     ensures \exists int e; 0 <= e <= size-1 && \result == p[e];
    // */
    // )"));

    //         EXPECT_EQ(loopInvTempl.to_string(mapping), string(R"(/*@
    //     loop invariant \forall integer j;
    //         0 <= j < it ==> res >= p[j];
    //     loop invariant \exists integer j;
    //         0 <= j < it ==> (\valid(p + j) && p[j] == res);
    //     loop invariant 0 <= it < size;
    //     loop invariant p == \at(p, Pre) && size == \at(size, Pre);
    //     loop invariant \valid(p + (0..size-1));
    // */
    // )"));
    // }
} // namespace acslg::test::unit::spec_generator