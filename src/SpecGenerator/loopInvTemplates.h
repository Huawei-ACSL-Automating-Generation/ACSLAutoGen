// src/SpecGenerator/loopInvTemplates.h

#ifndef LOOP_INV_TEMPLATES_H
#define LOOP_INV_TEMPLATES_H

#include "stringTemplate.h"

/*------------------------------------------------------------*/
/*--Spection should start with no space and end with no \n !--*/
/*------------------------------------------------------------*/

// TODO: some typical loop invariant patterns.

// Placeholders: n, index, array, max.
const StringTemplate FIND_MAX_LOOP_WITH_VAR_BOUND = R"(loop invariant \forall integer j;
        0 <= j < ${index} ==> ${max} >= ${array}[j];
    loop invariant \exists integer j;
        0 <= j < ${index} ==> (\valid(${array} + j) && ${array}[j] == ${max});
    loop invariant 0 <= ${index} < ${n};
    loop invariant ${array} == \at(${array}, Pre) && ${n} == \at(${n}, Pre);
    loop invariant \valid(${array} + (0..${n}-1));)";

// Placeholders: n, index, array, max.
const StringTemplate FIND_MAX_LOOP_WITH_OTHER_BOUND = R"(loop invariant \forall integer j;
        0 <= j < ${index} ==> ${max} >= ${array}[j];
    loop invariant \exists integer j;
        0 <= j < ${index} ==> (\valid(${array} + j) && ${array}[j] == ${max});
    loop invariant 0 <= ${index} < ${n};
    loop invariant ${array} == \at(${array}, Pre);
    loop invariant \valid(${array} + (0..${n}-1));)";

#endif