# tests/CMakeLists.txt

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(test_all
  ${CMAKE_CURRENT_SOURCE_DIR}/SpecGenerator/stringTemplate_test.cpp
)

target_link_libraries(test_all PRIVATE
  GTest::gtest
  GTest::gtest_main
  SpecGeneratorLib
)

if(NOT BUILD_TESTS)
  set_target_properties(test_all PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

gtest_discover_tests(test_all)