# ui-text: fonts, shaping, glyph rasterization, glyph atlas and single-line text editing.
# Headless: no Vulkan and no OS window code. The renderer consumes the atlas bitmap and quads.
#
# Third-party libraries are built here from the vendored sources as private static libraries
# with their warnings silenced (/W0); the first-party warning policy (/W4 /WX via r1ui_options)
# applies only to the ui-text target itself.
#
# FreeType 2.14.3: compiled from source with only the modules ui-text needs (TrueType driver,
#   SFNT, PS names, smooth rasterizer; see freetype-config/.../ftmodule.h). No zlib, bzip2,
#   libpng, brotli or HarfBuzz integration (ftoption.h leaves them off), no autohinter.
# HarfBuzz 14.6.0: the single amalgamated translation unit src/harfbuzz.cc. FreeType integration
#   is OFF: HarfBuzz reads the font tables itself (hb-ot), so shaping never calls FreeType and the
#   two libraries only share the font bytes. No ICU, GLib, Graphite, DirectWrite, Uniscribe, GDI.
enable_language(C)

set(R1UI_FT_ROOT ${PROJECT_SOURCE_DIR}/third_party/freetype)
set(R1UI_HB_ROOT ${PROJECT_SOURCE_DIR}/third_party/harfbuzz/src)

add_library(r1ui_freetype STATIC
  ${R1UI_FT_ROOT}/src/base/ftsystem.c
  ${R1UI_FT_ROOT}/src/base/ftinit.c
  ${R1UI_FT_ROOT}/src/base/ftdebug.c
  ${R1UI_FT_ROOT}/src/base/ftbase.c
  ${R1UI_FT_ROOT}/src/base/ftbbox.c
  ${R1UI_FT_ROOT}/src/base/ftbitmap.c
  ${R1UI_FT_ROOT}/src/base/ftglyph.c
  ${R1UI_FT_ROOT}/src/sfnt/sfnt.c
  ${R1UI_FT_ROOT}/src/truetype/truetype.c
  ${R1UI_FT_ROOT}/src/psnames/psnames.c
  ${R1UI_FT_ROOT}/src/smooth/smooth.c)
# The option override is PUBLIC so ui-text sees the same FreeType configuration as the library.
target_compile_definitions(r1ui_freetype
  PRIVATE FT2_BUILD_LIBRARY _CRT_SECURE_NO_WARNINGS
  PUBLIC "FT_CONFIG_OPTIONS_H=\"r1ui_ftoption.h\"")
target_include_directories(r1ui_freetype PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/freetype-config ${R1UI_FT_ROOT}/include)
if(MSVC)
  target_compile_options(r1ui_freetype PRIVATE /W0)
endif()

add_library(r1ui_harfbuzz STATIC ${R1UI_HB_ROOT}/harfbuzz.cc)
target_include_directories(r1ui_harfbuzz PUBLIC ${R1UI_HB_ROOT})
target_compile_features(r1ui_harfbuzz PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(r1ui_harfbuzz PRIVATE /W0 /utf-8 /bigobj)
endif()

add_library(ui-text STATIC
  src/CaretMap.cpp
  src/Font.cpp
  src/GlyphAtlas.cpp
  src/GlyphQuads.cpp
  src/Grapheme.cpp
  src/Rasterizer.cpp
  src/Shaper.cpp
  src/ShelfPacker.cpp
  src/TextEditor.cpp
  src/TextEditorHistory.cpp
  src/Utf8.cpp)
add_library(r1ui::text ALIAS ui-text)
target_include_directories(ui-text PUBLIC include)
target_compile_features(ui-text PUBLIC cxx_std_20)
target_link_libraries(ui-text PUBLIC r1ui::core PRIVATE r1ui_freetype r1ui_harfbuzz)
