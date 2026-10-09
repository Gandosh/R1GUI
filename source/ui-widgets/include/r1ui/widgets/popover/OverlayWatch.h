// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: OverlayWatch, a zero-size invisible child of an overlay host that keeps the overlay in step
//   with the widgets it belongs to: it re-anchors the overlay when the anchor widget moves or
//   resizes (a popover under a button that is scrolled or laid out again) and closes the overlay when
//   the anchor widget or the owner widget is destroyed or hidden.
// Why: the overlay layer places a popup once from the rectangle it was given; nothing tells it that
//   the trigger has moved or gone. Popovers and dialogs must not outlive their owner (a dialog whose
//   owning panel was closed, a popover whose button was removed), and every popup that follows an
//   anchor needs the same check, so it is one small widget instead of per-widget code.
// Callers: Popover (anchor and owner), Dialog (owner). Calls: OverlayManager::setAnchor / close.
// Timing: the check runs after every layout pass (destroying and moving widgets cause one) and every
//   kPollMs on the context clock (hiding a widget needs no layout), so a vanished anchor or owner is
//   noticed within about 120 ms even while the window is otherwise idle.
// Invariants: the watch only holds ids; closing the overlay from onLayout is safe because widget
//   objects live until the dispatch ends.
#pragma once

#include "r1ui/core/layout/Geometry.h"
#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/overlay/OverlayManager.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class OverlayWatch final : public WidgetObject {
 public:
  // `anchor` may be invalid (nothing to follow); `owner` may be invalid (nothing to outlive).
  OverlayWatch(OverlayId overlay, core::tree::WidgetId anchor, core::tree::WidgetId owner)
      : overlay_(overlay), anchor_(anchor), owner_(owner) {}
  const char* typeName() const override { return "OverlayWatch"; }
  void onAttached() override;
  void onDetached() override;
  void onLayout() override { check(); }
  // Gives keyboard-less initial focus (no focus ring) once the overlay is visible: to the first
  // focusable widget inside `root`, or to `fallback` when `root` has none. Used by dialogs, whose
  // body is filled after the overlay was opened.
  void setInitialFocus(core::tree::WidgetId root, core::tree::WidgetId fallback) {
    focusRoot_ = root;
    focusFallback_ = fallback;
    pendingFocus_ = true;
  }

 private:
  static constexpr uint64_t kPollMs = 120;
  void check();
  void arm();

  OverlayId overlay_;
  core::tree::WidgetId anchor_;
  core::tree::WidgetId owner_;
  core::layout::Rect lastAnchor_;
  core::tree::WidgetId focusRoot_;
  core::tree::WidgetId focusFallback_;
  bool pendingFocus_ = false;
  UiContext::TimerId timer_ = 0;
};

}  // namespace r1ui::widgets
