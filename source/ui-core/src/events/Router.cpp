// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Router delivery (capture / target / bubble), hover tracking, pointer capture, press,
//   click, double-click and drag-start synthesis.
// Why: see Router.h for the rules. Every public entry point validates router state against the
//   tree first and re-checks ids after each handler, so handlers can mutate the tree freely.
// Callers: ui-platform, widget handlers, tests. Calls: TreeQueries, EventHandler.
#include "r1ui/core/events/Router.h"

#include <algorithm>
#include <cmath>

#include "r1ui/core/events/TreeQueries.h"

namespace r1ui::core::events {

using tree::kNoWidget;
using tree::WidgetId;

namespace {

bool finite2(double a, double b) { return std::isfinite(a) && std::isfinite(b); }

RouterConfig sanitize(RouterConfig c) {
  const RouterConfig defaults;
  if (!std::isfinite(c.dragThreshold) || c.dragThreshold < 0.0) c.dragThreshold = defaults.dragThreshold;
  if (!std::isfinite(c.doubleClickDistance) || c.doubleClickDistance < 0.0) {
    c.doubleClickDistance = defaults.doubleClickDistance;
  }
  return c;
}

}  // namespace

Router::Router(tree::WidgetTree& tree, WidgetId root, RouterConfig config)
    : tree_(tree), root_(root), config_(sanitize(config)) {}

void Router::setConfig(const RouterConfig& config) { config_ = sanitize(config); }

// ---- delivery ----

// Delivers `event` along the path root..target in the phases the mode allows. Handlers are
// looked up on the widget at the moment of the call, so a handler cleared or a widget destroyed
// by an earlier handler is simply skipped.
bool Router::deliver(Event& event, WidgetId target, Delivery mode) {
  if (depth_ >= kMaxDispatchDepth || !tree_.alive(target)) return false;
  struct DepthGuard {
    int& depth;
    explicit DepthGuard(int& d) : depth(d) { ++depth; }
    ~DepthGuard() { --depth; }
  } guard(depth_);

  // Typical paths are short: keep them on the stack and only allocate for unusually deep trees.
  constexpr size_t kInlinePath = 48;
  WidgetId inlinePath[kInlinePath];
  std::vector<WidgetId> deepPath;
  const size_t capacity = size_t{tree_.depth(target)} + 1;
  WidgetId* path = inlinePath;
  if (capacity > kInlinePath) {
    deepPath.resize(capacity);
    path = deepPath.data();
  }
  size_t n = 0;
  for (WidgetId cur = target; cur.valid() && n < capacity; cur = tree_.parent(cur)) path[n++] = cur;
  std::reverse(path, path + static_cast<std::ptrdiff_t>(n));

  event.target = target;
  const bool keyLike = mode == Delivery::KeyBubbling;
  // Runs one handler; returns false when delivery must stop.
  auto invoke = [&](size_t index, Phase phase, uint8_t bit) {
    const WidgetId id = path[index];
    tree::Widget* w = tree_.get(id);
    if (w == nullptr || w->handler == nullptr) return true;
    if (keyLike && !w->flags.enabled) return true;
    EventHandler* handler = w->handler;
    if ((handler->phases() & bit) == 0) return true;
    event.phase = phase;
    event.current = id;
    event.localX = event.x - w->absRect.x;
    event.localY = event.y - w->absRect.y;
    handler->onEvent(event, *this);
    return !(event.stopped || (keyLike && event.handled));
  };

  if (mode == Delivery::PointerBubbling) {
    for (size_t i = 0; i + 1 < n; ++i) {
      if (!invoke(i, Phase::Capture, kListenCapture)) return event.handled;
    }
  }
  if (!invoke(n - 1, Phase::Target, kListenTarget)) return event.handled;
  if (mode != Delivery::TargetOnly) {
    for (size_t i = n - 1; i-- > 0;) {
      if (!invoke(i, Phase::Bubble, kListenBubble)) break;
    }
  }
  return event.handled;
}

Event Router::makePointerEvent(EventType type, const PointerInput& input) const {
  Event e;
  e.type = type;
  e.x = input.x;
  e.y = input.y;
  e.button = input.button;
  e.buttons = buttons_;
  e.modifiers = input.modifiers;
  e.wheelX = input.wheelX;
  e.wheelY = input.wheelY;
  e.timestampMs = input.timestampMs;
  return e;
}

// ---- state queries ----

WidgetId Router::hovered() const {
  return (!hoverChain_.empty() && tree_.alive(hoverChain_.back())) ? hoverChain_.back() : kNoWidget;
}

WidgetId Router::capturer() const { return tree_.alive(capture_) ? capture_ : kNoWidget; }

WidgetId Router::hitTest(double x, double y) const { return events::hitTest(tree_, root_, x, y); }

// ---- hover ----

// Moves the hover chain to the ancestors of `newLeaf`: Leave to widgets that dropped out (leaf
// first), then Enter to widgets that joined (root first). Dead widgets get nothing.
void Router::updateHover(WidgetId newLeaf) {
  const WidgetId oldLeaf = hoverChain_.empty() ? kNoWidget : hoverChain_.back();
  if (newLeaf == oldLeaf && hoverVersion_ == tree_.structureVersion()) return;

  // `previous` and `next` stay stable while Enter / Leave handlers run (a handler may move the
  // pointer state again); their storage comes from the spares and goes back at the end.
  std::vector<WidgetId> next = std::move(hoverSpareB_);
  next.clear();
  if (tree_.alive(newLeaf)) {
    tree_.ancestorsOf(newLeaf, next);
    std::reverse(next.begin(), next.end());
  }
  std::vector<WidgetId> previous = std::move(hoverChain_);
  hoverChain_ = std::move(hoverSpareA_);
  hoverChain_.assign(next.begin(), next.end());
  hoverVersion_ = tree_.structureVersion();

  size_t common = 0;
  while (common < previous.size() && common < next.size() && previous[common] == next[common]) ++common;

  auto boundary = [&](EventType type, WidgetId target, WidgetId related) {
    Event e;
    e.type = type;
    e.x = lastX_;
    e.y = lastY_;
    e.buttons = buttons_;
    e.modifiers = lastModifiers_;
    e.related = related;
    deliver(e, target, Delivery::TargetOnly);
  };
  const WidgetId newTip = next.empty() ? kNoWidget : next.back();
  for (size_t i = previous.size(); i-- > common;) {
    if (tree_.alive(previous[i])) boundary(EventType::PointerLeave, previous[i], newTip);
  }
  for (size_t i = common; i < next.size(); ++i) {
    if (tree_.alive(next[i])) boundary(EventType::PointerEnter, next[i], oldLeaf);
  }
  previous.clear();
  hoverSpareA_ = std::move(previous);
  hoverSpareB_ = std::move(next);
}

void Router::refreshHoverFromLastPosition() {
  if (!havePointer_) return;
  updateHover(capture_.valid() ? capture_ : hitTest(lastX_, lastY_));
}

// ---- capture ----

bool Router::capturePointer(WidgetId widget) {
  if (!tree_.alive(widget) || !isEffectivelyShown(tree_, widget) || buttons_ == 0) return false;
  if (capture_ == widget) return true;
  const WidgetId previous = capture_;
  capture_ = widget;
  if (tree_.alive(previous)) {
    Event e;
    e.type = EventType::CaptureLost;
    e.buttons = buttons_;
    e.x = lastX_;
    e.y = lastY_;
    deliver(e, previous, Delivery::TargetOnly);
  }
  return true;
}

void Router::endCapture() {
  const WidgetId old = capture_;
  capture_ = kNoWidget;
  if (tree_.alive(old)) {
    Event e;
    e.type = EventType::CaptureLost;
    e.buttons = buttons_;
    e.x = lastX_;
    e.y = lastY_;
    deliver(e, old, Delivery::TargetOnly);
  }
  refreshHoverFromLastPosition();
}

void Router::releaseCapture() {
  if (capture_.valid()) endCapture();
}

void Router::cancelPointerInteraction() {
  press_ = Press{};
  lastClick_.valid = false;
  releaseCapture();
  // The OS may never deliver the matching releases (capture or focus lost elsewhere), and a
  // button left set would swallow the next press and make later moves look like a drag. A late
  // release for a cancelled button is ignored by pointerUp.
  buttons_ = 0;
}

// A destroyed capturer simply disappears (nothing can be delivered); a hidden one is released.
void Router::validateCapture() {
  if (!capture_.valid()) return;
  if (!tree_.alive(capture_)) {
    capture_ = kNoWidget;
  } else if (!isEffectivelyShown(tree_, capture_) || !isEffectivelyEnabled(tree_, capture_)) {
    endCapture();
  }
}

void Router::sync() {
  validateCapture();
  validateFocus();
  refreshHoverFromLastPosition();
}

// ---- pointer input ----

uint32_t Router::prospectiveClickCount(Button button, WidgetId target, const PointerInput& input) const {
  const LastClick& last = lastClick_;
  if (!last.valid || last.button != button || last.target != target) return 1;
  if (input.timestampMs < last.timestampMs || input.timestampMs - last.timestampMs > config_.doubleClickMs) {
    return 1;
  }
  const double dx = input.x - last.x;
  const double dy = input.y - last.y;
  if (dx * dx + dy * dy > config_.doubleClickDistance * config_.doubleClickDistance) return 1;
  return std::min<uint32_t>(last.count + 1, 65535u);
}

void Router::maybeStartDrag(const PointerInput& input) {
  if (!press_.active || press_.dragStarted) return;
  const double dx = input.x - press_.x;
  const double dy = input.y - press_.y;
  if (dx * dx + dy * dy <= config_.dragThreshold * config_.dragThreshold) return;  // strictly greater
  press_.dragStarted = true;
  if (!tree_.alive(press_.target)) return;
  Event e = makePointerEvent(EventType::DragStart, input);
  e.originX = press_.x;
  e.originY = press_.y;
  const bool accepted = deliver(e, press_.target, Delivery::PointerBubbling);
  if (press_.active) press_.dragAccepted = accepted;
}

bool Router::pointerMove(const PointerInput& input) {
  if (!finite2(input.x, input.y)) return false;
  validateCapture();
  validateFocus();
  havePointer_ = true;
  lastX_ = input.x;
  lastY_ = input.y;
  lastModifiers_ = input.modifiers;
  const WidgetId target = capture_.valid() ? capture_ : hitTest(input.x, input.y);
  updateHover(target);
  Event e = makePointerEvent(EventType::PointerMove, input);
  const bool handled = deliver(e, target, Delivery::PointerBubbling);
  maybeStartDrag(input);
  return handled;
}

bool Router::pointerDown(const PointerInput& input) {
  if (!finite2(input.x, input.y) || input.button == Button::None) return false;
  validateCapture();
  validateFocus();
  havePointer_ = true;
  lastX_ = input.x;
  lastY_ = input.y;
  lastModifiers_ = input.modifiers;
  const WidgetId target = capture_.valid() ? capture_ : hitTest(input.x, input.y);
  updateHover(target);

  const bool first = buttons_ == 0;
  buttons_ |= buttonBit(input.button);
  uint32_t count = 1;
  if (first) {
    count = prospectiveClickCount(input.button, target, input);
    press_ = Press{true, input.button, target, input.x, input.y, count, false, false, focusEpoch_};
  }
  Event e = makePointerEvent(EventType::PointerDown, input);
  e.clickCount = count;
  const bool handled = deliver(e, target, Delivery::PointerBubbling);

  // Focus follows an unhandled left press unless a handler already chose where focus goes.
  if (first && input.button == Button::Left && !handled && press_.active &&
      focusEpoch_ == press_.focusEpoch) {
    const WidgetId f = innermostFocusable(tree_, press_.target);
    if (f.valid()) setFocus(f, FocusReason::Pointer);
  }
  return handled;
}

bool Router::pointerUp(const PointerInput& input) {
  if (!finite2(input.x, input.y) || input.button == Button::None) return false;
  validateCapture();
  validateFocus();
  if ((buttons_ & buttonBit(input.button)) == 0) return false;  // release without a press
  havePointer_ = true;
  lastX_ = input.x;
  lastY_ = input.y;
  lastModifiers_ = input.modifiers;
  const WidgetId hit = hitTest(input.x, input.y);
  const WidgetId target = capture_.valid() ? capture_ : hit;
  updateHover(target);
  buttons_ &= ~buttonBit(input.button);

  const bool isPressButton = press_.active && press_.button == input.button;
  Event up = makePointerEvent(EventType::PointerUp, input);
  up.clickCount = isPressButton ? press_.clickCount : 1;
  const bool handled = deliver(up, target, Delivery::PointerBubbling);

  if (press_.active && press_.button == input.button) {
    const Press p = press_;
    press_ = Press{};  // cleared first: click handlers may begin a new interaction
    if (p.dragAccepted) {
      lastClick_.valid = false;  // a drag ends any click sequence
    } else if (tree_.alive(p.target)) {
      const WidgetId clickTarget = commonAncestor(tree_, p.target, hit);
      if (clickTarget.valid()) {
        lastClick_ = LastClick{true, input.button, p.target, input.x, input.y, input.timestampMs, p.clickCount};
        Event click = makePointerEvent(EventType::Click, input);
        click.clickCount = p.clickCount;
        deliver(click, clickTarget, Delivery::PointerBubbling);
        if (p.clickCount == 2) {
          Event dbl = makePointerEvent(EventType::DoubleClick, input);
          dbl.clickCount = 2;
          deliver(dbl, clickTarget, Delivery::PointerBubbling);
        }
      }
    }
  }
  if (buttons_ == 0 && capture_.valid()) endCapture();
  return handled;
}

bool Router::pointerWheel(const PointerInput& input) {
  if (!finite2(input.x, input.y) || !finite2(input.wheelX, input.wheelY)) return false;
  validateCapture();
  havePointer_ = true;
  lastX_ = input.x;
  lastY_ = input.y;
  lastModifiers_ = input.modifiers;
  const WidgetId target = capture_.valid() ? capture_ : hitTest(input.x, input.y);
  updateHover(target);
  Event e = makePointerEvent(EventType::PointerWheel, input);
  return deliver(e, target, Delivery::PointerBubbling);
}

void Router::pointerLeftWindow() {
  updateHover(kNoWidget);
  havePointer_ = false;
}

}  // namespace r1ui::core::events
