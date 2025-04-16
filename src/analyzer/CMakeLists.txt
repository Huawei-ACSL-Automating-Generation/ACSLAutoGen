# src/Analyzer/CMakeLists.txt

add_library(AnalyzerLib STATIC analysis.cpp state.cpp symbolic.cpp)

target_include_directories(AnalyzerLib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(AnalyzerLib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(AnalyzerLib PRIVATE
    ACSLG_compiler_flags
    UtilsLib
    GlobalSMLib
    clangAST
    clangTooling
    clangFrontend
)