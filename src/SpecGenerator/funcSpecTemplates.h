/**
 * @file funcSpecTemplates.h
 * @brief Holds reusable StringTemplate snippets for common function specifications.
 */

#ifndef __ACSLG_SRC_SPECGENERATOR_FUNCSPECTEMPLATES_H__
#define __ACSLG_SRC_SPECGENERATOR_FUNCSPECTEMPLATES_H__


#include "stringTemplate.h"

namespace acslg::spec_generator {
    /*------------------------------------------------------------*/
    /*--Spection should start with no space and end with no \n !--*/
    /*------------------------------------------------------------*/

    // TODO: some function specifications patterns.

    // Placeholders: n, array
    const StringTemplate FIND_MAX_FUNC = R"(requires ${n} > 0 && \valid(${array} + (0..${n}-1));
    ensures \forall int i; 0 <= i <= ${n}-1 ==> \result >= ${array}[i];
    ensures \exists int e; 0 <= e <= ${n}-1 && \result == ${array}[e];)";
} // namespace acslg::spec_generator

#endif
