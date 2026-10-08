# Tests for ui-theme. The real tokens.json path is passed as an argument so the test never
# depends on the working directory.
add_executable(ui-theme-tokens-test tokens_test.cpp)
target_link_libraries(ui-theme-tokens-test PRIVATE r1ui::theme)
add_test(NAME ui-theme.tokens
         COMMAND ui-theme-tokens-test ${CMAKE_SOURCE_DIR}/assets/theme/tokens.json)
set_tests_properties(ui-theme.tokens PROPERTIES LABELS "fast")
