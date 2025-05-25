cmake_minimum_required(VERSION 3.16.0)
project(ACSLG VERSION 1.0.0 LANGUAGES C CXX)

add_compile_options(-Wno-deprecated-declarations)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(DEBUG_MODE)
endif()

add_library(ACSLG_system_includes INTERFACE)
target_include_directories(ACSLG_system_includes SYSTEM INTERFACE
  ${LLVM_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(ACSLG_compiler_flags INTERFACE)
target_link_libraries(ACSLG_compiler_flags INTERFACE
  ACSLG_strict_flags
  ACSLG_system_includes
)

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
target_compile_definitions(ACSLG_compiler_flags INTERFACE
  ${LLVM_DEFINITIONS_LIST}
)

add_subdirectory(src)

option(BUILD_TESTS "Build tests" OFF)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()