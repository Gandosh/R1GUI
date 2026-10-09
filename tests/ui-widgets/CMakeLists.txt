# Tests for ui-widgets, registered by a glob so that adding a widget's tests needs no CMake edit.
#
# Convention: tests/ui-widgets/<folder>/<name>_test.cpp  (one executable and one CTest test each)
#   CTest name:   ui-widgets.<folder>.<name>_test
#   label fast:   <name>_test.cpp            no GPU, no window; links r1ui::widgets
#   label gpu:    <name>_gpu_test.cpp and <name>_visual_test.cpp   need a Vulkan device (offscreen only);
#                 link r1ui::widgets_gpu. Skipped silently when the Vulkan SDK is absent.
# Shared helpers live in tests/ui-widgets/support (include path of every test, not globbed).
# Compile definitions every test gets: R1UI_ASSETS_DIR, R1UI_REFERENCE_DIR, R1UI_ARTIFACT_DIR (where
# visual tests write their renders and diffs).
set(R1UI_WIDGETS_TEST_DEFS
  R1UI_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
  R1UI_REFERENCE_DIR="${CMAKE_SOURCE_DIR}/tests/reference"
  R1UI_ARTIFACT_DIR="${CMAKE_BINARY_DIR}/artifacts/ui-widgets")

file(GLOB R1UI_WIDGETS_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*/*_test.cpp)
foreach(src ${R1UI_WIDGETS_TEST_SOURCES})
  get_filename_component(dir ${src} DIRECTORY)
  get_filename_component(folder ${dir} NAME)
  get_filename_component(stem ${src} NAME_WE)
  if(folder STREQUAL "support")
    continue()
  endif()
  set(is_gpu FALSE)
  if(stem MATCHES "_(gpu|visual)_test$")
    set(is_gpu TRUE)
  endif()
  if(is_gpu AND NOT TARGET ui-widgets-gpu)
    continue()
  endif()
  set(exe ui-widgets-${folder}-${stem})
  add_executable(${exe} ${src})
  target_include_directories(${exe} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
  target_compile_definitions(${exe} PRIVATE ${R1UI_WIDGETS_TEST_DEFS})
  if(is_gpu)
    target_link_libraries(${exe} PRIVATE r1ui::widgets_gpu r1ui_options)
  else()
    target_link_libraries(${exe} PRIVATE r1ui::widgets r1ui_options)
  endif()
  set(test_name ui-widgets.${folder}.${stem})
  add_test(NAME ${test_name} COMMAND ${exe})
  if(is_gpu)
    set_tests_properties(${test_name} PROPERTIES LABELS "gpu" TIMEOUT 300)
  else()
    set_tests_properties(${test_name} PROPERTIES LABELS "fast" TIMEOUT 120)
  endif()
endforeach()
