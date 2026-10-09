# Tests for ui-core. Registered by this module's own file (merge-friendly).
add_executable(ui-core-checked-cast-test checked_cast_test.cpp)
target_link_libraries(ui-core-checked-cast-test PRIVATE r1ui::core)
add_test(NAME ui-core.checked_cast COMMAND ui-core-checked-cast-test)
set_tests_properties(ui-core.checked_cast PROPERTIES LABELS "fast")

add_executable(ui-core-json-test json_test.cpp)
target_link_libraries(ui-core-json-test PRIVATE r1ui::core)
add_test(NAME ui-core.json COMMAND ui-core-json-test)
set_tests_properties(ui-core.json PROPERTIES LABELS "fast")

add_executable(ui-core-json-writer-test json_writer_test.cpp)
target_link_libraries(ui-core-json-writer-test PRIVATE r1ui::core)
add_test(NAME ui-core.json_writer COMMAND ui-core-json-writer-test)
set_tests_properties(ui-core.json_writer PROPERTIES LABELS "fast")
