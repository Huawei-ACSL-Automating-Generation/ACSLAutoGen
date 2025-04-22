# src/Context/CMakeLists.txt

add_library(ContextLib STATIC context.cpp globalSM.cpp)

target_include_directories(ContextLib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ContextLib PRIVATE ACSLG_system_includes)