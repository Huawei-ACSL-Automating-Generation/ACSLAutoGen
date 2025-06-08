// src/SpecGenerator/groups.cpp

#include "specGenerator.h"
#include "macros.h"

REGISTER_ACSL_GROUP(DefaultFunctionContract, "assigns", "result");
REGISTER_ACSL_GROUP(DefaultLoopInfo, "setLoopEntry", "setPatterns", "setIndex");
REGISTER_ACSL_GROUP(DefaultLoopInvariant, "dumpLoopInfo");