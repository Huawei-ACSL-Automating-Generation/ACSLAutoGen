#include "function.h"

std::unique_ptr<ACSLFunction> ACSLFunction::clone() const {
    return std::make_unique<ACSLFunction>(FuncDecl);
}
