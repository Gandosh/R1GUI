# Interactive preview application (owner requirement: launchable after every phase).
# Built from the real toolkit modules; grows cumulatively each phase.
# Assets next to the executable: assets/theme/tokens.json (copied) and <ref>/<theme>/screen-*.r1img
# (generated from the reference PNGs by tools/spec/png_to_raw.py; never committed).
if(NOT TARGET r1ui::render)
  return()
endif()

add_executable(r1gui-preview WIN32 main.cpp RawImage.cpp)
target_link_libraries(r1gui-preview PRIVATE r1ui::platform r1ui::render r1ui::theme r1ui::core r1ui_options)

set(PREVIEW_OUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(PREVIEW_TOKENS_SRC ${CMAKE_SOURCE_DIR}/assets/theme/tokens.json)
set(PREVIEW_TOKENS_DST ${PREVIEW_OUT}/assets/theme/tokens.json)
add_custom_command(
  OUTPUT ${PREVIEW_TOKENS_DST}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PREVIEW_TOKENS_SRC} ${PREVIEW_TOKENS_DST}
  DEPENDS ${PREVIEW_TOKENS_SRC}
  COMMENT "Copying design tokens next to the preview")

set(PREVIEW_PNG_ROOT ${CMAKE_SOURCE_DIR}/tests/reference/openpencil)
set(PREVIEW_RAW_ROOT ${PREVIEW_OUT}/reference)
set(PREVIEW_RAW_STAMP ${PREVIEW_RAW_ROOT}/screens.stamp)
set(PREVIEW_ASSET_TARGETS r1gui-preview-tokens)
add_custom_target(r1gui-preview-tokens DEPENDS ${PREVIEW_TOKENS_DST})

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  file(GLOB PREVIEW_PNGS CONFIGURE_DEPENDS ${PREVIEW_PNG_ROOT}/*/screen-*.png)
  add_custom_command(
    OUTPUT ${PREVIEW_RAW_STAMP}
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/spec/png_to_raw.py
            ${PREVIEW_PNG_ROOT} ${PREVIEW_RAW_ROOT} --only screen-
    COMMAND ${CMAKE_COMMAND} -E touch ${PREVIEW_RAW_STAMP}
    DEPENDS ${PREVIEW_PNGS} ${CMAKE_SOURCE_DIR}/tools/spec/png_to_raw.py
    COMMENT "Converting reference screens to raw images")
  add_custom_target(r1gui-preview-screens DEPENDS ${PREVIEW_RAW_STAMP})
  list(APPEND PREVIEW_ASSET_TARGETS r1gui-preview-screens)
else()
  message(WARNING "Python 3 not found: the preview will report missing reference screens")
endif()
add_dependencies(r1gui-preview ${PREVIEW_ASSET_TARGETS})
