# src/SpecGenerator/CMakeLists.txt

add_library(SpecGeneratorLib STATIC stringTemplate.cpp specGenerator.cpp)

add_library(
  plugins_obj OBJECT
  functionContractPlugins.cpp
  loopInfoPlugins.cpp
  loopInvariantPlugins.cpp
  numericalInvariantPrototypePlugin.cpp
  groups.cpp)

target_include_directories(
  plugins_obj
  PRIVATE $<TARGET_PROPERTY:ACSLG_system_includes,INTERFACE_INCLUDE_DIRECTORIES>
          $<TARGET_PROPERTY:AnalyzerLib,INTERFACE_INCLUDE_DIRECTORIES>
          $<TARGET_PROPERTY:SpecGeneratorLib,INTERFACE_INCLUDE_DIRECTORIES>
          $<TARGET_PROPERTY:UtilsLib,INTERFACE_INCLUDE_DIRECTORIES>)

target_include_directories(SpecGeneratorLib
                           INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(SpecGeneratorLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(
  SpecGeneratorLib
  PRIVATE ACSLG_compiler_flags
          UtilsLib
          $<TARGET_OBJECTS:plugins_obj>
          AnalyzerLib
          NumericalInvariantPrototypeLib
          clangAST
          clangTooling
          clangFrontend)
