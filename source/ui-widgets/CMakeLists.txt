# ui-widgets: the widget runtime (WidgetObject, UiContext, overlays, tooltips), shared text and icon
# services, the image codec / comparison used by visual tests, and every concrete widget.
#
# Targets:
#   ui-widgets      (r1ui::widgets)      Everything that needs no GPU. Textures are reached through the
#                   TextureFactory interface, so the whole runtime paints into a CPU Painter and is
#                   unit tested in the fast tier without Vulkan.
#   ui-widgets-gpu  (r1ui::widgets_gpu)  Vulkan-backed TextureFactory, frame driver and the offscreen visual
#                   harness. Only defined when ui-render exists (Vulkan SDK present), the same way
#                   ui-render guards Vulkan absence.
#
# Adding a widget needs no edit here: sources under src/ and tests under tests/ui-widgets/<folder>/
# are globbed (CONFIGURE_DEPENDS). src/gpu/ belongs to ui-widgets-gpu.
file(GLOB_RECURSE R1UI_WIDGETS_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
set(R1UI_WIDGETS_GPU_SOURCES ${R1UI_WIDGETS_SOURCES})
list(FILTER R1UI_WIDGETS_SOURCES EXCLUDE REGEX "/src/gpu/")
list(FILTER R1UI_WIDGETS_GPU_SOURCES INCLUDE REGEX "/src/gpu/")

add_library(ui-widgets STATIC ${R1UI_WIDGETS_SOURCES})
add_library(r1ui::widgets ALIAS ui-widgets)
target_include_directories(ui-widgets PUBLIC include)
target_compile_features(ui-widgets PUBLIC cxx_std_20)
target_link_libraries(ui-widgets PUBLIC r1ui::core r1ui::theme r1ui::text r1ui::render_paint r1ui::platform
                      PRIVATE r1ui_options)

if(TARGET ui-render)
  add_library(ui-widgets-gpu STATIC ${R1UI_WIDGETS_GPU_SOURCES})
  add_library(r1ui::widgets_gpu ALIAS ui-widgets-gpu)
  target_link_libraries(ui-widgets-gpu PUBLIC r1ui::widgets r1ui::render PRIVATE r1ui_options)
endif()

# Tests are registered by tests/ui-widgets/CMakeLists.txt (a glob, so adding a test needs no edit).
if(BUILD_TESTING)
  add_subdirectory(${CMAKE_SOURCE_DIR}/tests/ui-widgets ${CMAKE_BINARY_DIR}/tests/ui-widgets)
endif()
