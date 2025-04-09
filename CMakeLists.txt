cmake_minimum_required(VERSION 3.16.0)
project(ACSLG VERSION 1.0.0 LANGUAGES C CXX)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Using ClangConfig.cmake in: ${Clang_DIR}")

add_library(ACSLG_strict_flags INTERFACE)
target_compile_features(ACSLG_strict_flags INTERFACE cxx_std_17)
target_compile_options(ACSLG_strict_flags INTERFACE 
  -Wall
  -Wextra
  -Wshadow
  -Wformat=2
  -Wunused
)

add_library(ACSLG_system_includes INTERFACE)
target_include_directories(ACSLG_system_includes INTERFACE SYSTEM
  ${LLVM_INCLUDE_DIRS}
  ${Clang_INCLUDE_DIRS}
)

add_library(ACSLG_compiler_flags INTERFACE)
target_link_libraries(ACSLG_compiler_flags INTERFACE
  ACSLG_strict_flags
  ACSLG_system_includes
)

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
separate_arguments(CLANG_DEFINITIONS_LIST NATIVE_COMMAND ${CLANG_DEFINITIONS})
target_compile_definitions(ACSLG_compiler_flags INTERFACE
  ${LLVM_DEFINITIONS_LIST}
  ${CLANG_DEFINITIONS_LIST}
)

option(BUILD_TESTS "Build tests" OFF)
enable_testing()

add_subdirectory(src)
add_subdirectory(tests)