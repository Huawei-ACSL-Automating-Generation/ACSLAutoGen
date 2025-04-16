// src/specGenerator/funcSpecTemplates.h

#ifndef FUNC_SPEC_TEMPLATES_H
#define FUNC_SPEC_TEMPLATES_H

#include "stringTemplate.h"

// TODO: some function specifications patterns.

// Placeholders: n, array
const StringTemplate FIND_MAX_FUNC = R"(
    requires ${n} > 0 && \valid(${array} + (0..${n}-1));
    ensures \forall int i; 0 <= i <= ${n}-1 ==> \result >= ${array}[i];
    ensures \exists int e; 0 <= e <= ${n}-1 && \result == ${array}[e];
)";

#endif