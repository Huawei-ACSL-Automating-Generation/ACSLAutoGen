// src/specGenerator/loopInvTemplates.h

#ifndef LOOP_INV_TEMPLATES_H
#define LOOP_INV_TEMPLATES_H

#include "stringTemplate.h"

// TODO: some typical loop invariant patterns.

// Placeholders: n, i, array, max, e.
// Note: need "ghost ${e} = i;" in loop body.
const StringTemplate FIND_MAX_LOOP = R"(
    loop invariant \forall integer j;
        0 <= j < ${index} ==> ${max} >= ${array}[j];
    loop invariant \valid(${array} + ${index}) && p[${index}] == ${max};
    loop invariant about_${i}: 0 <= ${i} <= ${n};
    loop invariant 0 <= ${index} < ${n};
    loop invariant ${array} == \at(${array}, Pre) && ${n} == \at(${n}, Pre);
    loop invariant \valid(${array} + (0..${n}-1));
)";

#endif