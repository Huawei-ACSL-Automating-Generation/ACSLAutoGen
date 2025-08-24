// src/SpecGenerator/groups.h

#ifndef GROUPS_H
#define GROUPS_H

// // testing...
// #define DEFAULT_FUNC_CONTRACT_PLUGINS "EmptyGroup"
// #define DEFAULT_LOOP_INFO_PLUGINS "EmptyGroup"
// #define DEFAULT_LOOP_INVARIANT_PLUGINS "EmptyGroup"

#ifndef DEFAULT_FUNC_CONTRACT_PLUGINS
#define DEFAULT_FUNC_CONTRACT_PLUGINS "DefaultFunctionContract"
#endif

#ifndef DEFAULT_LOOP_INFO_PLUGINS
#define DEFAULT_LOOP_INFO_PLUGINS "DefaultLoopInfo"
#endif

#ifndef DEFAULT_LOOP_INVARIANT_PLUGINS
#define DEFAULT_LOOP_INVARIANT_PLUGINS "DefaultLoopInvariant"
#endif

#endif