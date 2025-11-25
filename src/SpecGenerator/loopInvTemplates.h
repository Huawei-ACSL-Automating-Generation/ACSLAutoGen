// src/SpecGenerator/loopInvTemplates.h

#ifndef __ACSLG_SRC_SPECGENERATOR_LOOPINVTEMPLATES_H__
#define __ACSLG_SRC_SPECGENERATOR_LOOPINVTEMPLATES_H__

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
        0 <= j < ${index} && ${m} == ${array}[j];
    loop invariant 0 <= ${index} <= ${n};)";

    // Placeholders: n, index, array, m.
    const StringTemplate FIND_MAX_LOOP_WITH_OTHER_BOUND = R"(loop invariant \forall integer j;
        0 <= j < ${index} ==> ${m} >= ${array}[j];
    loop invariant \exists integer j;
        0 <= j < ${index} && ${m} == ${array}[j];
    loop invariant 0 <= ${index} <= ${n};)";

    // Placeholders: n, index, array, m.
    const StringTemplate FIND_MIN_LOOP_WITH_VAR_BOUND = R"(loop invariant \forall integer j;
        0 <= j < ${index} ==> ${m} <= ${array}[j];
    loop invariant \exists integer j;
        0 <= j < ${index} && ${m} == ${array}[j]);
    loop invariant 0 <= ${index} < ${n};)";

    // Placeholders: n, index, array, m.
    const StringTemplate FIND_MIN_LOOP_WITH_OTHER_BOUND = R"(loop invariant \forall integer j;
        0 <= j < ${index} ==> ${m} <= ${array}[j];
    loop invariant \exists integer j;
        0 <= j < ${index} && ${m} == ${array}[j]);
    loop invariant 0 <= ${index} < ${n};)";
} // namespace acslg::spec_generator

#endif