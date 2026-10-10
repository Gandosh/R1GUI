# Tests for the preview application's own code (examples/preview), registered from
# examples/preview/CMakeLists.txt. fast: no GPU. gpu: needs a Vulkan device (no window).
set(R1UI_PREVIEW_TEST_DEFS
  R1UI_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
  R1UI_REFERENCE_DIR="${CMAKE_SOURCE_DIR}/tests/reference")

function(r1ui_preview_test name source test label)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE r1gui-preview-core)
  target_compile_definitions(${name} PRIVATE ${R1UI_PREVIEW_TEST_DEFS})
  add_test(NAME ${test} COMMAND ${name})
  set_tests_properties(${test} PROPERTIES LABELS "${label}" TIMEOUT 180)
endfunction()

r1ui_preview_test(preview-scene-test scene_test.cpp preview.scene gpu)
r1ui_preview_test(preview-modes-test modes_test.cpp preview.modes fast)
r1ui_preview_test(preview-editor-test editor_test.cpp preview.editor fast)

# Native-backend window commands over a real desktop window: reuses the dock tests' native rig.
add_executable(preview-editor-float-native-test editor_float_native_test.cpp)
target_link_libraries(preview-editor-float-native-test PRIVATE r1gui-preview-core r1ui::widgets_gpu r1ui_options)
target_include_directories(preview-editor-float-native-test PRIVATE
  ${CMAKE_SOURCE_DIR}/tests/ui-widgets/support ${CMAKE_SOURCE_DIR}/tests/ui-widgets/dock ${CMAKE_SOURCE_DIR}/tests/ui-widgets/dock/native)
target_compile_definitions(preview-editor-float-native-test PRIVATE ${R1UI_PREVIEW_TEST_DEFS} R1UI_ARTIFACT_DIR="${CMAKE_BINARY_DIR}/artifacts/preview")
add_test(NAME preview.editor_float_native COMMAND preview-editor-float-native-test)
set_tests_properties(preview.editor_float_native PROPERTIES LABELS "gpu" TIMEOUT 300)

# Stress mix of native floating windows (create, destroy, hide, minimize, move) with a hang watchdog.
add_executable(preview-editor-float-stress-test editor_float_stress_test.cpp)
target_link_libraries(preview-editor-float-stress-test PRIVATE r1gui-preview-core r1ui::widgets_gpu r1ui_options)
target_include_directories(preview-editor-float-stress-test PRIVATE
  ${CMAKE_SOURCE_DIR}/tests/ui-widgets/support ${CMAKE_SOURCE_DIR}/tests/ui-widgets/dock ${CMAKE_SOURCE_DIR}/tests/ui-widgets/dock/native)
target_compile_definitions(preview-editor-float-stress-test PRIVATE ${R1UI_PREVIEW_TEST_DEFS} R1UI_ARTIFACT_DIR="${CMAKE_BINARY_DIR}/artifacts/preview")
# The application's own blocking loop, and the unpaced scenario that stalled before the fix, three processes at once.
add_test(NAME preview.float_stress_blocking COMMAND preview-editor-float-stress-test blocking 20 1)
add_test(NAME preview.float_stress_unpaced_x3 COMMAND preview-editor-float-stress-test x3-unpaced-again 15 1)
set_tests_properties(preview.float_stress_blocking preview.float_stress_unpaced_x3 PROPERTIES LABELS "gpu" TIMEOUT 120)
