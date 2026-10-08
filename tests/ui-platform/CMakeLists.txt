# Tests for ui-platform. Window creation needs a desktop session, so only the headless
# build/link check lives in the fast tier; interactive checks are in the preview checklist.
add_executable(ui-platform-link-test link_test.cpp)
target_link_libraries(ui-platform-link-test PRIVATE r1ui::platform r1ui_options)
add_test(NAME ui-platform.link COMMAND ui-platform-link-test)
set_tests_properties(ui-platform.link PROPERTIES LABELS "fast")
