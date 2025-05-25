# src/Utils/CMakeLists.txt

add_library(UtilsLib STATIC utils.cpp)

target_include_directories(UtilsLib PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(UtilsLib PRIVATE ACSLG_system_includes)
