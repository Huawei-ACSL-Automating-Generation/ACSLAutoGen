#include "function.h"

ACSLFunction *ACSLFunction::clone() const { return new ACSLFunction(FuncDecl); }
