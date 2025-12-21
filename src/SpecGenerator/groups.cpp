/**
 * @file groups.cpp
 * @brief Registers default plugin groups for ACSL generation.
 */
#include "specGenerator.h"

namespace acslg::spec_generator {
    REGISTER_ACSL_GROUP(EmptyGroup);
    REGISTER_ACSL_GROUP(DefaultFunctionContract, "assigns", "poststate");
    REGISTER_ACSL_GROUP(
        DefaultLoopInfo, "SetEntryAndCurrent", "setPatterns", "setSharedState", "setIndex");
    REGISTER_ACSL_GROUP(ComplexLoopInfo);
    REGISTER_ACSL_GROUP(DefaultPathInsensitiveLoopInv,
                        "checkAndDumpLoopInfo",
                        "loopAssigns",
                        "paradigmMaxMin",
                        "loopVariant");
    // Path-sensitive invariants require both search and StInGX plugins to cooperate.
    REGISTER_ACSL_GROUP(DefaultPathSensitiveLoopInv, "paradigmSearch", "StInGXPlugin")
    REGISTER_ACSL_GROUP(ComplexPathInsensitiveLoopInv,
                        "checkAndDumpLoopInfo",
                        "complexLoopAssigns");
    REGISTER_ACSL_GROUP(ComplexPathSensitiveLoopInv, "StInGXPluginForComplexLoop");
} // namespace acslg::spec_generator
