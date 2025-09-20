# src/Context/CMakeLists.txt

add_library(ContextLib STATIC context.cpp)

target_include_directories(ContextLib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(ContextLib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ContextLib PRIVATE ACSLG_system_includes)