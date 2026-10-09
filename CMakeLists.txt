# R1GUI top level: owns project-wide compiler settings and module registration.
# Modules live under source/<module>; tests mirror them under tests/<module>.
cmake_minimum_required(VERSION 3.25)
project(R1GUI VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(R1UI_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

# Shared warning/definition set applied to every first-party target.
add_library(r1ui_options INTERFACE)
target_compile_definitions(r1ui_options INTERFACE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
  target_compile_options(r1ui_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus)
  if(R1UI_WARNINGS_AS_ERRORS)
    target_compile_options(r1ui_options INTERFACE /WX)
  endif()
endif()

include(CTest)

add_subdirectory(source/ui-core)
add_subdirectory(source/ui-theme)
add_subdirectory(source/ui-dock)
add_subdirectory(source/ui-platform)
add_subdirectory(source/ui-text)
add_subdirectory(source/ui-render)
add_subdirectory(examples/preview)

if(BUILD_TESTING)
  add_subdirectory(tests/ui-core)
  add_subdirectory(tests/ui-theme)
  add_subdirectory(tests/ui-dock)
  add_subdirectory(tests/ui-text)
  add_subdirectory(tests/ui-platform)
  add_subdirectory(tools/audit)
  add_subdirectory(tools/spec)
endif()
