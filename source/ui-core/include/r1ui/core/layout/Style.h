// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the flexbox-style layout properties of one widget and their sanitising rules.
// Why: widgets describe layout declaratively; the engine (FlexLayout.h) is the only consumer, so
//   hostile values (NaN, infinity, negatives, 1e300) are normalised here once, never inside the
//   algorithm.
// Callers: tree::Widget (holds a Style), layout::FlexLayout, widget code that edits styles.
// Semantics: CSS flexbox with box-sizing: border-box (width/height/min/max include padding).
//   Intentionally unsupported: grid, float, margin collapsing, writing modes / RTL, automatic
//   minimum content size for flex items (min-width:auto behaves as 0), percentage padding and
//   margins, borders (a border is painted inside the padding box and has no layout effect),
//   baseline alignment (Align::Baseline lays out as Start), z-index (stacking is the tree's job).
// Sanitising (sanitizeStyle): NaN or negative sizes/basis/max become Auto; NaN or negative
//   min becomes 0; padding and gaps clamp to >= 0; margins and insets clamp to +-kMaxExtent
//   (NaN margin -> 0, NaN inset -> Auto); grow/shrink: NaN or negative -> 0 (shrink NaN -> 1);
//   aspect ratio NaN/<=0 -> none; every magnitude clamps to kMaxExtent so nothing overflows.
#pragma once

#include <cstdint>

#include "r1ui/core/layout/Geometry.h"

namespace r1ui::core::layout {

enum class Display : uint8_t { Flex, None };
enum class Position : uint8_t { Relative, Absolute };
enum class FlexDirection : uint8_t { Row, RowReverse, Column, ColumnReverse };
enum class FlexWrap : uint8_t { NoWrap, Wrap, WrapReverse };
enum class Justify : uint8_t { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
// Cross-axis alignment of an item. Auto is only meaningful for alignSelf (use the container's).
enum class Align : uint8_t { Auto, Start, Center, End, Stretch, Baseline };
// Distribution of wrapped lines along the cross axis.
enum class AlignContent : uint8_t { Stretch, Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
enum class Overflow : uint8_t { Visible, Hidden };

// A CSS-like dimension. FitContent means "shrink to the content, never stretch".
struct Length {
  enum class Kind : uint8_t { Auto, Px, Percent, FitContent };
  Kind kind = Kind::Auto;
  double value = 0.0;

  static constexpr Length autoValue() { return Length{Kind::Auto, 0.0}; }
  static constexpr Length px(double v) { return Length{Kind::Px, v}; }
  static constexpr Length percent(double v) { return Length{Kind::Percent, v}; }
  static constexpr Length fitContent() { return Length{Kind::FitContent, 0.0}; }
  friend bool operator==(const Length&, const Length&) = default;
};

// Index into the four-edge arrays of Style.
enum Edge : int { kLeft = 0, kTop = 1, kRight = 2, kBottom = 3 };

struct Style {
  Display display = Display::Flex;
  Position position = Position::Relative;
  FlexDirection direction = FlexDirection::Row;
  FlexWrap wrap = FlexWrap::NoWrap;
  Justify justifyContent = Justify::Start;
  Align alignItems = Align::Stretch;
  Align alignSelf = Align::Auto;
  AlignContent alignContent = AlignContent::Stretch;
  Overflow overflow = Overflow::Visible;
  // True for a leaf whose content size comes from the host's MeasureProvider (text, images).
  // A measured widget's children, if any, are ignored by layout.
  bool hasMeasure = false;

  double flexGrow = 0.0;
  double flexShrink = 1.0;
  Length flexBasis;  // Auto = use width/height, then content

  Length width;
  Length height;
  Length minWidth;
  Length minHeight;
  Length maxWidth;   // Auto = no maximum
  Length maxHeight;

  double padding[4] = {0, 0, 0, 0};                                     // px, indexed by Edge
  Length margin[4] = {Length::px(0), Length::px(0), Length::px(0), Length::px(0)};  // px or Auto
  Length inset[4];   // left/top/right/bottom offsets, used only when position == Absolute

  double gapRow = 0.0;     // space between wrapped lines of a row container / between column items
  double gapColumn = 0.0;  // space between row items / between wrapped lines of a column container
  double aspectRatio = 0.0;  // width / height; <= 0 means none
};

// Returns a copy whose every field satisfies the invariants the engine relies on (see header).
Style sanitizeStyle(const Style& style);

}  // namespace r1ui::core::layout
