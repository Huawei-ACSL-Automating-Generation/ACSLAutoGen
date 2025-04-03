add_executable(ACSLG main.cpp)

target_link_libraries(ACSLG PRIVATE ACSLG_compiler_flags)

llvm_map_components_to_libnames(llvm_libs support core irreader)

target_link_libraries(ACSLG PRIVATE
  ${llvm_libs}
  clangTooling
  clangFrontend
  clangAST
  AnalyzerLib
  ContextLib
)

add_subdirectory(Analyzer)
add_subdirectory(Context)