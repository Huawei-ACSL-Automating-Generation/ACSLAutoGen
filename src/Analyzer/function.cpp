#include "function.h"

namespace acslg::analyzer {
    std::unique_ptr<ACSLFunction> ACSLFunction::clone() const {
        return std::make_unique<ACSLFunction>(FuncDecl);
    }
} // namespace acslg::analyzer