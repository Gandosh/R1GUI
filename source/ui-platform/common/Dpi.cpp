// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the DPI scale conversion functions declared in Dpi.h.
// Why: a single rounding and sanitizing rule for logical/physical conversion.
// Callers: Window backend, hit-testing, monitor clamping, layout code via Window.h.
// Invariants: never throws; see Dpi.h for the saturation and NaN rules.
#include "r1ui/platform/Dpi.h"

#include <cmath>
#include <limits>

namespace r1ui::platform {

float dpiScaleFromDpi(unsigned dpi) {
  return dpi == 0 ? 1.0f : static_cast<float>(dpi) / static_cast<float>(kBaseDpi);
}

float sanitizeScale(float scale) { return (std::isfinite(scale) && scale > 0.0f) ? scale : 1.0f; }

int logicalToPhysical(float logical, float scale) {
  const double value = static_cast<double>(logical) * static_cast<double>(sanitizeScale(scale));
  if (std::isnan(value)) return 0;
  const double rounded = std::round(value);
  if (rounded >= static_cast<double>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
  if (rounded <= static_cast<double>(std::numeric_limits<int>::min())) return std::numeric_limits<int>::min();
  return static_cast<int>(rounded);
}

float physicalToLogical(int physical, float scale) {
  return static_cast<float>(physical) / sanitizeScale(scale);
}

}  // namespace r1ui::platform
