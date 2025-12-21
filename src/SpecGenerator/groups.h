/**
 * @file groups.h
 * @brief Declares plugin group name constants used to configure ACSL generation.
 */

#ifndef __ACSLG_SRC_SPECGENERATOR_GROUPS_H__
#define __ACSLG_SRC_SPECGENERATOR_GROUPS_H__

namespace acslg::spec_generator {
    inline constexpr auto DEFAULT_FUNC_CONTRACT_PLUGINS = "DefaultFunctionContract";
    inline constexpr auto DEFAULT_LOOP_INFO_PLUGINS     = "DefaultLoopInfo";
    inline constexpr auto COMPLEX_LOOP_INFO_PLUGINS     = "ComplexLoopInfo";
    inline constexpr auto DEFAULT_PATH_INSENSITIVE_LOOP_INV_PLUGINS =
        "DefaultPathInsensitiveLoopInv";
    inline constexpr auto DEFAULT_PATH_SENSITIVE_LOOP_INV_PLUGINS = "DefaultPathSensitiveLoopInv";
    inline constexpr auto COMPLEX_PATH_INSENSITIVE_LOOP_INV_PLUGINS =
        "ComplexPathInsensitiveLoopInv";
    inline constexpr auto COMPLEX_PATH_SENSITIVE_LOOP_INV_PLUGINS = "ComplexPathSensitiveLoopInv";
} // namespace acslg::spec_generator

#endif
