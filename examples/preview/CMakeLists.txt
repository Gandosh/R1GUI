# Interactive preview application (owner requirement: launchable after every phase).
# Built from the real toolkit modules; grows cumulatively each phase.
#   r1gui-preview-core  everything except main.cpp (also linked by tests/preview)
#   r1gui-preview       the borderless preview window; --bench, --bench-editor and --shot command lines
# Assets next to the executable: assets/theme/tokens.json, assets/fonts/*.ttf, assets/icons/lucide/*.svg
# (copied) and <ref>/<theme>/screen-*.r1img (generated from the reference PNGs by
# tools/spec/png_to_raw.py; never committed).
if(NOT TARGET r1ui::render)
  return()
endif()

add_library(r1gui-preview-core STATIC
  Assets.cpp
  Bench.cpp
  BenchEditor.cpp
  CanvasView.cpp
  ComposedApp.cpp
  ComposedCanvas.cpp
  ComposedOverlays.cpp
  ComposedPanel.cpp
  ComposedSide.cpp
  DockSandbox.cpp
  DriveStatus.cpp
  editor/EditorApp.cpp
  editor/EditorBrushes.cpp
  editor/EditorBrushLibrary.cpp
  editor/EditorCommands.cpp
  editor/EditorCustomMenus.cpp
  editor/EditorLayouts.cpp
  editor/EditorMenus.cpp
  editor/EditorModel.cpp
  editor/EditorPanels.cpp
  editor/EditorViewport.cpp
  editor/EditorWorkspaces.cpp
  GalleryApp.cpp
  ModeViews.cpp
  PreviewApp.cpp
  PreviewParts.cpp
  RawImage.cpp
  Scene.cpp
  SceneBuild.cpp
  ScenePaint.cpp
  Shots.cpp
  UiStyle.cpp)
target_include_directories(r1gui-preview-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(r1gui-preview-core PUBLIC r1ui::platform r1ui::render r1ui::theme r1ui::dock r1ui::core r1ui::text r1ui::widgets_gpu r1ui_options
                      psapi shell32)

add_executable(r1gui-preview WIN32 main.cpp)
target_link_libraries(r1gui-preview PRIVATE r1gui-preview-core)

set(PREVIEW_OUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# ---- Copied assets: tokens, fonts, icons -------------------------------------------------
file(GLOB PREVIEW_FONT_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/fonts/*)
file(GLOB PREVIEW_ICON_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/icons/lucide/* ${CMAKE_SOURCE_DIR}/assets/icons/custom/*)
set(PREVIEW_ASSET_STAMP ${PREVIEW_OUT}/assets/assets.stamp)
add_custom_command(
  OUTPUT ${PREVIEW_ASSET_STAMP}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/assets/theme/tokens.json ${PREVIEW_OUT}/assets/theme/tokens.json
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets/fonts ${PREVIEW_OUT}/assets/fonts
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets/icons/lucide ${PREVIEW_OUT}/assets/icons/lucide
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets/icons/custom ${PREVIEW_OUT}/assets/icons/custom
  COMMAND ${CMAKE_COMMAND} -E touch ${PREVIEW_ASSET_STAMP}
  DEPENDS ${CMAKE_SOURCE_DIR}/assets/theme/tokens.json ${PREVIEW_FONT_FILES} ${PREVIEW_ICON_FILES}
  COMMENT "Copying tokens, fonts and icons next to the preview")
add_custom_target(r1gui-preview-assets DEPENDS ${PREVIEW_ASSET_STAMP})
set(PREVIEW_ASSET_TARGETS r1gui-preview-assets)

# ---- Reference screens converted to raw images -------------------------------------------
set(PREVIEW_PNG_ROOT ${CMAKE_SOURCE_DIR}/tests/reference/openpencil)
set(PREVIEW_RAW_ROOT ${PREVIEW_OUT}/reference)
set(PREVIEW_RAW_STAMP ${PREVIEW_RAW_ROOT}/screens.stamp)
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

# Tests live in tests/preview and are registered here, like ui-render does, because the top-level
# CMakeLists.txt does not list them (module-owned registration).
if(BUILD_TESTING)
  add_subdirectory(${CMAKE_SOURCE_DIR}/tests/preview ${CMAKE_BINARY_DIR}/tests/preview)
endif()
