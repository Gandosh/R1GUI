# Tests for ui-props, registered by a glob: tests/ui-props/<name>_test.cpp or tests/ui-props/<folder>/<name>_test.cpp
# (one executable and one fast CTest test each, named ui-props.<name>). Compile definitions every test gets:
# R1UI_ASSETS_DIR and R1UI_REFERENCE_DIR.
file(GLOB R1UI_PROPS_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*/*_test.cpp)
foreach(src ${R1UI_PROPS_TEST_SOURCES})
  get_filename_component(stem ${src} NAME_WE)
  set(exe ui-props-${stem})
  add_executable(${exe} ${src})
  target_include_directories(${exe} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
  target_compile_definitions(${exe} PRIVATE R1UI_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets" R1UI_REFERENCE_DIR="${CMAKE_SOURCE_DIR}/tests/reference")
  target_link_libraries(${exe} PRIVATE r1ui::props r1ui_options)
  add_test(NAME ui-props.${stem} COMMAND ${exe})
  set_tests_properties(ui-props.${stem} PROPERTIES LABELS "fast" TIMEOUT 120)
endforeach()
