# src/Analyzer/CMakeLists.txt

add_library(AnalyzerLib STATIC analysis.cpp state.cpp)

target_include_directories(AnalyzerLib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(AnalyzerLib PRIVATE ACSLG_system_includes)