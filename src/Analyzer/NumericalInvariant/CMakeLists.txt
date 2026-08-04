add_library(
  NumericalInvariantPrototypeLib STATIC
  transitionModel.cpp
  transitionModelExtractor.cpp
  polynomialRecurrence.cpp
  clauseCombiner.cpp
  invariantEmitter.cpp
  clauseProvider.cpp
  clauseSynthesis.cpp
  clause2InvPrototype.cpp)

target_include_directories(
  NumericalInvariantPrototypeLib
  PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(
  NumericalInvariantPrototypeLib
  PUBLIC AnalyzerLib
  PRIVATE ACSLG_compiler_flags
          ${GMP_LIBRARY}
          ${GMPXX_LIBRARY})

llvm_map_components_to_libnames(numerical_invariant_llvm_libs support)
target_link_libraries(NumericalInvariantPrototypeLib PRIVATE ${numerical_invariant_llvm_libs})

option(ACSLG_ENABLE_Z3_PROTOTYPE "Enable the Z3-backed invariant prototype" ON)
if(ACSLG_ENABLE_Z3_PROTOTYPE)
  find_path(Z3_INCLUDE_DIR NAMES z3++.h)
  find_library(Z3_LIBRARY NAMES z3)
endif()

if(ACSLG_ENABLE_Z3_PROTOTYPE AND Z3_INCLUDE_DIR AND Z3_LIBRARY)
  set(ACSLG_NUMERICAL_INVARIANT_Z3_AVAILABLE
      TRUE
      CACHE INTERNAL "Whether the numerical invariant implementation has Z3")
  target_include_directories(NumericalInvariantPrototypeLib PRIVATE ${Z3_INCLUDE_DIR})
  target_link_libraries(NumericalInvariantPrototypeLib PRIVATE ${Z3_LIBRARY})
  target_compile_definitions(
    NumericalInvariantPrototypeLib
    PUBLIC ACSLG_NUMERICAL_INVARIANT_HAS_Z3=1)
  message(STATUS "Building numerical invariant prototype with Z3")
else()
  set(ACSLG_NUMERICAL_INVARIANT_Z3_AVAILABLE
      FALSE
      CACHE INTERNAL "Whether the numerical invariant implementation has Z3")
  target_compile_definitions(
    NumericalInvariantPrototypeLib
    PUBLIC ACSLG_NUMERICAL_INVARIANT_HAS_Z3=0)
  message(STATUS "Building numerical invariant prototype without Z3")
endif()
