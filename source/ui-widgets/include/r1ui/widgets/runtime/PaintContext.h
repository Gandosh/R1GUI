// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: what a widget sees while it paints: the Painter, its absolute rectangle in logical and
//   physical pixels, the display scale, style resolution, animated values and the shared drawing
//   helpers (box from a resolved style, one line of text with ellipsis, icon, focus ring).
// Why: widget values are logical pixels while the Painter works in physical pixels with no hidden
//   scaling; funnelling every conversion through one object keeps that rule in one place and keeps
//   widgets free of Vulkan, text-engine and icon-cache details.
// Callers: UiContext paint traversal creates one per widget; widgets receive it in paint() and
//   paintOver(). Calls: UiContext (services, animation), Painter, TextEngine, IconCache.
// Units: rect() and every `logical` argument are logical pixels in window space; box(), toPhysical
//   and everything returned as render::Rect / float px are physical pixels.
// Failure behavior: an unknown style key throws std::logic_error; an unreadable icon throws
//   std::runtime_error (see IconCache); text that cannot be shaped draws nothing.
#pragma once

#include <optional>
#include <string_view>

#include "r1ui/core/layout/Geometry.h"
#include "r1ui/render/Painter.h"
#include "r1ui/theme/StyleSheet.h"

namespace r1ui::widgets {

class UiContext;
class WidgetObject;

enum class TextAlign : uint8_t { Start, Center, End };

struct TextOptions {
  double padLeft = 0.0;   // logical px inside the box before the text
  double padRight = 0.0;
  TextAlign align = TextAlign::Start;
  bool ellipsis = true;   // shorten with U+2026 when the text does not fit; false clips instead
  std::optional<render::Color> color;  // overrides the style's text colour
  int weight = -1;        // overrides the style's weight when >= 0
  bool tabular = false;   // digits with equal advances (OpenType tnum), as number fields show them
};

class PaintContext {
 public:
  PaintContext(UiContext& ui, render::Painter& painter, WidgetObject& widget, const core::layout::Rect& absRect, float scale);

  UiContext& ui() const { return ui_; }
  WidgetObject& widget() const { return widget_; }
  render::Painter& painter() const { return painter_; }
  float scale() const { return scale_; }

  // ---- geometry ----
  const core::layout::Rect& rect() const { return rect_; }
  render::Rect box() const { return toPhysical(rect_); }
  render::Rect toPhysical(const core::layout::Rect& logical) const;
  render::Rect toPhysical(double x, double y, double w, double h) const;
  float px(double logical) const { return static_cast<float>(logical) * scale_; }
  // One device-independent hairline: at least one physical pixel.
  float hairline() const;

  // ---- style and colour ----
  const theme::ResolvedStyle& resolve(std::string_view key, uint8_t state) const;
  // The key resolved with the widget's own styleState().
  const theme::ResolvedStyle& style(std::string_view key) const;
  render::Color color(std::string_view token, double opacity = 1.0) const;
  render::Color color(const theme::Color& c, double opacity = 1.0) const;

  // ---- animation (150 ms colour transitions; instant when the frame loop is not running) ----
  // Returns the value to draw now for animation slot `slot` of this widget, moving towards
  // `target`; asks for more frames while it is still moving. Slots are small per-widget integers.
  render::Color animatedColor(int slot, const render::Color& target) const;
  float animatedValue(int slot, float target) const;

  // ---- drawing helpers ----
  // Background and inside border of `box` (default box()) from a resolved style. Honors radius.
  void fillBox(const theme::ResolvedStyle& style) const { fillBox(style, box()); }
  void fillBox(const theme::ResolvedStyle& style, const render::Rect& box) const;
  // One line of text vertically centred (CSS half-leading) in `box`; returns the drawn width in
  // physical pixels. See TextOptions.
  float drawText(std::string_view text, const theme::TextStyle& style, const render::Rect& box, const TextOptions& options = {}) const;
  // Icon of `logicalSize` px centred in `box`.
  void drawIcon(std::string_view name, double logicalSize, const render::Rect& box, const render::Color& tint) const;
  // The focus ring ("focus.ring" style row) just outside-in on `box` (default box()).
  void focusRing(float radiusPhysical = -1.0f) const { focusRing(box(), radiusPhysical); }
  void focusRing(const render::Rect& box, float radiusPhysical = -1.0f) const;

 private:
  UiContext& ui_;
  render::Painter& painter_;
  WidgetObject& widget_;
  core::layout::Rect rect_;
  float scale_;
};

}  // namespace r1ui::widgets
