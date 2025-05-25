add_executable(ACSLG main.cpp)

target_link_libraries(ACSLG PRIVATE ACSLG_compiler_flags)

llvm_map_components_to_libnames(llvm_libs support core irreader)

add_subdirectory(analyzer)
add_subdirectory(context)
add_subdirectory(utils)
add_subdirectory(specGenerator)
add_subdirectory(stingx)

target_link_libraries(ACSLG PRIVATE
  ${llvm_libs}
  clangTooling
  clangFrontend
  clangAST
  AnalyzerLib
  ContextLib
  UtilsLib
  SpecGeneratorLib
  lstingx
)
