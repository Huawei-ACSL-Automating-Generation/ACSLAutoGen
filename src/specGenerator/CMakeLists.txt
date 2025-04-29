# src/specGenerator/CMakeLists.txt

add_library(SpecGeneratorLib STATIC 
    stringTemplate.cpp
    specGenerator.cpp
    plugins.cpp
    groups.cpp
)

target_include_directories(SpecGeneratorLib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(SpecGeneratorLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(SpecGeneratorLib PRIVATE 
    ACSLG_compiler_flags
    AnalyzerLib
)