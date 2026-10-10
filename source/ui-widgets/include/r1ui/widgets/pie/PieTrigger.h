// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PieTrigger, the container widget that turns "hold the right mouse button" inside its area into a
//   pie menu: it asks the host which pie applies at the pointer, runs the PieGesture, shows a PieMenu in
//   the overlay layer once the draw delay has passed, and on release executes the chosen slot's command
//   through the CommandRouter, or reports a click (fallback to the normal context menu) or a cancel.
// Why: owner requirement 2026-10-10 (pie menu opened by HOLDING the right mouse button; the host decides
//   the trigger area and context). The widget is the glue between the pure gesture, the drawing widget
//   and the command layer; it owns no menu data.
// Callers: the host (wraps the area in a PieTrigger and supplies the provider), tests. Calls: PieGesture,
//   PieMenu, the overlay manager, CommandRegistry / CommandRouter through CommandServices.
//
// Use: `PieTrigger& t = ui.create<PieTrigger>(parent, services, provider);` then create the area's
//   content as children of the trigger (style it like any flex container: it fills its parent by default).
//   `provider(x, y)` is called on every right-button press inside the area (window coordinates) and
//   returns the pie to use there, or nullopt for "none here": then the press is left alone and the host's
//   normal handling (its context menu) runs. A returned menu that is not a pie is treated as none.
// Events: the press is taken in the capture phase, so the area's children never see the right press that
//   opens a pie; the trigger captures the pointer and gets every move and the release (also outside the
//   window while the platform keeps the capture). A press while another button is held, or while a
//   gesture runs, is ignored.
// Outcomes (callbacks, all optional): onExecuted(commandId, result) after a slot ran (a refused or
//   throwing command is reported through the result and never breaks the gesture); onFallback(x, y) for a
//   quick click without movement (the host opens its context menu at the point); onCancelled() for
//   Escape, return to the centre, capture loss, window deactivation, a destroyed overlay.
// Escape: handled when focus is inside the trigger (key events bubble to it) and by the overlay while the
//   pie is drawn; hosts whose focus is elsewhere call cancelGesture() from their key handler.
// Draw delay and click time come from PieGestureConfig (150 ms and 180 ms by default). The pie is drawn
//   in an overlay, never clipped by the area; the overlay layer keeps it inside the window (the pie shifts
//   inward near an edge; selection still goes by the direction from the press point).
// Lifetime: the services and the provider's data must outlive the context. The trigger keeps WidgetIds and
//   timer ids only; destroying the trigger mid-gesture cancels it (the capture is released by the router).
// Threading: UI thread only.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "r1ui/commands/custommenu/CustomMenu.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/overlay/OverlayManager.h"
#include "r1ui/widgets/pie/PieGesture.h"
#include "r1ui/widgets/pie/PieMenu.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

// The slot views of a custom pie menu as the registry sees them right now (label and icon from the
// entry's override or the command, enabled, checked, missing).
std::vector<PieSlotView> pieSlotViews(const CommandServices& services, const commands::custommenu::CustomMenu& menu);

class PieTrigger : public WidgetObject {
 public:
  using Provider = std::function<std::optional<commands::custommenu::CustomMenu>(double x, double y)>;

  PieTrigger(CommandServices services, Provider provider) : services_(services), provider_(std::move(provider)) {}

  const char* typeName() const override { return "PieTrigger"; }
  void onAttached() override;
  void onDetached() override;
  uint8_t phases() const override;

  void setProvider(Provider provider) { provider_ = std::move(provider); }
  void setGestureConfig(const PieGestureConfig& config) { gesture_.setConfig(config); }
  void setOnExecuted(std::function<void(const std::string& commandId, const commands::ExecuteResult& result)> fn) { onExecuted_ = std::move(fn); }
  void setOnFallback(std::function<void(double x, double y)> fn) { onFallback_ = std::move(fn); }
  void setOnCancelled(std::function<void()> fn) { onCancelled_ = std::move(fn); }

  // ---- state (tests, host) ----
  bool gestureActive() const { return gesture_.active(); }
  bool pieDrawn() const { return gesture_.drawn(); }
  int highlighted() const { return gesture_.highlighted(); }
  // The PieMenu widget while the pie is drawn, else an invalid id.
  core::tree::WidgetId pieWidget() const { return pie_; }
  // Cancels a running gesture (Escape forwarded by a host key handler). The held button's release is
  // then swallowed so it opens nothing.
  void cancelGesture();

  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onDragStart(Event& e) override;
  void onClick(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;

 private:
  void startDrawTimer();
  void onDrawTimer();
  void showPie();
  void overlayClosed(DismissReason reason);
  void closePie();
  void endGesture(bool notifyCancelled);
  void updateHighlight();

  CommandServices services_;
  Provider provider_;
  PieGesture gesture_;
  std::function<void(const std::string&, const commands::ExecuteResult&)> onExecuted_;
  std::function<void(double, double)> onFallback_;
  std::function<void()> onCancelled_;

  commands::custommenu::CustomMenu menu_;  // the pie of the running gesture
  std::vector<PieSlotView> views_;
  uint32_t timer_ = 0;  // UiContext::TimerId
  OverlayId overlay_;
  core::tree::WidgetId pie_;
  bool swallowRelease_ = false;  // a cancelled gesture's button is still down: ignore until it is released
  bool swallowClick_ = false;    // the Click the router sends for the press that began a pie is stopped
  bool closingOverlay_ = false;  // we closed the overlay ourselves: its onClosed is not a cancel
};

}  // namespace r1ui::widgets
