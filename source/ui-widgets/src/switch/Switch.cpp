// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Switch.h: style rows, size and painting.
// Invariants: the node's size always equals the size class (fixed style, no measure); the thumb
//   travels from kThumbInset to kThumbInset + travel.
// Callers: UiContext (rows registered through create<T>), tests.
#include "r1ui/widgets/switch/Switch.h"

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using theme::State::kDisabled;
using theme::State::kMixed;
using theme::State::kNone;
using theme::State::kSelected;
using theme::StyleProperty;

constexpr double kThumbInset = 3.0;  // 1 px border + 2 px padding

constexpr theme::StyleRuleEntry kRows[] = {
    {"switch.track", kNone, StyleProperty::Background, "color:panel-field"},
    {"switch.track", kNone, StyleProperty::BorderColor, "color:border"},
    {"switch.track", kNone, StyleProperty::BorderWidth, "number:1"},
    {"switch.track", kSelected, StyleProperty::Background, "color:accent"},
    {"switch.track", kSelected, StyleProperty::BorderColor, "color:accent"},
    {"switch.track", kMixed, StyleProperty::Background, "color:accent@0.2"},
    {"switch.track", kMixed, StyleProperty::BorderColor, "color:accent@0.6"},
    {"switch.track", kDisabled, StyleProperty::Opacity, "number:0.5"},
    {"switch.thumb", kNone, StyleProperty::Background, "color:muted"},
    {"switch.thumb", kSelected, StyleProperty::Background, "#ffffff"},
    {"switch.thumb", kMixed, StyleProperty::Background, "color:accent"},
};

struct Geometry {
  double w, h, thumb;
};

// Sizes come from the widget metrics of tokens.json (switch.sm / switch.md).
Geometry geometry(const theme::Tokens& tokens, SwitchSize s) {
  const char* prefix = s == SwitchSize::Sm ? "sm." : "md.";
  auto get = [&](const char* key, double fallback) { return tokens.widgetMetric("switch", std::string(prefix) + key).value_or(fallback); };
  return s == SwitchSize::Sm ? Geometry{get("w", 28), get("h", 16), get("thumb", 12)} : Geometry{get("w", 36), get("h", 20), get("thumb", 16)};
}

}  // namespace

std::span<const theme::StyleRuleEntry> Switch::styleRows() { return kRows; }

void Switch::onAttached() {
  Pressable::onAttached();
  style().flexShrink = 0.0;
  applySize();
}

void Switch::applySize() {
  const Geometry g = geometry(ui().services().tokens(), size_);
  style().width = core::layout::Length::px(g.w);
  style().height = core::layout::Length::px(g.h);
  requestLayout();
  requestPaint();
}

void Switch::setSize(SwitchSize size) {
  if (size == size_) return;
  size_ = size;
  applySize();
}

void Switch::setChecked(bool checked) {
  setMixed(false);
  setSelected(checked);
}

void Switch::activate() {
  const bool next = mixed() ? true : !checked();
  setChecked(next);
  if (onChange_) onChange_(next);
}

float Switch::paintOpacity() const { return static_cast<float>(ui().services().resolve("switch.track", styleState()).opacity); }

void Switch::paint(PaintContext& ctx) {
  const Geometry g = geometry(ctx.ui().services().tokens(), size_);
  const theme::ResolvedStyle& track = ctx.style("switch.track");
  const theme::ResolvedStyle& thumb = ctx.style("switch.thumb");
  const render::Rect box = ctx.box();
  const render::CornerRadii pill = render::CornerRadii::uniform(box.h * 0.5f);
  ctx.painter().fillRoundedRect(box, pill, ctx.animatedColor(0, ctx.color(track.background)));
  ctx.painter().border(box, pill, ctx.px(track.border.width), ctx.animatedColor(1, ctx.color(track.border.color)));

  const double travel = g.thumb;  // measured: the travel equals the thumb diameter (12 / 16)
  const float t = ctx.animatedValue(3, mixed() ? 0.5f : (checked() ? 1.0f : 0.0f));
  const double d = g.thumb;
  const core::layout::Rect& r = ctx.rect();
  const double x = r.x + kThumbInset + travel * static_cast<double>(t);
  const double y = r.y + (r.h - d) * 0.5;
  const render::Rect tb = ctx.toPhysical(x, y, d, d);
  const render::CornerRadii round = render::CornerRadii::uniform(tb.w * 0.5f);
  // shadow-sm under the thumb; layers are issued last to first (Painter.h).
  if (const auto layers = ctx.ui().services().tokens().shadow("sm")) {
    for (auto it = layers->rbegin(); it != layers->rend(); ++it) {
      render::ShadowSpec spec;
      spec.offsetX = ctx.px(it->offsetX);
      spec.offsetY = ctx.px(it->offsetY);
      spec.blur = ctx.px(it->blur);
      spec.spread = ctx.px(it->spread);
      spec.color = ctx.color(it->color);
      ctx.painter().shadow(tb, round, spec);
    }
  }
  ctx.painter().fillRoundedRect(tb, round, ctx.animatedColor(2, ctx.color(thumb.background)));
}

void Switch::paintOver(PaintContext& ctx) {
  if (!focusVisible()) return;
  const core::layout::Rect& r = ctx.rect();
  const render::Rect ring = ctx.toPhysical(r.x - 1.0, r.y - 1.0, r.w + 2.0, r.h + 2.0);
  ctx.focusRing(ring, ring.h * 0.5f);
}

}  // namespace r1ui::widgets
