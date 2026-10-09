// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of OverlayWatch.h.
// Callers: UiContext (layout callbacks).
#include "r1ui/widgets/popover/OverlayWatch.h"

#include "r1ui/core/events/TreeQueries.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

void OverlayWatch::onAttached() {
  core::layout::Style& s = style();
  s.position = core::layout::Position::Absolute;
  s.width = core::layout::Length::px(0);
  s.height = core::layout::Length::px(0);
  node().flags.hitTestTransparent = true;
  setWantsLayoutCallback(true);
  if (anchor_.valid()) lastAnchor_ = ui().absRect(anchor_);  // the overlay was opened at this rectangle
  arm();
}

void OverlayWatch::onDetached() {
  if (timer_ != 0) ui().cancelTimer(timer_);
  timer_ = 0;
}

void OverlayWatch::arm() {
  const core::tree::WidgetId self = id();
  UiContext* u = &ui();
  timer_ = u->setTimer(kPollMs, [u, self]() {
    if (OverlayWatch* watch = u->objectAs<OverlayWatch>(self)) {
      watch->timer_ = 0;
      watch->check();
      if (u->alive(self)) watch->arm();
    }
  });
}

void OverlayWatch::check() {
  UiContext& u = ui();
  const auto gone = [&](core::tree::WidgetId w) { return w.valid() && !core::events::isEffectivelyShown(u.tree(), w); };
  if (gone(owner_) || gone(anchor_)) {
    u.overlays().close(overlay_, DismissReason::Programmatic);  // destroys this widget; the dispatch is still safe
    return;
  }
  if (pendingFocus_ && core::events::isEffectivelyShown(u.tree(), id()) && core::events::isEffectivelyShown(u.tree(), u.overlays().hostOf(overlay_))) {
    // The overlay is placed and visible: focus the first focusable widget of the body, else the fallback.
    pendingFocus_ = false;
    core::tree::WidgetId target = focusRoot_.valid() && u.alive(focusRoot_) ? core::events::nextFocusable(u.tree(), focusRoot_, {}, false) : core::tree::WidgetId{};
    if (!target.valid()) target = focusFallback_;
    if (target.valid() && u.alive(target)) u.router().focus(target, core::events::FocusReason::Program);
  }
  if (!anchor_.valid()) return;
  const core::layout::Rect rect = u.absRect(anchor_);
  if (rect.empty() || rect == lastAnchor_) return;
  lastAnchor_ = rect;
  u.overlays().setAnchor(overlay_, rect);
}

}  // namespace r1ui::widgets
