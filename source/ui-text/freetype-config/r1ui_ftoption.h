// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the FreeType feature switches for R1GUI's private FreeType build, layered on top of the
//   stock ftoption.h so the vendored tree stays untouched.
// Why: ui-text renders static TrueType/OpenType-glyf fonts only. Turning off the bundled zlib
//   (compressed SVG documents and .gz fonts), the OpenType-SVG color glyph path and
//   variable-font (GX) support removes parsers for hostile input that we never need and avoids
//   linking the unused gzip module.
// Used by: every r1ui_freetype translation unit via -DFT_CONFIG_OPTIONS_H="r1ui_ftoption.h".
#include <freetype/config/ftoption.h>

#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_SVG
#undef TT_CONFIG_OPTION_GX_VAR_SUPPORT
