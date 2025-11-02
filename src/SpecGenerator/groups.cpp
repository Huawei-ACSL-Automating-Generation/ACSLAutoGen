// src/SpecGenerator/groups.cpp

#include "specGenerator.h"

namespace acslg::spec_generator {
    REGISTER_ACSL_GROUP(EmptyGroup);
    REGISTER_ACSL_GROUP(DefaultFunctionContract, "assigns", "poststate");
    REGISTER_ACSL_GROUP(DefaultLoopInfo, "SetEntryAndCurrent", "setPatterns", "setIndex");
    REGISTER_ACSL_GROUP(ComplexLoopInfo);
    REGISTER_ACSL_GROUP(DefaultPathInsensitiveLoopInv,
                        "checkAndDumpLoopInfo",
                        "loopAssigns",
                        "paradigmMaxMin",
                        "loopVariant");
    REGISTER_ACSL_GROUP(DefaultPathSensitiveLoopInv, "StInGXPlugin")
    REGISTER_ACSL_GROUP(ComplexPathInsensitiveLoopInv,
                        "checkAndDumpLoopInfo",
                        "loopAssigns",
                        "loopVariant");
    REGISTER_ACSL_GROUP(ComplexPathSensitiveLoopInv)
} // namespace acslg::spec_generator