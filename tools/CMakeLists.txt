add_executable(emit_subclasses emitSubclasses.cpp)
target_link_libraries(emit_subclasses PRIVATE ACSLG_compiler_flags clangTooling
                                              clangBasic clangAST clangFrontend)
