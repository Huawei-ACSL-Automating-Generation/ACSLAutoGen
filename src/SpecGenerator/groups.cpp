// src/SpecGenerator/groups.cpp

#include "specGenerator.h"

namespace acslg::spec_generator {
    REGISTER_ACSL_GROUP(EmptyGroup);
    REGISTER_ACSL_GROUP(DefaultFunctionContract, "assigns", "poststate");
    REGISTER_ACSL_GROUP(DefaultLoopInfo, "setLoopEntry", "setPatterns", "setIndex");
    REGISTER_ACSL_GROUP(ComplexLoopInfo);
    REGISTER_ACSL_GROUP(DefaultLoopInvariant,
                        "checkAndDumpLoopInfo",
                        "loopAssigns",
                        "paradigmMaxMin",
                        "StInGXPlugin",
                        "loopVariant");
    REGISTER_ACSL_GROUP(ComplexLoopInvariant, "checkAndDumpLoopInfo", "loopAssigns", "loopVariant");
} // namespace acslg::spec_generator