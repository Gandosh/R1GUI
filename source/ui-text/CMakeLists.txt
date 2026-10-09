# ui-text: fonts, shaping, glyph rasterization, glyph atlas and single-line text editing.
# Placeholder created with the Phase 3 setup; the text module owner replaces this file.
add_library(ui-text INTERFACE)
add_library(r1ui::text ALIAS ui-text)
target_include_directories(ui-text INTERFACE include)
target_link_libraries(ui-text INTERFACE r1ui::core)
