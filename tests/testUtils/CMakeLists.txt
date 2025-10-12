# tests/testUtils/CMakeLists.txt

add_library(TestUtilsLib STATIC ASTExtractor.cpp testHelper.cpp)

target_include_directories(TestUtilsLib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(TestUtilsLib PRIVATE ACSLG_compiler_flags)

llvm_map_components_to_libnames(llvm_libs support core irreader)

target_link_libraries(TestUtilsLib PRIVATE
  ContextLib
  ${llvm_libs}
  clangTooling
  clangFrontend
  clangAST
  GTest::gtest
  GTest::gmock
)