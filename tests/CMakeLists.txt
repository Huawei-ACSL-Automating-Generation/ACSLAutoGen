# tests/CMakeLists.txt

find_package(GTest REQUIRED)
include(GoogleTest)

# Use 'test_all' instead of 'TestAll' for better distinction.
add_executable(test_all
  ${CMAKE_CURRENT_SOURCE_DIR}/SpecGenerator/stringTemplate_test.cpp
)

target_link_libraries(test_all PRIVATE
  GTest::gtest
  GTest::gtest_main
  SpecGeneratorLib
)

gtest_discover_tests(test_all)