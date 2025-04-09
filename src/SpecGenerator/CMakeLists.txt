# src/SpecGenerator/CMakeLists.txt

add_library(SpecGeneratorLib STATIC stringTemplate.cpp)

target_include_directories(SpecGeneratorLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(SpecGeneratorLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(SpecGeneratorLib PRIVATE ACSLG_system_includes)