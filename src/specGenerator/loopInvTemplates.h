// src/specGenerator/loopInvTemplates.h

#ifndef LOOP_INV_TEMPLATES_H
#define LOOP_INV_TEMPLATES_H

#include <stringTemplate.h>

// TODO: some typical loop invariant patterns.

// Placeholders: n, i, array, max, e.
// Note: need "ghost ${e} = i;" in loop body.
const StringTemplate FIND_MAX_LOOP = R"(
    ghost int ${e} = 0;
    loop invariant \forall integer j;
        0 <= j < ${i} ==> ${max} >= ${array}[j];
    loop invariant \valid(${array} + ${e}) && p[${e}] == ${max};
    loop invariant about_${i}: 0 <= ${i} <= ${n};
    loop invariant 0 <= ${e} < ${n};
    loop invariant ${array} == \at(${array}, Pre) && ${n} == \at(${n}, Pre);
    loop invariant \valid(${array} + (0..${n}-1));
)";

#endif