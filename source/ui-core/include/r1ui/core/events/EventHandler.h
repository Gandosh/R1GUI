// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the small virtual interfaces through which widgets, the application and the focus
//   owner receive events from the Router.
// Why: virtual handler objects instead of std::function keep dispatch allocation-free and make
//   handler lifetime explicit: a Widget stores a non-owning EventHandler*, the owner keeps the
//   object alive for as long as the widget exists (or clears the pointer first). One virtual
//   call per widget per phase is the whole cost of delivery.
// Callers: events::Router calls these; widget code and the application implement them.
// Re-entrancy: a handler may destroy, hide or reparent widgets (including its own) and call
//   Router methods; the Router re-validates every id after each call. The Router limits nested
//   dispatch depth (kMaxDispatchDepth) so a handler that re-enters unconditionally cannot
//   exhaust the stack: deeper events are dropped.
#pragma once

#include <cstdint>

#include "r1ui/core/events/Event.h"

namespace r1ui::core::events {

class Router;

class EventHandler {
 public:
  virtual ~EventHandler() = default;
  // Which phases this handler wants (kListen* bits). Default: target and bubble.
  virtual uint8_t phases() const { return kListenTarget | kListenBubble; }
  virtual void onEvent(Event& event, Router& router) = 0;
};

// Last stop for key presses nobody used (application-wide shortcuts, spec 01 rule 16 step 6).
class GlobalKeyHandler {
 public:
  virtual ~GlobalKeyHandler() = default;
  // Return true when the key was used.
  virtual bool onGlobalKey(const Event& event, Router& router) = 0;
};

// Notified whenever keyboard focus changes, including when it is cleared because the focused
// widget was destroyed or hidden. `next` is invalid when focus was cleared. Use it to remember
// and restore focus (save `previous`, later call Router::restoreFocus).
class FocusObserver {
 public:
  virtual ~FocusObserver() = default;
  virtual void onFocusChanged(tree::WidgetId previous, tree::WidgetId next, FocusReason reason,
                              Router& router) = 0;
};

}  // namespace r1ui::core::events
