# src/Context/CMakeLists.txt

add_library(ContextLib STATIC context.cpp)

target_include_directories(ContextLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(ContextLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ContextLib PRIVATE ACSLG_system_includes)