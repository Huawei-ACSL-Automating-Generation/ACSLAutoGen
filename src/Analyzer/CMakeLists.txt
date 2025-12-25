# src/Analyzer/CMakeLists.txt

add_library(
  AnalyzerLib STATIC
  analysis.cpp
  state.cpp
  crossTU.cpp
  Symbolic/expr.cpp
  Symbolic/aggregateExpr.cpp
  Symbolic/invariant.cpp
  function.cpp)

target_include_directories(AnalyzerLib PUBLIC ${CMAKE_SOURCE_DIR}/src
                                              ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(AnalyzerLib PUBLIC ${CMAKE_SOURCE_DIR}/src/Stingx)

target_link_libraries(
  AnalyzerLib
  PRIVATE ACSLG_compiler_flags
          UtilsLib
          SpecGeneratorLib
          ContextLib
          lstingx
          clangAST
          clangTooling
          clangFrontend
          ${PPL_LIBRARY}
          ${GMP_LIBRARY}
          ${GMPXX_LIBRARY})

# ------------------------------------------------------------------------------------------
# This entire block of CMake logic is responsible for **automatically
# generating** a `.inc` file (`subclassesOfSymbol.inc`) that lists all direct
# subclasses of the `acslg::analyzer::symbolic::Symbol` class.
#
# The generated `.inc` file can then be `#include`d directly in C++ source code
# (for example, in a `switch` or `llvm::TypeSwitch` statement) to **replace
# manual enumeration of subclasses**. This mechanism is particularly useful for
# implementing LLVM-style RTTI (Run-Time Type Information) patterns, where new
# subclasses can be automatically picked up at build time without needing to
# manually update type lists.
#
# Workflow summary: 1. Collect all headers in the `Symbolic` directory. 2.
# Auto-generate a synthetic translation unit (`__all_headers__.cpp`) that
# includes them all. 3. Compile this synthetic TU into an object target to
# ensure it appears in `compile_commands.json`. 4. Invoke the `emit_subclasses`
# Clang tool to parse this TU, find all direct subclasses of `Symbol`, and
# generate the `.inc` file containing `SUBCLASS(<TypeName>)` macros. 5. Include
# the generated `.inc` in the build so it is available to the main codebase.
# ------------------------------------------------------------------------------------------

# Recursively collect all header files under Symbolic directory (.h and .hpp)
file(GLOB_RECURSE PROJECT_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/Symbolic/*.h
     ${CMAKE_CURRENT_SOURCE_DIR}/Symbolic/*.hpp)

# Define the directory where generated files will be placed
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/Generated)
# Define the path for the auto-generated "umbrella" TU (translation unit) that
# includes all headers
set(UMBRELLA_CPP ${GEN_DIR}/__all_headers__.cpp)
# Ensure the generated directory exists
file(MAKE_DIRECTORY ${GEN_DIR})

# Prepare the content of a CMake script that will generate __all_headers__.cpp
# This script writes a comment header and then appends #include lines for each
# header
set(_GEN_SCRIPT_CONTENT
    "file(WRITE \"${UMBRELLA_CPP}\" \"// auto-generated: include all headers\\n\\n\")\n"
)
foreach(h ${PROJECT_HEADERS})
  # Normalize the path to a CMake-compatible format
  file(TO_CMAKE_PATH "${h}" _H_CANON)
  # Append a line to include the current header file into the generated TU
  string(
    APPEND _GEN_SCRIPT_CONTENT
    "file(APPEND \"${UMBRELLA_CPP}\" \"#include \\\"${_H_CANON}\\\"\\n\")\n")
endforeach()

# Write the generated script content to a file (gen_umbrella.cmake)
set(GEN_SCRIPT ${GEN_DIR}/gen_umbrella.cmake)
file(
  GENERATE
  OUTPUT ${GEN_SCRIPT}
  CONTENT "${_GEN_SCRIPT_CONTENT}")

# Custom command: run the script to generate the umbrella translation unit
# (__all_headers__.cpp)
add_custom_command(
  OUTPUT ${UMBRELLA_CPP}
  COMMAND ${CMAKE_COMMAND} -P ${GEN_SCRIPT}
  DEPENDS ${GEN_SCRIPT} ${PROJECT_HEADERS} # Regenerate if script or any header
                                           # changes
  COMMENT "Generating umbrella TU: ${UMBRELLA_CPP}"
  VERBATIM)

# Create an object library from the generated umbrella TU Purpose: ensure
# __all_headers__.cpp appears in compile_commands.json with full compile options
add_library(__headers_tu OBJECT ${UMBRELLA_CPP})
# Do not build this library as part of the default build (only needed for
# tooling)
set_target_properties(__headers_tu PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Add include directories so the umbrella TU can successfully compile all
# included headers
target_include_directories(
  __headers_tu PRIVATE ${CMAKE_SOURCE_DIR}/src
                       ${CMAKE_CURRENT_SOURCE_DIR}/Symbolic)
# Link necessary compiler flags and utility libraries for the generated TU
target_link_libraries(__headers_tu PRIVATE ACSLG_compiler_flags UtilsLib)

# Define the path for the generated subclass list (.inc file)
set(GEN_INC ${GEN_DIR}/subclassesOfSymbol.inc)

set(TOOL_ENV_TARGET AnalyzerLib)
if(NOT TARGET ${TOOL_ENV_TARGET})
  message(FATAL_ERROR "TOOL_ENV_TARGET '${TOOL_ENV_TARGET}' does not exist.")
endif()

get_target_property(_incs ${TOOL_ENV_TARGET} INCLUDE_DIRECTORIES)
get_target_property(_incs_iface ${TOOL_ENV_TARGET}
                    INTERFACE_INCLUDE_DIRECTORIES)
set(_all_incs ${_incs} ${_incs_iface})

set(EMIT_INCLUDES "")
foreach(dir IN LISTS _all_incs)
  if(dir)
    list(APPEND EMIT_INCLUDES -I"${dir}")
  endif()
endforeach()

set(_llvm_inc_cands ${LLVM_INCLUDE_DIRS} ${LLVM_INCLUDES})
set(_clang_inc_cands ${Clang_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})

foreach(dir IN LISTS _llvm_inc_cands _clang_inc_cands)
  if(dir)
    list(APPEND EMIT_SYS_INCLUDES -isystem"${dir}")
  endif()
endforeach()

get_target_property(_defs ${TOOL_ENV_TARGET} COMPILE_DEFINITIONS)
get_target_property(_defs_iface ${TOOL_ENV_TARGET}
                    INTERFACE_COMPILE_DEFINITIONS)
set(_all_defs ${_defs} ${_defs_iface})

set(EMIT_DEFS "")
foreach(d IN LISTS _all_defs)
  if(d)
    list(APPEND EMIT_DEFS -D${d})
  endif()
endforeach()

get_target_property(_opts ${TOOL_ENV_TARGET} COMPILE_OPTIONS)
get_target_property(_opts_iface ${TOOL_ENV_TARGET} INTERFACE_COMPILE_OPTIONS)
set(_all_opts ${_opts} ${_opts_iface})

set(EMIT_OPTS "")
foreach(opt IN LISTS _all_opts)
  if(opt)
    list(APPEND EMIT_OPTS ${opt})
  endif()
endforeach()

get_target_property(_std ${TOOL_ENV_TARGET} CXX_STANDARD)
if(_std)
  set(EMIT_STD -std=c++${_std})
endif()

# message(STATUS "emit incs: ${EMIT_INCLUDES}") message(STATUS "emit sys :
# ${EMIT_SYS_INCLUDES}") message(STATUS "emit defs: ${EMIT_DEFS}")
# message(STATUS "emit opts: ${EMIT_OPTS}") message(STATUS "emit std :
# ${EMIT_STD}")

set(EXTRA_ARGS "")
foreach(a IN LISTS EMIT_STD EMIT_INCLUDES EMIT_SYS_INCLUDES EMIT_DEFS EMIT_OPTS)
  if(a)
    list(APPEND EXTRA_ARGS "-extra-arg=${a}")
  endif()
endforeach()

# clang tooling needs a valid resource dir so builtin headers (e.g., stddef.h)
# are found
find_program(CLANG_PROG clang)
if(CLANG_PROG)
  execute_process(
    COMMAND ${CLANG_PROG} -print-resource-dir
    OUTPUT_VARIABLE CLANG_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(CLANG_RESOURCE_DIR)
    list(APPEND EXTRA_ARGS "-extra-arg=-resource-dir=${CLANG_RESOURCE_DIR}")
  endif()
endif()

# Custom command: run the emit_subclasses tool to generate the .inc file
add_custom_command(
  OUTPUT ${GEN_INC}
  BYPRODUCTS ${GEN_INC} # Declare this file as a byproduct of the command
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR} # Ensure output
                                                        # directory exists
  COMMAND ${CMAKE_COMMAND} -E copy /dev/null ${GEN_INC} # Create an empty .inc
                                                        # file initially
  COMMAND
    $<TARGET_FILE:emit_subclasses> --base acslg::analyzer::symbolic::Symbol -o
    ${GEN_INC} ${EXTRA_ARGS} ${UMBRELLA_CPP}
    # compile commands
  DEPENDS emit_subclasses __headers_tu ${UMBRELLA_CPP} # Regenerate if tool, TU,
                                                       # or headers change
  COMMENT "Generating subclass list: ${GEN_INC}"
  VERBATIM)

# Create a custom target to explicitly generate the subclass list
add_custom_target(gen_symbolicexpr_inc DEPENDS ${GEN_INC})
# Ensure AnalyzerLib depends on this generation step (so it happens before
# compilation)
add_dependencies(AnalyzerLib gen_symbolicexpr_inc)
# Add the generated directory to include paths so #include
# "subclassesOfSymbol.inc" works
target_include_directories(AnalyzerLib PRIVATE ${GEN_DIR})
