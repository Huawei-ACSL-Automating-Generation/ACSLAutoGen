/* Force-ignore openHiTLS error push macro for analysis experiments. */
#ifndef ACSL_BSL_ERR_PUSH_ERROR_IGNORE_H
#define ACSL_BSL_ERR_PUSH_ERROR_IGNORE_H

/*
 * Include the original header first so its include guard is set, then override
 * the macro to a no-op for all following translation unit code.
 */
#include "bsl_err_internal.h"

#undef BSL_ERR_PUSH_ERROR
#define BSL_ERR_PUSH_ERROR(e) ((void)0)

#endif
