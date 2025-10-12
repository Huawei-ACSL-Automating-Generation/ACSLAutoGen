// src/SpecGenerator/groups.h

#ifndef GROUPS_H
#define GROUPS_H

namespace acslg::spec_generator {
    inline constexpr auto DEFAULT_FUNC_CONTRACT_PLUGINS  = "DefaultFunctionContract";
    inline constexpr auto DEFAULT_LOOP_INFO_PLUGINS      = "DefaultLoopInfo";
    inline constexpr auto COMPLEX_LOOP_INFO_PLUGINS      = "ComplexLoopInfo";
    inline constexpr auto DEFAULT_LOOP_INVARIANT_PLUGINS = "DefaultLoopInvariant";
    inline constexpr auto COMPLEX_LOOP_INVARIANT_PLUGINS = "ComplexLoopInvariant";
} // namespace acslg::spec_generator

#endif