# tests/CMakeLists.txt


include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
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
  ${CMAKE_CURRENT_SOURCE_DIR}/SpecGenerator/stringTemplate_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/SpecGenerator/templates_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Analyzer/state_test.cpp
)

llvm_map_components_to_libnames(llvm_libs support)

target_link_libraries(test_all PRIVATE
  gtest
  gmock
  gtest_main
  ACSLG_compiler_flags
  ${llvm_libs}
  AnalyzerLib
  SpecGeneratorLib
)

gtest_discover_tests(test_all)