# tests/utils/CMakeLists.txt

add_library(Utils STATIC ASTExtractor.cpp)

target_include_directories(Utils INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Utils PRIVATE ACSLG_compiler_flags)

llvm_map_components_to_libnames(llvm_libs support core irreader)

target_link_libraries(Utils PRIVATE
  ${llvm_libs}
  clangTooling
  clangFrontend
  clangAST
)