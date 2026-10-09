# Tests for ui-theme. The real tokens.json path is passed as an argument so the test never
# depends on the working directory.
add_executable(ui-theme-tokens-test tokens_test.cpp)
target_link_libraries(ui-theme-tokens-test PRIVATE r1ui::theme)
add_test(NAME ui-theme.tokens
         COMMAND ui-theme-tokens-test ${CMAKE_SOURCE_DIR}/assets/theme/tokens.json)
set_tests_properties(ui-theme.tokens PROPERTIES LABELS "fast")

add_executable(ui-theme-token-sections-test token_sections_test.cpp)
target_link_libraries(ui-theme-token-sections-test PRIVATE r1ui::theme)
add_test(NAME ui-theme.token_sections
         COMMAND ui-theme-token-sections-test ${CMAKE_SOURCE_DIR}/assets/theme/tokens.json)
set_tests_properties(ui-theme.token_sections PROPERTIES LABELS "fast")

add_executable(ui-theme-style-sheet-test style_sheet_test.cpp)
target_link_libraries(ui-theme-style-sheet-test PRIVATE r1ui::theme)
add_test(NAME ui-theme.style_sheet
         COMMAND ui-theme-style-sheet-test ${CMAKE_SOURCE_DIR}/assets/theme/tokens.json)
set_tests_properties(ui-theme.style_sheet PROPERTIES LABELS "fast")
