# Tests for ui-core. Registered by this module's own file (merge-friendly).
# Every test is a plain executable (exit code 0 = pass) registered under the "fast" label.
function(r1ui_core_test name source)
  add_executable(ui-core-${name}-test ${source})
  target_link_libraries(ui-core-${name}-test PRIVATE r1ui::core)
  add_test(NAME ui-core.${name} COMMAND ui-core-${name}-test)
  set_tests_properties(ui-core.${name} PROPERTIES LABELS "fast")
endfunction()

r1ui_core_test(checked_cast checked_cast_test.cpp)
r1ui_core_test(json json_test.cpp)
r1ui_core_test(json_writer json_writer_test.cpp)
r1ui_core_test(tree tree_test.cpp)
r1ui_core_test(layout layout_test.cpp)
r1ui_core_test(events events_test.cpp)
r1ui_core_test(invalidation invalidation_test.cpp)
