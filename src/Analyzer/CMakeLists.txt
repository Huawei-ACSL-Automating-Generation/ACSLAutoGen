# src/Analyzer/CMakeLists.txt

add_library(AnalyzerLib STATIC analysis.cpp)

target_include_directories(AnalyzerLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(AnalyzerLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(AnalyzerLib PRIVATE ACSLG_system_includes)