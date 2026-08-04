# tests/CMakeLists.txt

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.16.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  foreach(_tgt IN ITEMS gtest gmock gtest_main)
    target_compile_options(${_tgt} PRIVATE
      -Wno-deprecated-declarations
    )
  endforeach()
endif()

include(GoogleTest)

# Use 'test_all' instead of 'TestAll' for better distinction.
add_executable(test_all
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/SpecGenerator/stringTemplate_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/SpecGenerator/templates_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/SpecGenerator/loopInfoPlugins_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/SpecGenerator/loopInvariantPlugins_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/SpecGenerator/functionContractPlugin_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/SpecGenerator/specGenerator_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/Analyzer/state_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/Analyzer/expr_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/Analyzer/expr_public_api_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/Analyzer/transitionModelExtractor_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/Analyzer/polynomialRecurrence_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/Analyzer/clauseCombiner_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/Analyzer/invariantEmitter_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/Analyzer/clauseProvider_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/Analyzer/clauseSynthesis_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/integration_test.cpp
)

llvm_map_components_to_libnames(llvm_libs support)

target_link_libraries(test_all PRIVATE
  gtest
  gmock
  gtest_main
  ACSLG_compiler_flags
  clangTooling
  clangFrontend
  clangAST
  clangBasic
  TestUtilsLib
  ${llvm_libs}
  AnalyzerLib
  NumericalInvariantPrototypeLib
  SpecGeneratorLib
  ContextLib
)

gtest_add_tests(TARGET test_all
                TEST_PREFIX test_all::
                TEST_LIST test_all_tests)

if(ACSLG_NUMERICAL_INVARIANT_Z3_AVAILABLE)
  find_program(FRAMA_C_EXECUTABLE NAMES frama-c)
  add_test(
    NAME numerical_invariant_benchmarks
    COMMAND
      ${CMAKE_COMMAND}
      "-DACSLG_EXECUTABLE=$<TARGET_FILE:ACSLG>"
      "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/numerical-invariant-benchmarks"
      "-DFRAMA_C_EXECUTABLE=${FRAMA_C_EXECUTABLE}"
      -P
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/numericalInvariantBenchmark.cmake)
  set_tests_properties(numerical_invariant_benchmarks
                       PROPERTIES TIMEOUT 120)
endif()

add_subdirectory(testUtils)
