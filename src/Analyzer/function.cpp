/**
 * @file function.cpp
 * @brief Implements the ACSLFunction helper utilities.
 */
#include "function.h"
#include <memory>

namespace acslg::analyzer {
    /**
     * @brief Create a new wrapper that points to the same underlying `clang::FunctionDecl`.
     * @return Newly constructed `ACSLFunction` instance.
     */
    std::unique_ptr<ACSLFunction> ACSLFunction::clone() const {
        return std::make_unique<ACSLFunction>(FuncDecl);
    }
} // namespace acslg::analyzer
