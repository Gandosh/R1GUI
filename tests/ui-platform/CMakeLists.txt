# Tests for ui-platform. Registered by this module's own file (merge-friendly).
# fast: headless logic (no desktop needed). desktop: creates real windows and skips itself
# (prints SKIPPED, exit 0) when the session has no interactive desktop.
add_executable(ui-platform-link-test link_test.cpp)
target_link_libraries(ui-platform-link-test PRIVATE r1ui::platform r1ui_options)
add_test(NAME ui-platform.link COMMAND ui-platform-link-test)
set_tests_properties(ui-platform.link PROPERTIES LABELS "fast")

foreach(name utf queue_dpi monitor_clamp chrome_hit)
  add_executable(ui-platform-${name}-test ${name}_test.cpp)
  target_link_libraries(ui-platform-${name}-test PRIVATE r1ui::platform r1ui_options)
  add_test(NAME ui-platform.${name} COMMAND ui-platform-${name}-test)
  set_tests_properties(ui-platform.${name} PROPERTIES LABELS "fast")
endforeach()

add_executable(ui-platform-window-desktop-test window_desktop_test.cpp)
target_link_libraries(ui-platform-window-desktop-test PRIVATE r1ui::platform r1ui_options)
add_test(NAME ui-platform.window_desktop COMMAND ui-platform-window-desktop-test)
set_tests_properties(ui-platform.window_desktop PROPERTIES LABELS "desktop" SKIP_REGULAR_EXPRESSION "SKIPPED")

# Manual harness: built only on request (cmake --build <dir> --target ui-platform-manual).
add_executable(ui-platform-manual EXCLUDE_FROM_ALL manual_chrome_harness.cpp)
target_link_libraries(ui-platform-manual PRIVATE r1ui::platform r1ui_options gdi32)
