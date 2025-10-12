// src/SpecGenerator/loopInvTemplates.h

#ifndef LOOP_INV_TEMPLATES_H
#define LOOP_INV_TEMPLATES_H

#include "stringTemplate.h"

namespace acslg::spec_generator {
    /*------------------------------------------------------------*/
    /*--Spection should start with no space and end with no \n !--*/
    /*------------------------------------------------------------*/

    // TODO: some typical loop invariant patterns.

    // Placeholders: n, index, array, m.
    const StringTemplate FIND_MAX_LOOP_WITH_VAR_BOUND = R"(loop invariant \forall integer j;
        0 <= j < ${index} ==> ${m} >= ${array}[j];
    loop invariant \exists integer j;
        0 <= j < ${index} ==> (\valid(${array} + j) && ${array}[j] == ${m});
    loop invariant 0 <= ${index} < ${n};
    loop invariant ${array} == \at(${array}, Pre) && ${n} == \at(${n}, Pre);
    loop invariant \valid(${array} + (0..${n}-1));)";

    // Placeholders: n, index, array, m.
    const StringTemplate FIND_MAX_LOOP_WITH_OTHER_BOUND = R"(loop invariant \forall integer j;
        0 <= j < ${index} ==> ${m} >= ${array}[j];
    loop invariant \exists integer j;
        0 <= j < ${index} ==> (\valid(${array} + j) && ${array}[j] == ${m});
    loop invariant 0 <= ${index} < ${n};
    loop invariant ${array} == \at(${array}, Pre);
    loop invariant \valid(${array} + (0..${n}-1));)";

    // Placeholders: n, index, array, m.
    const StringTemplate FIND_MIN_LOOP_WITH_VAR_BOUND = R"(loop invariant \forall integer j;
        0 <= j < ${index} ==> ${m} <= ${array}[j];
    loop invariant \exists integer j;
        0 <= j < ${index} ==> (\valid(${array} + j) && ${array}[j] == ${m});
    loop invariant 0 <= ${index} < ${n};
    loop invariant ${array} == \at(${array}, Pre) && ${n} == \at(${n}, Pre);
    loop invariant \valid(${array} + (0..${n}-1));)";

    // Placeholders: n, index, array, m.
    const StringTemplate FIND_MIN_LOOP_WITH_OTHER_BOUND = R"(loop invariant \forall integer j;
        0 <= j < ${index} ==> ${m} <= ${array}[j];
    loop invariant \exists integer j;
        0 <= j < ${index} ==> (\valid(${array} + j) && ${array}[j] == ${m});
    loop invariant 0 <= ${index} < ${n};
    loop invariant ${array} == \at(${array}, Pre);
    loop invariant \valid(${array} + (0..${n}-1));)";
} // namespace acslg::spec_generator

#endif