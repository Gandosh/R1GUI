# Tests for ui-dock (headless). Registered by this module's own file (merge-friendly).
foreach(name docking resize persistence)
  add_executable(ui-dock-${name}-test ${name}_test.cpp)
  target_link_libraries(ui-dock-${name}-test PRIVATE r1ui::dock r1ui_options)
  add_test(NAME ui-dock.${name} COMMAND ui-dock-${name}-test)
  set_tests_properties(ui-dock.${name} PROPERTIES LABELS "fast")
endforeach()
