// src/SpecGenerator/groups.h

#ifndef __ACSLG_SRC_SPECGENERATOR_GROUPS_H__
#define __ACSLG_SRC_SPECGENERATOR_GROUPS_H__


namespace acslg::spec_generator {
    inline constexpr auto DEFAULT_FUNC_CONTRACT_PLUGINS  = "DefaultFunctionContract";
    inline constexpr auto DEFAULT_LOOP_INFO_PLUGINS      = "DefaultLoopInfo";
    inline constexpr auto COMPLEX_LOOP_INFO_PLUGINS      = "ComplexLoopInfo";
    inline constexpr auto DEFAULT_LOOP_INVARIANT_PLUGINS = "DefaultLoopInvariant";
    inline constexpr auto COMPLEX_LOOP_INVARIANT_PLUGINS = "ComplexLoopInvariant";
} // namespace acslg::spec_generator

#endif