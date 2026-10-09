// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PaintContext.h (unit conversion and the shared drawing helpers).
// Invariants: every value handed to the Painter is physical and finite; helpers never draw outside
//   the box they are given except the shadow of OverlayHost and the focus ring (which is inside).
// Callers: widgets via paint().
#include "r1ui/widgets/runtime/PaintContext.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

PaintContext::PaintContext(UiContext& ui, render::Painter& painter, WidgetObject& widget, const core::layout::Rect& absRect, float scale)
    : ui_(ui), painter_(painter), widget_(widget), rect_(absRect), scale_(scale) {}

render::Rect PaintContext::toPhysical(const core::layout::Rect& r) const {
  return {static_cast<float>(r.x) * scale_, static_cast<float>(r.y) * scale_, static_cast<float>(r.w) * scale_, static_cast<float>(r.h) * scale_};
}

render::Rect PaintContext::toPhysical(double x, double y, double w, double h) const {
  return {px(x), px(y), px(w), px(h)};
}

float PaintContext::hairline() const { return std::max(1.0f, std::round(scale_)); }

theme::ResolvedStyle PaintContext::resolve(std::string_view key, uint8_t state) const { return ui_.services().resolve(key, state); }

theme::ResolvedStyle PaintContext::style(std::string_view key) const { return resolve(key, widget_.styleState()); }

render::Color PaintContext::color(std::string_view token, double opacity) const { return ui_.services().color(token, opacity); }

render::Color PaintContext::color(const theme::Color& c, double opacity) const { return Services::toRender(c, opacity); }

render::Color PaintContext::animatedColor(int slot, const render::Color& target) const {
  return ui_.animatedColor(widget_.id(), slot, target);
}

float PaintContext::animatedValue(int slot, float target) const { return ui_.animatedValue(widget_.id(), slot, target); }

void PaintContext::fillBox(const theme::ResolvedStyle& s, const render::Rect& box) const {
  const render::CornerRadii radii = render::CornerRadii::uniform(px(s.radius));
  if (s.background.a > 0) painter_.fillRoundedRect(box, radii, color(s.background));
  if (s.border.width > 0.0 && s.border.color.a > 0) painter_.border(box, radii, px(s.border.width), color(s.border.color));
}

float PaintContext::drawText(std::string_view text, const theme::TextStyle& ts, const render::Rect& box, const TextOptions& options) const {
  if (text.empty()) return 0.0f;
  TextEngine& engine = ui_.text();
  const float size = static_cast<float>(ts.fontSize) * scale_;
  const int weight = options.weight >= 0 ? options.weight : ts.weight;
  const float left = box.x + px(options.padLeft);
  const float room = std::max(0.0f, box.w - px(options.padLeft) - px(options.padRight));
  std::string_view shown = text;
  float width = 0.0f;
  bool clip = false;
  if (options.ellipsis) {
    const FittedText& fitted = engine.fit(text, size, room, options.tabular);
    shown = fitted.text;
    width = fitted.width;
    // The bare ellipsis can still be wider than a tiny box.
    clip = width > room + 0.5f;
  } else {
    width = engine.measure(text, size, weight, options.tabular);
    clip = width > room + 0.5f;
  }
  float x = left;
  if (options.align == TextAlign::Center) x = left + (room - width) * 0.5f;
  else if (options.align == TextAlign::End) x = left + room - width;
  if (clip) x = left;  // overflowing text always starts at the start edge
  const float baseline = box.y + engine.baselineInBox(size, box.h);
  const render::Color tint = options.color ? *options.color : color(ts.color);
  if (clip) painter_.pushClip(box);
  engine.draw(painter_, shown, size, weight, x, baseline, tint, options.tabular);
  if (clip) painter_.popClip();
  return width;
}

void PaintContext::drawIcon(std::string_view name, double logicalSize, const render::Rect& box, const render::Color& tint, std::string_view fallback) const {
  const int size = std::max(1, static_cast<int>(std::lround(logicalSize * static_cast<double>(scale_))));
  const float s = static_cast<float>(size);
  ui_.icons().draw(painter_, name, box.x + (box.w - s) * 0.5f, box.y + (box.h - s) * 0.5f, size, tint, fallback);
}

void PaintContext::focusRing(const render::Rect& box, float radiusPhysical) const {
  const theme::ResolvedStyle& rs = resolve("focus.ring", 0);
  const float radius = radiusPhysical >= 0.0f ? radiusPhysical : px(rs.radius);
  painter_.border(box, render::CornerRadii::uniform(radius), px(rs.border.width), color(rs.border.color));
}

}  // namespace r1ui::widgets
