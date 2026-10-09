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

r1ui_preview_test(preview-svg-png-test svg_png_test.cpp preview.svg_png fast)
r1ui_preview_test(preview-text-gpu-test text_gpu_test.cpp preview.text_gpu gpu)
r1ui_preview_test(preview-scene-test scene_test.cpp preview.scene gpu)
