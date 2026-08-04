/**
 * @file groups.cpp
 * @brief Registers default plugin groups for ACSL generation.
 */
#include "specGenerator.h"

#include <cstdlib>
#include <string_view>

namespace acslg::spec_generator {
    NumericalInvariantMode configuredNumericalInvariantMode() {
        if (const char *value = std::getenv("ACSLG_NUMERICAL_INVARIANTS")) {
            const std::string_view mode{value};
            if (mode == "local")
                return NumericalInvariantMode::Local;
            if (mode == "llm")
                return NumericalInvariantMode::Llm;
            return NumericalInvariantMode::Disabled;
        }

        const char *legacyValue = std::getenv("ACSLG_ENABLE_CLAUSE2INV_PROTOTYPE");
        return legacyValue && std::string_view{legacyValue} == "1"
                   ? NumericalInvariantMode::Llm
                   : NumericalInvariantMode::Disabled;
    }

    bool clause2InvPrototypeEnabled() {
        return configuredNumericalInvariantMode() == NumericalInvariantMode::Llm;
    }

    std::string_view configuredPathInsensitiveLoopInvariantGroup() {
        switch (configuredNumericalInvariantMode()) {
            case NumericalInvariantMode::Disabled: return DEFAULT_PATH_INSENSITIVE_LOOP_INV_PLUGINS;
            case NumericalInvariantMode::Local:
                return VERIFIED_NUMERICAL_PATH_INSENSITIVE_LOOP_INV_PLUGINS;
            case NumericalInvariantMode::Llm:
                return CLAUSE2INV_PROTOTYPE_PATH_INSENSITIVE_LOOP_INV_PLUGINS;
        }
        return DEFAULT_PATH_INSENSITIVE_LOOP_INV_PLUGINS;
    }

    REGISTER_ACSL_GROUP(EmptyGroup);
    REGISTER_ACSL_GROUP(DefaultFunctionContract, "assigns", "poststate");
    REGISTER_ACSL_GROUP(DefaultLoopInfo,
                        "SetEntryAndCurrent",
                        "setPatterns",
                        "setSharedState",
                        "setIndex");
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
    REGISTER_ACSL_GROUP(Clause2InvPrototypePathInsensitiveLoopInv,
                        "checkAndDumpLoopInfo",
                        "loopAssigns",
                        "paradigmMaxMin",
                        "loopVariant",
                        "VerifiedPolynomialInvariantPlugin",
                        "Clause2InvPrototypePlugin");
    REGISTER_ACSL_GROUP(VerifiedNumericalPathInsensitiveLoopInv,
                        "checkAndDumpLoopInfo",
                        "loopAssigns",
                        "paradigmMaxMin",
                        "loopVariant",
                        "VerifiedPolynomialInvariantPlugin");
} // namespace acslg::spec_generator
