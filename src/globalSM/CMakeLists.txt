# src/globalSM/CMakeLists.txt

add_library(GlobalSMLib STATIC globalSM.cpp)

target_include_directories(GlobalSMLib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(GlobalSMLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(GlobalSMLib PRIVATE ACSLG_system_includes)