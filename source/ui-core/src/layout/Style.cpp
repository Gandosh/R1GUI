// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: sanitizeStyle, the single place where hostile style values are normalised.
// Why: see Style.h; the flex algorithm assumes finite, in-range inputs and never re-checks.
// Callers: layout::FlexLayout (once per node per computation), tests.
#include "r1ui/core/layout/Style.h"

#include <algorithm>
#include <cmath>

namespace r1ui::core::layout {

namespace {

// NaN -> fallback; +-infinity and out-of-range values clamp to [lo, hi].
double finiteOr(double v, double lo, double hi, double fallback) {
  if (std::isnan(v)) return fallback;
  return std::clamp(v, lo, hi);
}

// Size-like length (width, height, basis, max): NaN or negative -> Auto.
Length sanitizeSize(const Length& l) {
  switch (l.kind) {
    case Length::Kind::Px:
      if (std::isnan(l.value) || l.value < 0) return Length::autoValue();
      return Length::px(std::min(l.value, kMaxExtent));
    case Length::Kind::Percent:
      if (std::isnan(l.value) || l.value < 0) return Length::autoValue();
      return Length::percent(std::min(l.value, 1.0e7));
    case Length::Kind::FitContent: return Length::fitContent();
    case Length::Kind::Auto: break;
  }
  return Length::autoValue();
}

// Minimum length: NaN or negative -> 0 px; Auto and FitContent mean 0 as well.
Length sanitizeMin(const Length& l) {
  if (l.kind == Length::Kind::Px) return Length::px(finiteOr(l.value, 0.0, kMaxExtent, 0.0));
  if (l.kind == Length::Kind::Percent) return Length::percent(finiteOr(l.value, 0.0, 1.0e7, 0.0));
  return Length::px(0.0);
}

// Margin: px (negative allowed) or Auto; percentages are unsupported and become 0.
Length sanitizeMargin(const Length& l) {
  if (l.kind == Length::Kind::Auto) return Length::autoValue();
  if (l.kind == Length::Kind::Px) return Length::px(finiteOr(l.value, -kMaxExtent, kMaxExtent, 0.0));
  return Length::px(0.0);
}

// Inset: px or percent of the containing block, any sign; anything else is Auto.
Length sanitizeInset(const Length& l) {
  if (l.kind == Length::Kind::Px && !std::isnan(l.value)) {
    return Length::px(std::clamp(l.value, -kMaxExtent, kMaxExtent));
  }
  if (l.kind == Length::Kind::Percent && !std::isnan(l.value)) {
    return Length::percent(std::clamp(l.value, -1.0e7, 1.0e7));
  }
  return Length::autoValue();
}

}  // namespace

Style sanitizeStyle(const Style& in) {
  Style s = in;
  s.flexGrow = finiteOr(in.flexGrow, 0.0, 1.0e9, 0.0);
  s.flexShrink = finiteOr(in.flexShrink, 0.0, 1.0e9, 1.0);
  s.flexBasis = sanitizeSize(in.flexBasis);
  s.width = sanitizeSize(in.width);
  s.height = sanitizeSize(in.height);
  s.maxWidth = sanitizeSize(in.maxWidth);
  s.maxHeight = sanitizeSize(in.maxHeight);
  // FitContent is meaningful only for width/height; elsewhere it is Auto.
  if (s.flexBasis.kind == Length::Kind::FitContent) s.flexBasis = Length::autoValue();
  if (s.maxWidth.kind == Length::Kind::FitContent) s.maxWidth = Length::autoValue();
  if (s.maxHeight.kind == Length::Kind::FitContent) s.maxHeight = Length::autoValue();
  s.minWidth = sanitizeMin(in.minWidth);
  s.minHeight = sanitizeMin(in.minHeight);
  for (int i = 0; i < 4; ++i) {
    s.padding[i] = finiteOr(in.padding[i], 0.0, kMaxExtent, 0.0);
    s.margin[i] = sanitizeMargin(in.margin[i]);
    s.inset[i] = sanitizeInset(in.inset[i]);
  }
  s.gapRow = finiteOr(in.gapRow, 0.0, kMaxExtent, 0.0);
  s.gapColumn = finiteOr(in.gapColumn, 0.0, kMaxExtent, 0.0);
  s.aspectRatio = (std::isnan(in.aspectRatio) || in.aspectRatio <= 0.0)
                      ? 0.0
                      : std::clamp(in.aspectRatio, 1.0e-6, 1.0e6);
  return s;
}

}  // namespace r1ui::core::layout
