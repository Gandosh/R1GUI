// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the FreeType module list for R1GUI's private FreeType build.
// Why: ui-text only loads TrueType/OpenType (glyf) fonts and renders unhinted grayscale
//   outlines, so only the TrueType driver, its SFNT/PS-names support and the smooth rasterizer
//   are compiled in. Fewer drivers means less parsing code exposed to untrusted font files.
// Used by: ftinit.c via FT_CONFIG_MODULES_H. This directory is placed before
//   third_party/freetype/include on the include path of the r1ui_freetype target only; the
//   vendored FreeType tree is not modified.
// Order matters: the renderer and helper modules are listed after the driver, as in FreeType's
//   own modules.cfg order.
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
