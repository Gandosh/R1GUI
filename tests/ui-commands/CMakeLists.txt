# Tests for ui-commands, registered by a glob: tests/ui-commands/<name>_test.cpp or tests/ui-commands/<folder>/<name>_test.cpp
# (one executable and one fast CTest test each, named ui-commands.<name>). Compile definitions every test gets:
# R1UI_ASSETS_DIR and R1UI_REFERENCE_DIR.
file(GLOB R1UI_COMMANDS_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*/*_test.cpp)
foreach(src ${R1UI_COMMANDS_TEST_SOURCES})
  get_filename_component(stem ${src} NAME_WE)
  set(exe ui-commands-${stem})
  add_executable(${exe} ${src})
  target_include_directories(${exe} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
  target_compile_definitions(${exe} PRIVATE R1UI_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets" R1UI_REFERENCE_DIR="${CMAKE_SOURCE_DIR}/tests/reference")
  target_link_libraries(${exe} PRIVATE r1ui::commands r1ui_options)
  add_test(NAME ui-commands.${stem} COMMAND ${exe})
  set_tests_properties(ui-commands.${stem} PROPERTIES LABELS "fast" TIMEOUT 120)
endforeach()
