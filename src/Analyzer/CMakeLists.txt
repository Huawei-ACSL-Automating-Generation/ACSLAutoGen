# src/Analyzer/CMakeLists.txt

add_library(
  AnalyzerLib STATIC
  analysis.cpp
  state.cpp
  crossTU.cpp
  Symbolic/expr.cpp
  Symbolic/aggregateExpr.cpp
  Symbolic/invariant.cpp
  function.cpp)

target_include_directories(AnalyzerLib PUBLIC ${CMAKE_SOURCE_DIR}/src
                                              ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(AnalyzerLib PUBLIC ${CMAKE_SOURCE_DIR}/src/Stingx)

target_link_libraries(
  AnalyzerLib
  PRIVATE ACSLG_compiler_flags
          UtilsLib
          SpecGeneratorLib
          ContextLib
          lstingx
          clangAST
          clangTooling
          clangFrontend
          ${PPL_LIBRARY}
          ${GMP_LIBRARY}
          ${GMPXX_LIBRARY})
