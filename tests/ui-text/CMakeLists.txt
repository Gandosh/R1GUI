# Tests for ui-text. Registered by this module's own file (merge-friendly). All are headless and
# in the fast tier. Asset and reference locations are passed as compile definitions so no test
# depends on the working directory.
set(R1UI_TEXT_TEST_DEFS
  R1UI_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
  R1UI_REFERENCE_DIR="${CMAKE_SOURCE_DIR}/tests/reference")

function(r1ui_text_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE r1ui::text)
  target_compile_definitions(${name} PRIVATE ${R1UI_TEXT_TEST_DEFS})
  add_test(NAME ui-text.${ARGN} COMMAND ${name})
  set_tests_properties(ui-text.${ARGN} PROPERTIES LABELS "fast")
endfunction()

r1ui_text_test(ui-text-unicode-test unicode_test.cpp unicode)
r1ui_text_test(ui-text-font-test font_test.cpp font)
r1ui_text_test(ui-text-shaper-test shaper_test.cpp shaper)
r1ui_text_test(ui-text-text-widths-test text_widths_test.cpp text_widths)
r1ui_text_test(ui-text-raster-test raster_test.cpp raster)
r1ui_text_test(ui-text-atlas-test atlas_test.cpp atlas)
r1ui_text_test(ui-text-editor-test editor_test.cpp editor)
r1ui_text_test(ui-text-editor-fuzz-test editor_fuzz_test.cpp editor_fuzz)

# Human-eyeball helper, not a test: rasterizes a string as ASCII art or a raw R8 file.
add_executable(ui-text-dump text_dump.cpp)
target_link_libraries(ui-text-dump PRIVATE r1ui::text)
target_compile_definitions(ui-text-dump PRIVATE ${R1UI_TEXT_TEST_DEFS})
