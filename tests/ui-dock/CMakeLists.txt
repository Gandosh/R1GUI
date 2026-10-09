# Tests for ui-dock (headless). Registered by this module's own file (merge-friendly).
# R1UI_DOCK_GOLDEN_DIR holds the golden layout files.
foreach(name docking resize persistence model2 persistence2 manager)
  add_executable(ui-dock-${name}-test ${name}_test.cpp)
  target_link_libraries(ui-dock-${name}-test PRIVATE r1ui::dock r1ui_options)
  target_compile_definitions(ui-dock-${name}-test PRIVATE R1UI_DOCK_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
  add_test(NAME ui-dock.${name} COMMAND ui-dock-${name}-test)
  set_tests_properties(ui-dock.${name} PROPERTIES LABELS "fast")
endforeach()
