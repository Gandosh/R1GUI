// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DPI scale conversions between logical pixels (device independent, 1.0 = 96 dpi) and
//   physical pixels (what the OS and the renderer use).
// Why: the guard register requires explicit logical vs physical units; one rounding rule keeps
//   hit-test bands, margins and layout sizes identical on every monitor.
// Callers: Window.h (dpiScale), Monitors.h (clamp margin), ChromeHitTest.h (resize bands).
// Failure behavior: never throws. A non-finite or non-positive scale is treated as 1.0; results
//   saturate to the int range; NaN input becomes 0.
#pragma once

namespace r1ui::platform {

inline constexpr unsigned kBaseDpi = 96;

// Scale for an OS dpi value; dpi == 0 (unknown) yields 1.0.
float dpiScaleFromDpi(unsigned dpi);

// Returns `scale` if it is finite and positive, otherwise 1.0.
float sanitizeScale(float scale);

// Rounds logical * scale to the nearest physical pixel (half away from zero).
int logicalToPhysical(float logical, float scale);

// physical / scale as a float.
float physicalToLogical(int physical, float scale);

}  // namespace r1ui::platform
