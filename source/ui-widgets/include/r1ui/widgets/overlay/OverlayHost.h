// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the generic widgets of the overlay layer: OverlayLayer (the full-window absolute container
//   above the normal tree), OverlayHost (the popup surface: shadow, background, border, radius,
//   padding; the popup content are its children) and OverlayBlocker (the input blocker and scrim
//   behind a modal overlay).
// Why: the OverlayManager needs concrete widgets; menus, selects, popovers, tooltips and dialogs
//   differ only in surface styling and content, so the surface is data (style rows overlay.*)
//   chosen by OverlaySurface, and every popup widget builds on the same host.
// Callers: OverlayManager creates them; popup widgets add children to the host.
// Style rows (registered automatically, see Services): overlay.popover, overlay.menu,
//   overlay.tooltip, overlay.dialog (background, border, radius, padding from the widget metrics in
//   tokens.json) and overlay.scrim. Shadows come from the tokens: popover xl, menu lg, tooltip lg,
//   dialog 2xl.
// Layout: the host is an absolutely positioned flex column whose insets the manager sets when it
//   places the popup; give the content children normal flex styles. Until placed the host is
//   invisible (it still lays out so it can be measured).
#pragma once

#include <span>
#include <string>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/overlay/OverlayManager.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class OverlayLayer final : public WidgetObject {
 public:
  const char* typeName() const override { return "OverlayLayer"; }
  void onAttached() override;
};

class OverlayHost final : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();

  // `shadow` names a shadow token that replaces the surface's default (empty = default).
  OverlayHost(OverlaySurface surface, double fadeInMs, bool interactive, std::string shadow = {})
      : surface_(surface), fadeInMs_(fadeInMs), interactive_(interactive), shadow_(std::move(shadow)) {}
  const char* typeName() const override { return "OverlayHost"; }
  void onAttached() override;
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;

  OverlaySurface surface() const { return surface_; }
  // Called by the manager when the host becomes visible (starts the fade-in clock).
  void markShown(uint64_t nowMs) { shownAtMs_ = nowMs; shown_ = true; }
  const char* styleKey() const;

 private:
  OverlaySurface surface_;
  double fadeInMs_;
  bool interactive_;
  std::string shadow_;
  bool shown_ = false;
  uint64_t shownAtMs_ = 0;
};

class OverlayBlocker final : public WidgetObject {
 public:
  explicit OverlayBlocker(bool scrim) : scrim_(scrim) {}
  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "OverlayBlocker"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  // Swallows every pointer event: nothing behind a modal overlay may react.
  void onPointerDown(Event& e) override { swallow(e); }
  void onPointerUp(Event& e) override { swallow(e); }
  void onPointerMove(Event& e) override { swallow(e); }
  void onPointerWheel(Event& e) override { swallow(e); }
  void onClick(Event& e) override { swallow(e); }
  void onDoubleClick(Event& e) override { swallow(e); }

 private:
  static void swallow(Event& e) {
    e.markHandled();
    e.stopPropagation();
  }
  bool scrim_;
};

}  // namespace r1ui::widgets
