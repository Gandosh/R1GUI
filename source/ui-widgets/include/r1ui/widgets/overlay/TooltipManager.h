// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tooltip timing and lifecycle for one UiContext (spec 10, rules 1-18): the rest delay, the
//   show delay, the hide rules, which widget supplies the text, and the overlay that displays it.
// Why: every widget may carry tooltip text; the behaviour (wait, show, follow the pointer, never
//   while a button is held, close on press or modal) belongs in one place rather than in widgets.
// Callers: UiContext feeds it pointer moves, presses, window state and the clock; it opens and
//   closes a non-interactive Tooltip overlay through the OverlayManager. Calls: OverlayManager,
//   Placement, WidgetObject::tooltipText().
// Timing (defaults from spec 10): the pointer must rest 50 ms over a widget with tooltip text, the
//   tooltip is then created hidden and becomes visible 150 ms later, fading in over 100 ms. When the
//   pointer moves to a widget with a different tooltip the old one closes at once and the wait
//   starts again; moves inside the same widget keep the tooltip, which follows the pointer. No
//   tooltip appears while a pointer button is held, while the window is inactive, while the pointer
//   is outside the window, or while another overlay is modal; a press, a drag, a modal opening or
//   the source widget disappearing closes it.
// Known gaps: disabled widgets are not hit by the Router, so they supply no tooltip (spec rule 4
//   wants them to); the slide-in offset of rule 3 is not drawn (fade only).
// Threading: UI thread only.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/overlay/OverlayManager.h"

namespace r1ui::widgets {

class UiContext;

struct TooltipTiming {
  uint64_t restMs = 50;
  uint64_t showDelayMs = 150;
  uint64_t fadeMs = 100;
};

class TooltipManager {
 public:
  explicit TooltipManager(UiContext& ui) : ui_(ui) {}

  void setTiming(const TooltipTiming& timing) { timing_ = timing; }
  const TooltipTiming& timing() const { return timing_; }
  // Replaces the default content (one Label with the source's text) of newly shown tooltips: the
  // factory adds children to `host` and returns true, or returns false to get the default label.
  // Used by the rich tooltip (title, shortcut, description) of the tooltip widget folder.
  using ContentFactory = std::function<bool(UiContext&, core::tree::WidgetId host, core::tree::WidgetId source, const std::string& text)>;
  void setContentFactory(ContentFactory factory) { factory_ = std::move(factory); }
  // Globally switches tooltips off (spec rule 7); closes a visible one.
  void setEnabled(bool enabled);
  bool enabled() const { return enabled_; }

  // ---- input from UiContext ----
  void onPointerMoved(double x, double y);
  void onPointerPressed();   // any button press, drag start, wheel: closes and suppresses until the next move
  void onPointerLeftWindow();
  void onModalOpened();
  // Closes the tooltip and forgets the pending one.
  void hide();

  // ---- clock ----
  // Advances the state machine; true when the tooltip appeared, moved or closed (needs a frame).
  bool tick();
  // Milliseconds until tick() has something to do; nullopt when idle.
  std::optional<uint64_t> msUntilTick() const;
  // After layout: positions the visible tooltip by its real size (placeTooltip); true when it moved,
  // which needs another placement pass in the same frame.
  bool afterLayout();

  // ---- queries (tests) ----
  bool visible() const { return overlay_.valid(); }
  core::tree::WidgetId source() const { return source_; }
  OverlayHandle overlay() const { return overlay_; }
  const std::string& text() const { return text_; }

 private:
  bool mayShow() const;
  core::tree::WidgetId findSource(core::tree::WidgetId hovered) const;
  void show();
  void closeOverlay();

  UiContext& ui_;
  TooltipTiming timing_;
  ContentFactory factory_;
  bool enabled_ = true;
  core::tree::WidgetId source_;      // widget whose tooltip is pending or shown
  std::string text_;
  uint64_t lastMoveMs_ = 0;
  bool suppressed_ = false;          // set by a press until the pointer moves again
  bool waiting_ = false;             // a source is waiting for the rest + show delay
  double pointerX_ = 0.0;
  double pointerY_ = 0.0;
  OverlayHandle overlay_;
  double placedX_ = -1e9;
  double placedY_ = -1e9;
};

}  // namespace r1ui::widgets
