// src/SpecGenerator/groups.cpp

#include "specGenerator.h"
#include "macros.h"

REGISTER_ACSL_GROUP(EmptyGroup);
REGISTER_ACSL_GROUP(DefaultFunctionContract, "assigns", "result" /*, "poststate"*/);
REGISTER_ACSL_GROUP(DefaultLoopInfo, "setLoopEntry", "setPatterns", "setIndex");
REGISTER_ACSL_GROUP(DefaultLoopInvariant,
                    "checkAndDumpLoopInfo",
                    "loopAssigns",
                    "paradigmMaxMin",
                    "StInGXPlugin",
                    "loopVariant");
// REGISTER_ACSL_GROUP(ComplexLoop, "StInGXPlugin");