// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of OverlayHost.h: style rows, absolute-layout setup, surface painting.
// Callers: OverlayManager, UiContext (rows registration through create<T>).
#include "r1ui/widgets/overlay/OverlayHost.h"

#include <algorithm>
#include <vector>

#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using theme::State::kNone;
using theme::StyleProperty;

constexpr theme::StyleRuleEntry kHostRows[] = {
    {"overlay.popover", kNone, StyleProperty::Background, "color:panel"},
    {"overlay.popover", kNone, StyleProperty::BorderColor, "color:border"},
    {"overlay.popover", kNone, StyleProperty::BorderWidth, "number:1"},
    {"overlay.popover", kNone, StyleProperty::Radius, "metric:popover.radius"},
    {"overlay.menu", kNone, StyleProperty::Background, "color:panel"},
    {"overlay.menu", kNone, StyleProperty::BorderColor, "color:border"},
    {"overlay.menu", kNone, StyleProperty::BorderWidth, "number:1"},
    {"overlay.menu", kNone, StyleProperty::Radius, "metric:menu.radius"},
    {"overlay.menu", kNone, StyleProperty::PaddingX, "metric:menu.padding"},
    {"overlay.menu", kNone, StyleProperty::PaddingY, "metric:menu.padding"},
    {"overlay.tooltip", kNone, StyleProperty::Background, "color:panel"},
    {"overlay.tooltip", kNone, StyleProperty::BorderColor, "color:border"},
    {"overlay.tooltip", kNone, StyleProperty::BorderWidth, "number:1"},
    {"overlay.tooltip", kNone, StyleProperty::Radius, "metric:tooltip.radius"},
    {"overlay.tooltip", kNone, StyleProperty::PaddingX, "metric:tooltip.padX"},
    {"overlay.tooltip", kNone, StyleProperty::PaddingY, "metric:tooltip.padY"},
    {"overlay.dialog", kNone, StyleProperty::Background, "color:panel"},
    {"overlay.dialog", kNone, StyleProperty::BorderColor, "color:border"},
    {"overlay.dialog", kNone, StyleProperty::BorderWidth, "number:1"},
    {"overlay.dialog", kNone, StyleProperty::Radius, "metric:dialog.radius"},
};

constexpr theme::StyleRuleEntry kBlockerRows[] = {
    {"overlay.scrim", kNone, StyleProperty::Background, "#00000080"},
};

const char* shadowName(OverlaySurface s) {
  switch (s) {
    case OverlaySurface::Popover: return "xl";
    case OverlaySurface::Menu: return "lg";
    case OverlaySurface::Tooltip: return "lg";
    case OverlaySurface::Dialog: return "2xl";
    case OverlaySurface::None: return "";
  }
  return "";
}

void makeAbsoluteFill(core::layout::Style& s) {
  s.position = core::layout::Position::Absolute;
  for (int e = 0; e < 4; ++e) s.inset[e] = core::layout::Length::px(0);
}

}  // namespace

// ---- OverlayLayer -----------------------------------------------------------------------------

void OverlayLayer::onAttached() {
  makeAbsoluteFill(style());
  node().layer = 10000;                   // above every normal widget (hit testing and painting)
  node().flags.hitTestTransparent = true; // only its children are hit
}

// ---- OverlayHost ------------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> OverlayHost::styleRows() { return kHostRows; }

const char* OverlayHost::styleKey() const {
  switch (surface_) {
    case OverlaySurface::Popover: return "overlay.popover";
    case OverlaySurface::Menu: return "overlay.menu";
    case OverlaySurface::Tooltip: return "overlay.tooltip";
    case OverlaySurface::Dialog: return "overlay.dialog";
    case OverlaySurface::None: return "";
  }
  return "";
}

void OverlayHost::onAttached() {
  core::layout::Style& s = style();
  s.position = core::layout::Position::Absolute;
  s.inset[core::layout::kLeft] = core::layout::Length::px(0);
  s.inset[core::layout::kTop] = core::layout::Length::px(0);
  s.direction = core::layout::FlexDirection::Column;
  s.alignItems = core::layout::Align::Stretch;
  if (surface_ != OverlaySurface::None) {
    const theme::ResolvedStyle& rs = ui().services().resolve(styleKey(), 0);
    // The border is drawn inside the box but has no layout effect, so it is part of the padding:
    // content placed in the host never has to add a pixel of its own.
    s.padding[core::layout::kLeft] = s.padding[core::layout::kRight] = rs.paddingX + rs.border.width;
    s.padding[core::layout::kTop] = s.padding[core::layout::kBottom] = rs.paddingY + rs.border.width;
  }
  node().flags.hitTestTransparent = !interactive_;
  ui().invalidator().setVisible(id(), false);  // shown by the manager once placed
}

float OverlayHost::paintOpacity() const {
  if (fadeInMs_ <= 0.0 || !shown_ || !ui().animationsActive()) return 1.0f;
  const double t = static_cast<double>(ui().now() - shownAtMs_) / fadeInMs_;
  if (t >= 1.0) return 1.0f;
  ui().invalidator().requestAnimation(id());
  return static_cast<float>(std::max(0.0, t));
}

void OverlayHost::paint(PaintContext& ctx) {
  if (surface_ == OverlaySurface::None) return;
  const render::Rect box = ctx.box();
  const theme::ResolvedStyle& rs = ctx.resolve(styleKey(), 0);
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(rs.radius));
  // Shadows are issued last to first so the first listed layer ends up on top (Painter.h).
  const auto layers = ctx.ui().services().tokens().shadow(shadow_.empty() ? shadowName(surface_) : shadow_.c_str());
  if (layers) {
    for (auto it = layers->rbegin(); it != layers->rend(); ++it) {
      render::ShadowSpec spec;
      spec.offsetX = ctx.px(it->offsetX);
      spec.offsetY = ctx.px(it->offsetY);
      spec.blur = ctx.px(it->blur);
      spec.spread = ctx.px(it->spread);
      spec.color = ctx.color(it->color);
      ctx.painter().shadow(box, radii, spec);
    }
  }
  ctx.fillBox(rs, box);
}

// ---- OverlayBlocker ---------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> OverlayBlocker::styleRows() { return kBlockerRows; }

void OverlayBlocker::onAttached() { makeAbsoluteFill(style()); }

void OverlayBlocker::paint(PaintContext& ctx) {
  if (!scrim_) return;
  ctx.fillBox(ctx.resolve("overlay.scrim", 0));
}

}  // namespace r1ui::widgets
