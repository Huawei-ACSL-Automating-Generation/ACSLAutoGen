# src/Utils/CMakeLists.txt

add_library(UtilsLib STATIC utils.cpp)

target_include_directories(UtilsLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(UtilsLib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(UtilsLib PRIVATE ACSLG_system_includes)
