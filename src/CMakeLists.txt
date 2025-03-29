add_executable(ACSLG.exe main.cpp)

target_link_libraries(ACSLG.exe PRIVATE ACSLG_compiler_flags)

llvm_map_components_to_libnames(llvm_libs support core irreader)

target_link_libraries(ACSLG.exe PRIVATE
  ${llvm_libs}
  clangTooling
  clangFrontend
  clangAST
)