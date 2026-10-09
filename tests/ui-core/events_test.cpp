// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for events::Router and the tree queries behind it: hit testing (stacking, clips,
//   disabled / hidden / transparent widgets), capture and bubble delivery, hover enter / leave,
//   pointer capture, click / double-click / drag synthesis, focus and Tab traversal, keyboard
//   routing, and the hostile cases (destroying widgets inside handlers, event storms).
// Callers: CTest (label fast).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/core/events/Router.h"
#include "r1ui/core/events/TreeQueries.h"

using namespace core_test;
using namespace r1ui::core::events;
using r1ui::core::layout::LayoutInput;
using r1ui::core::layout::Position;
using r1ui::core::layout::layoutTree;
using r1ui::core::tree::kNoWidget;
using r1ui::core::tree::TreeLimits;

namespace {

const char* typeName(EventType t) {
  switch (t) {
    case EventType::PointerMove: return "Move";
    case EventType::PointerDown: return "Down";
    case EventType::PointerUp: return "Up";
    case EventType::PointerWheel: return "Wheel";
    case EventType::PointerEnter: return "Enter";
    case EventType::PointerLeave: return "Leave";
    case EventType::Click: return "Click";
    case EventType::DoubleClick: return "DoubleClick";
    case EventType::DragStart: return "DragStart";
    case EventType::CaptureLost: return "CaptureLost";
    case EventType::KeyDown: return "KeyDown";
    case EventType::KeyUp: return "KeyUp";
    case EventType::TextInput: return "Text";
    case EventType::FocusIn: return "FocusIn";
    case EventType::FocusOut: return "FocusOut";
  }
  return "?";
}

// Records "tag:Type:Phase" into a shared log and optionally runs a hook.
class Probe : public EventHandler {
 public:
  Probe(std::string tag, std::vector<std::string>& log, uint8_t mask = kListenTarget | kListenBubble)
      : tag_(std::move(tag)), log_(log), mask_(mask) {}
  uint8_t phases() const override { return mask_; }
  void onEvent(Event& e, Router& r) override {
    const char phase = e.phase == Phase::Capture ? 'C' : (e.phase == Phase::Target ? 'T' : 'B');
    log_.push_back(tag_ + ":" + typeName(e.type) + ":" + phase);
    if (hook) hook(e, r);
  }
  std::function<void(Event&, Router&)> hook;

 private:
  std::string tag_;
  std::vector<std::string>& log_;
  uint8_t mask_;
};

constexpr uint8_t kAllPhases = kListenCapture | kListenTarget | kListenBubble;

WidgetId absBox(WidgetTree& t, WidgetId parent, int x, int y, int w, int h) {
  Style s = sized(w, h);
  s.position = Position::Absolute;
  s.inset[r1ui::core::layout::kLeft] = Length::px(x);
  s.inset[r1ui::core::layout::kTop] = Length::px(y);
  return addChild(t, parent, s);
}

// root 400x300; A (0,0,200,200) holds B (50,50,100,100).
struct Scene {
  WidgetTree tree;
  WidgetId root, a, b;
  Scene() {
    root = addRoot(tree, sized(400, 300));
    a = absBox(tree, root, 0, 0, 200, 200);
    b = absBox(tree, a, 50, 50, 100, 100);
    relayout();
  }
  void relayout() { (void)layoutTree(tree, root, LayoutInput{400, 300}, nullptr); }
};

PointerInput at(double x, double y, uint64_t ts = 0, Button b = Button::None) {
  PointerInput p;
  p.x = x;
  p.y = y;
  p.timestampMs = ts;
  p.button = b;
  return p;
}

bool contains(const std::vector<std::string>& log, const std::string& entry) {
  for (const auto& e : log) {
    if (e == entry) return true;
  }
  return false;
}

std::string join(const std::vector<std::string>& log) {
  std::string s;
  for (const auto& e : log) s += e + " ";
  return s;
}

void expectLog(const std::vector<std::string>& got, const std::vector<std::string>& want, const char* what) {
  ++checks();
  if (got != want) {
    std::fprintf(stderr, "FAIL [%s]: %s\n  got:  %s\n  want: %s\n", currentCase(), what, join(got).c_str(),
                 join(want).c_str());
    ++failures();
  }
}

void hitTesting() {
  Scene s;
  expect(hitTest(s.tree, s.root, 60, 60) == s.b, "innermost widget wins");
  expect(hitTest(s.tree, s.root, 10, 10) == s.a, "parent area");
  expect(hitTest(s.tree, s.root, 300, 250) == s.root, "background is the root");
  expect(!hitTest(s.tree, s.root, 500, 500).valid(), "outside the root hits nothing");
  expect(!hitTest(s.tree, s.root, 400, 10).valid(), "right edge is exclusive");
  expect(hitTest(s.tree, s.root, 399, 299) == s.root, "last pixel is inside");

  s.tree.get(s.b)->flags.enabled = false;
  expect(hitTest(s.tree, s.root, 60, 60) == s.a, "disabled widgets are skipped");
  s.tree.get(s.b)->flags.enabled = true;
  s.tree.get(s.b)->flags.visible = false;
  expect(hitTest(s.tree, s.root, 60, 60) == s.a, "hidden widgets are skipped");
  s.tree.get(s.b)->flags.visible = true;
  s.tree.get(s.b)->flags.hitTestTransparent = true;
  expect(hitTest(s.tree, s.root, 60, 60) == s.a, "transparent widgets are skipped");
  const WidgetId c = absBox(s.tree, s.b, 10, 10, 20, 20);
  s.relayout();
  expect(hitTest(s.tree, s.root, 65, 65) == c, "children of a transparent widget stay hittable");
  s.tree.get(s.b)->flags.hitTestTransparent = false;
  s.tree.get(s.a)->flags.enabled = false;
  expect(hitTest(s.tree, s.root, 65, 65) == s.root, "a disabled parent removes its whole subtree");
  s.tree.get(s.a)->flags.enabled = true;

  // Clipping: D sticks out of A; outside A's clip it is not hittable, inside it is.
  const WidgetId d = absBox(s.tree, s.a, 150, 150, 100, 100);
  s.relayout();
  expect(hitTest(s.tree, s.root, 170, 170) == d, "inside the clip");
  expect(hitTest(s.tree, s.root, 210, 210) == d, "no clip: overflow is hittable");
  s.tree.get(s.a)->flags.clipsChildren = true;
  expect(hitTest(s.tree, s.root, 210, 210) == s.root, "clipped children are not hittable outside the clip");
  expect(hitTest(s.tree, s.root, 199, 199) == d, "still hittable inside the clip");
  s.tree.get(s.a)->flags.clipsChildren = false;
  s.tree.get(s.a)->style.overflow = r1ui::core::layout::Overflow::Hidden;
  expect(hitTest(s.tree, s.root, 210, 210) == s.root, "overflow:hidden clips hit testing too");
}

void overlappingSiblings() {
  WidgetTree t;
  const WidgetId root = addRoot(t, sized(100, 100));
  const WidgetId s1 = absBox(t, root, 0, 0, 60, 60);
  const WidgetId s2 = absBox(t, root, 30, 30, 60, 60);
  (void)layoutTree(t, root, LayoutInput{100, 100}, nullptr);
  expect(hitTest(t, root, 40, 40) == s2, "later sibling is on top");
  t.get(s1)->layer = 1;
  expect(hitTest(t, root, 40, 40) == s1, "a higher layer beats sibling order");
  t.get(s1)->layer = 0;
  t.get(s2)->layer = -1;
  expect(hitTest(t, root, 40, 40) == s1, "a lower layer is beneath");
  t.get(s2)->layer = 0;

  // In-flow vs absolute: an absolute sibling is above a later in-flow one.
  WidgetTree t2;
  const WidgetId r2 = addRoot(t2, sized(100, 100));
  const WidgetId over = absBox(t2, r2, 0, 0, 100, 100);
  const WidgetId flow = addChild(t2, r2, sized(100, 100));
  (void)layoutTree(t2, r2, LayoutInput{100, 100}, nullptr);
  expect(hitTest(t2, r2, 50, 50) == over, "absolute sibling stacks above an in-flow one");
  (void)flow;
}

void captureAndBubble() {
  Scene s;
  std::vector<std::string> log;
  Probe pr("R", log, kAllPhases), pa("A", log, kAllPhases), pb("B", log, kAllPhases);
  s.tree.get(s.root)->handler = &pr;
  s.tree.get(s.a)->handler = &pa;
  s.tree.get(s.b)->handler = &pb;
  Router router(s.tree, s.root);
  router.pointerMove(at(60, 60));
  log.clear();
  router.pointerDown(at(60, 60, 0, Button::Left));
  expectLog(log, {"R:Down:C", "A:Down:C", "B:Down:T", "A:Down:B", "R:Down:B"},
            "capture from the root, target, bubble back up");

  log.clear();
  pa.hook = [](Event& e, Router&) {
    if (e.phase == Phase::Capture) e.stopPropagation();
  };
  router.pointerUp(at(60, 60, 10, Button::Left));
  expect(!contains(log, "B:Up:T") && contains(log, "A:Up:C"), "stopPropagation in capture hides the target");
  pa.hook = nullptr;

  log.clear();
  pb.hook = [](Event& e, Router&) { e.stopPropagation(); };
  router.pointerWheel(at(60, 60));
  expectLog(log, {"R:Wheel:C", "A:Wheel:C", "B:Wheel:T"}, "stopPropagation at the target skips the bubble");
  pb.hook = [](Event& e, Router&) { e.markHandled(); };
  log.clear();
  const bool handled = router.pointerWheel(at(60, 60));
  expect(handled && contains(log, "R:Wheel:B"), "handled is reported and does not stop pointer bubbling");
  pb.hook = nullptr;

  // Capture-only listener sees events destined for descendants but not as target/bubble.
  Probe cap("cap", log, kListenCapture);
  s.tree.get(s.a)->handler = &cap;
  log.clear();
  router.pointerWheel(at(60, 60));
  expect(contains(log, "cap:Wheel:C") && !contains(log, "cap:Wheel:B"), "phase mask is honoured");
  s.tree.get(s.a)->handler = &pa;
}

void hoverTracking() {
  Scene s;
  std::vector<std::string> log;
  Probe pr("R", log), pa("A", log), pb("B", log);
  s.tree.get(s.root)->handler = &pr;
  s.tree.get(s.a)->handler = &pa;
  s.tree.get(s.b)->handler = &pb;
  Router router(s.tree, s.root);
  router.pointerMove(at(60, 60));
  expectLog(log, {"R:Enter:T", "A:Enter:T", "B:Enter:T", "B:Move:T", "A:Move:B", "R:Move:B"},
            "enter events run root first, before the move");
  expect(router.hovered() == s.b, "hovered widget");
  log.clear();
  router.pointerMove(at(10, 10));
  expectLog(log, {"B:Leave:T", "A:Move:T", "R:Move:B"}, "leave precedes the move");
  log.clear();
  router.pointerMove(at(300, 250));
  expectLog(log, {"A:Leave:T", "R:Move:T"}, "moving onto the background");
  log.clear();
  router.pointerLeftWindow();
  expectLog(log, {"R:Leave:T"}, "leaving the window clears hover");
  expect(!router.hovered().valid(), "nothing hovered after leaving");

  // Destroyed hovered widget: no event is delivered to it, the rest of the chain stays.
  router.pointerMove(at(60, 60));
  log.clear();
  expect(s.tree.destroy(s.b) == r1ui::core::tree::TreeError::None, "destroy hovered widget");
  router.pointerMove(at(60, 60));
  expectLog(log, {"A:Move:T", "R:Move:B"}, "no events for the destroyed hovered widget");
  expect(router.hovered() == s.a, "hover falls back to the parent");

  // Hidden hovered widget: sync() delivers the Leave.
  Scene h;
  std::vector<std::string> hlog;
  Probe hb("B", hlog);
  h.tree.get(h.b)->handler = &hb;
  Router hr(h.tree, h.root);
  hr.pointerMove(at(60, 60));
  hlog.clear();
  h.tree.get(h.b)->flags.visible = false;
  hr.sync();
  expectLog(hlog, {"B:Leave:T"}, "hiding the hovered widget produces a Leave");
  expect(hr.hovered() == h.a, "hover moved to what is now under the pointer");

  // Destroyed (not hidden) hovered widget: sync() has nobody to notify.
  Scene d;
  std::vector<std::string> dlog;
  Probe da("A", dlog);
  d.tree.get(d.a)->handler = &da;
  Router dr(d.tree, d.root);
  dr.pointerMove(at(60, 60));
  dlog.clear();
  (void)d.tree.destroy(d.b);
  dr.sync();
  expect(dlog.empty() && dr.hovered() == d.a, "sync after destroy is silent and re-hovers the parent");
}

void pointerCapture() {
  Scene s;
  std::vector<std::string> log;
  Probe pr("R", log), pa("A", log), pb("B", log);
  s.tree.get(s.root)->handler = &pr;
  s.tree.get(s.a)->handler = &pa;
  s.tree.get(s.b)->handler = &pb;
  Router router(s.tree, s.root);
  expect(!router.capturePointer(s.b), "capture needs a pressed button");
  expect(!router.capturePointer(WidgetId{77, 3}), "capture on a stale id is refused");

  pb.hook = [&](Event& e, Router& r) {
    if (e.type == EventType::PointerDown) expect(r.capturePointer(s.b), "capture inside the down handler");
  };
  router.pointerMove(at(60, 60));
  router.pointerDown(at(60, 60, 0, Button::Left));
  expect(router.capturer() == s.b, "capturer set");
  log.clear();
  router.pointerMove(at(350, 250));
  expectLog(log, {"B:Move:T", "A:Move:B", "R:Move:B", "B:DragStart:T", "A:DragStart:B", "R:DragStart:B"},
            "moves go to the capturer wherever the pointer is");
  expect(router.hovered() == s.b, "hover stays on the capturer");
  log.clear();
  router.pointerUp(at(350, 250, 10, Button::Left));
  expect(log.size() >= 2 && log[0] == "B:Up:T", "up is delivered to the capturer");
  expect(contains(log, "B:CaptureLost:T"), "release delivers CaptureLost");
  expect(!router.capturer().valid(), "capture ends when the last button is released");
  size_t leaveB = 99, leaveA = 99;
  for (size_t i = 0; i < log.size(); ++i) {
    if (log[i] == "B:Leave:T") leaveB = i;
    if (log[i] == "A:Leave:T") leaveA = i;
  }
  expect(leaveB < leaveA, "hover is re-evaluated after release, leaf first");

  // Capturer destroyed mid-drag: capture drops, routing falls back to hit testing, no crash.
  pb.hook = nullptr;
  router.pointerMove(at(60, 60));
  router.pointerDown(at(60, 60, 20, Button::Left));
  expect(router.capturePointer(s.b), "explicit capture");
  (void)s.tree.destroy(s.b);
  log.clear();
  router.pointerMove(at(60, 60));
  expect(!router.capturer().valid() && contains(log, "A:Move:T"), "destroyed capturer is dropped, events re-route");
  router.pointerUp(at(60, 60, 30, Button::Left));
  expect(router.heldButtons() == 0, "buttons released");

  // Nested capture: a second capturer replaces the first, which hears about it.
  Scene n;
  std::vector<std::string> nlog;
  Probe na("A", nlog), nb("B", nlog);
  n.tree.get(n.a)->handler = &na;
  n.tree.get(n.b)->handler = &nb;
  Router nr(n.tree, n.root);
  nr.pointerMove(at(60, 60));
  nr.pointerDown(at(60, 60, 0, Button::Left));
  expect(nr.capturePointer(n.a), "outer capture");
  nlog.clear();
  expect(nr.capturePointer(n.b) && nr.capturer() == n.b, "inner capture replaces it");
  expectLog(nlog, {"A:CaptureLost:T"}, "replaced capturer is told");
  nlog.clear();
  nr.releaseCapture();
  expect(!nlog.empty() && nlog[0] == "B:CaptureLost:T", "explicit release delivers CaptureLost");
  nr.pointerUp(at(60, 60, 5, Button::Left));

  // Hidden capturer is released.
  Scene hc;
  std::vector<std::string> hlog;
  Probe hb("B", hlog);
  hc.tree.get(hc.b)->handler = &hb;
  Router hr(hc.tree, hc.root);
  hr.pointerMove(at(60, 60));
  hr.pointerDown(at(60, 60, 0, Button::Left));
  (void)hr.capturePointer(hc.b);
  hc.tree.get(hc.b)->flags.visible = false;
  hlog.clear();
  hr.pointerMove(at(70, 70));
  expect(contains(hlog, "B:CaptureLost:T") && !hr.capturer().valid(), "hiding the capturer releases capture");
}

void clicksAndDrags() {
  Scene s;
  std::vector<std::string> log;
  Probe pb("B", log), pa("A", log);
  std::vector<uint32_t> clicks;
  pb.hook = [&](Event& e, Router&) {
    if (e.type == EventType::Click) clicks.push_back(e.clickCount);
  };
  s.tree.get(s.b)->handler = &pb;
  s.tree.get(s.a)->handler = &pa;
  Router router(s.tree, s.root);
  auto click = [&](double x, double y, uint64_t t) {
    router.pointerDown(at(x, y, t, Button::Left));
    router.pointerUp(at(x, y, t + 20, Button::Left));
  };
  click(60, 60, 100);
  expect(contains(log, "B:Click:T") && clicks.size() == 1 && clicks[0] == 1, "single click");
  expect(!contains(log, "B:DoubleClick:T"), "no double click yet");
  click(61, 60, 300);
  expect(clicks.size() == 2 && clicks[1] == 2 && contains(log, "B:DoubleClick:T"), "second click within 500 ms");
  click(61, 60, 400);
  expect(clicks.size() == 3 && clicks[2] == 3, "third click counts on");
  size_t doubles = 0;
  for (const auto& e : log) doubles += e == "B:DoubleClick:T" ? 1u : 0u;
  expect(doubles == 1, "double click fires only for the second click");
  click(60, 60, 2000);
  expect(clicks.back() == 1, "click after the time threshold restarts the count");
  click(60, 60, 2100);
  click(70, 60, 2200);
  expect(clicks.back() == 1, "click beyond the distance threshold restarts the count");
  click(60, 60, 3000);
  click(64, 60, 3100);
  expect(clicks.back() == 2, "exactly 4 px apart still counts (inclusive)");
  click(60, 60, 4000);
  click(60, 60, 3500);  // clock went backwards
  expect(clicks.back() == 1, "a backwards timestamp never forms a double click");

  RouterConfig cfg;
  cfg.doubleClickMs = 100;
  cfg.doubleClickDistance = 20;
  router.setConfig(cfg);
  click(60, 60, 5000);
  click(75, 60, 5050);
  expect(clicks.back() == 2, "tunable distance");
  click(60, 60, 6000);
  click(60, 60, 6200);
  expect(clicks.back() == 1, "tunable time");

  // Release over a different widget: the click goes to the common ancestor.
  router.setConfig(RouterConfig{});
  log.clear();
  router.pointerDown(at(60, 60, 7000, Button::Left));
  router.pointerUp(at(10, 10, 7010, Button::Left));
  expect(contains(log, "A:Click:T") && !contains(log, "B:Click:T"), "click lands on the common ancestor");
  log.clear();
  router.pointerDown(at(60, 60, 7100, Button::Left));
  router.pointerUp(at(500, 500, 7110, Button::Left));
  expect(!contains(log, "A:Click:T") && !contains(log, "B:Click:T"), "release outside every widget is no click");

  // Drag threshold: strictly greater than 5 px.
  log.clear();
  router.pointerDown(at(60, 60, 8000, Button::Left));
  router.pointerMove(at(65, 60, 8001));
  expect(!contains(log, "B:DragStart:T"), "exactly the threshold is not a drag");
  router.pointerMove(at(63, 64, 8002));
  expect(!contains(log, "B:DragStart:T"), "3-4-5 triangle is exactly 5 px: not a drag");
  router.pointerMove(at(66, 60, 8003));
  expect(contains(log, "B:DragStart:T") && contains(log, "A:DragStart:B"), "beyond the threshold starts a drag, bubbling");
  size_t starts = 0;
  for (const auto& e : log) starts += e == "B:DragStart:T" ? 1u : 0u;
  router.pointerMove(at(90, 90, 8004));
  size_t starts2 = 0;
  for (const auto& e : log) starts2 += e == "B:DragStart:T" ? 1u : 0u;
  expect(starts == 1 && starts2 == 1, "DragStart fires once per press");
  log.clear();
  router.pointerUp(at(66, 60, 8010, Button::Left));
  expect(contains(log, "B:Click:T"), "an unaccepted drag does not cancel the click");

  // Accepted drag suppresses the click.
  pb.hook = [](Event& e, Router&) {
    if (e.type == EventType::DragStart) e.markHandled();
  };
  log.clear();
  router.pointerDown(at(60, 60, 9000, Button::Left));
  router.pointerMove(at(80, 60, 9001));
  router.pointerUp(at(80, 60, 9010, Button::Left));
  expect(contains(log, "B:DragStart:T") && !contains(log, "B:Click:T") && !contains(log, "A:Click:T"),
         "accepted drag suppresses Click");

  // Configurable threshold.
  RouterConfig big;
  big.dragThreshold = 20;
  router.setConfig(big);
  log.clear();
  router.pointerDown(at(60, 60, 9100, Button::Left));
  router.pointerMove(at(75, 60, 9101));
  expect(!contains(log, "B:DragStart:T"), "tunable threshold");
  router.pointerUp(at(75, 60, 9102, Button::Left));
  router.setConfig(RouterConfig{});

  // Second button while one is held: no click for it.
  log.clear();
  pb.hook = nullptr;
  router.pointerDown(at(60, 60, 9500, Button::Left));
  router.pointerDown(at(60, 60, 9501, Button::Right));
  router.pointerUp(at(60, 60, 9502, Button::Right));
  expect(!contains(log, "B:Click:T"), "a chorded button never clicks");
  router.pointerUp(at(60, 60, 9503, Button::Left));
  expect(contains(log, "B:Click:T"), "the first button still clicks");
  expect(!router.pointerUp(at(60, 60, 9600, Button::Left)), "stray release is ignored");
}

class FocusLog : public FocusObserver {
 public:
  struct Change {
    WidgetId previous;
    WidgetId next;
    FocusReason reason;
  };
  std::vector<Change> changes;
  void onFocusChanged(WidgetId previous, WidgetId next, FocusReason reason, Router&) override {
    changes.push_back({previous, next, reason});
  }
};

struct FocusScene {
  WidgetTree tree;
  WidgetId root;
  WidgetId f[5];
  FocusScene() {
    root = addRoot(tree, sized(400, 100));
    for (int i = 0; i < 5; ++i) {
      f[i] = absBox(tree, root, i * 50, 0, 40, 40);
      tree.get(f[i])->flags.focusable = true;
    }
    (void)layoutTree(tree, root, LayoutInput{400, 100}, nullptr);
  }
};

void focusAndTab() {
  FocusScene s;
  Router router(s.tree, s.root);
  FocusLog observer;
  router.setFocusObserver(&observer);
  expect(router.focusNext() && router.focused() == s.f[0], "first Tab focuses the first widget");
  expect(router.focusVisible(), "keyboard focus shows the indication");
  for (int i = 1; i < 5; ++i) expect(router.focusNext() && router.focused() == s.f[i], "Tab walks document order");
  expect(router.focusNext() && router.focused() == s.f[0], "Tab wraps around");
  expect(router.focusNext(true) && router.focused() == s.f[4], "Shift+Tab wraps backwards");
  expect(router.focusNext(true) && router.focused() == s.f[3], "Shift+Tab walks backwards");
  expect(!observer.changes.empty() && observer.changes.back().reason == FocusReason::Keyboard, "observer sees the reason");

  // Tab through key events.
  router.clearFocus();
  expect(router.keyDown(Key::Tab, Mod::kNone, false) && router.focused() == s.f[0], "Tab key moves focus");
  expect(router.keyDown(Key::Tab, Mod::kShift, false) && router.focused() == s.f[4], "Shift+Tab key moves back");
  expect(!router.keyDown(Key::Tab, Mod::kCtrl, false) && router.focused() == s.f[4], "Ctrl+Tab is not navigation");

  // tabIndex: positive first (ascending), then zero in document order, negative skipped.
  s.tree.get(s.f[2])->tabIndex = 1;
  s.tree.get(s.f[0])->tabIndex = 2;
  s.tree.get(s.f[1])->tabIndex = -1;
  router.clearFocus();
  std::vector<WidgetId> order;
  for (int i = 0; i < 4; ++i) {
    router.focusNext();
    order.push_back(router.focused());
  }
  expect(order[0] == s.f[2] && order[1] == s.f[0] && order[2] == s.f[3] && order[3] == s.f[4],
         "order is tabIndex 1, 2, then 0 in document order; -1 is skipped");
  expect(router.focus(s.f[1]) && router.focused() == s.f[1], "negative tabIndex is focusable programmatically");
  expect(router.focusNext() && router.focused() == s.f[3], "from a skipped widget Tab continues by document position");
  s.tree.get(s.f[2])->tabIndex = 0;
  s.tree.get(s.f[0])->tabIndex = 0;
  s.tree.get(s.f[1])->tabIndex = 0;

  // Disabled and hidden widgets are skipped.
  router.focus(s.f[0]);
  s.tree.get(s.f[1])->flags.enabled = false;
  s.tree.get(s.f[2])->flags.visible = false;
  expect(router.focusNext() && router.focused() == s.f[3], "disabled and hidden widgets are skipped");
  expect(!router.focus(s.f[1]) && !router.focus(s.f[2]), "focus() refuses disabled / hidden widgets");
  expect(router.focused() == s.f[3], "refused focus leaves focus unchanged");

  // All disabled: Tab does nothing, existing focus is cleared by validation.
  for (int i = 0; i < 5; ++i) s.tree.get(s.f[i])->flags.enabled = false;
  expect(!router.focusNext() && !router.keyDown(Key::Tab, Mod::kNone, false), "Tab with everything disabled");
  expect(!router.focused().valid(), "focus cleared when its widget got disabled");
  for (int i = 0; i < 5; ++i) s.tree.get(s.f[i])->flags.enabled = true;
  s.tree.get(s.f[2])->flags.visible = true;
  s.tree.get(s.f[1])->flags.enabled = true;

  // A disabled container removes its focusable children.
  const WidgetId panel = absBox(s.tree, s.root, 0, 50, 100, 40);
  const WidgetId inner = absBox(s.tree, panel, 0, 0, 10, 10);
  s.tree.get(inner)->flags.focusable = true;
  (void)layoutTree(s.tree, s.root, LayoutInput{400, 100}, nullptr);
  s.tree.get(panel)->flags.enabled = false;
  expect(!router.focus(inner), "children of a disabled container are not focusable");
}

void focusLifecycle() {
  FocusScene s;
  std::vector<std::string> log;
  Probe p0("f0", log), p1("f1", log), p2("f2", log);
  s.tree.get(s.f[0])->handler = &p0;
  s.tree.get(s.f[1])->handler = &p1;
  s.tree.get(s.f[2])->handler = &p2;
  Router router(s.tree, s.root);
  FocusLog observer;
  router.setFocusObserver(&observer);
  router.pointerMove(at(10, 10));
  router.pointerDown(at(10, 10, 0, Button::Left));
  expect(router.focused() == s.f[0] && !router.focusVisible(), "pressing a focusable focuses it without indication");
  expect(observer.changes.back().reason == FocusReason::Pointer, "pointer reason");
  router.pointerUp(at(10, 10, 5, Button::Left));
  log.clear();
  router.focus(s.f[1]);
  expectLog(log, {"f0:FocusOut:T", "f1:FocusIn:T"}, "FocusOut before FocusIn");

  // Pressing a non-focusable area keeps the focus (spec 01 rule 8).
  const WidgetId blank = absBox(s.tree, s.root, 0, 60, 400, 40);
  (void)layoutTree(s.tree, s.root, LayoutInput{400, 100}, nullptr);
  router.pointerDown(at(100, 80, 10, Button::Left));
  router.pointerUp(at(100, 80, 11, Button::Left));
  expect(router.focused() == s.f[1], "clicking a non-focusable area keeps focus");
  (void)blank;

  // A handler that handles the press, or asks for focus itself, wins over rule 2.
  p2.hook = [](Event& e, Router&) {
    if (e.type == EventType::PointerDown) e.markHandled();
  };
  router.pointerDown(at(110, 10, 20, Button::Left));
  router.pointerUp(at(110, 10, 21, Button::Left));
  expect(router.focused() == s.f[1], "a handled press does not move focus");
  p2.hook = [&](Event& e, Router& r) {
    if (e.type == EventType::PointerDown) r.focus(s.f[3]);
  };
  router.pointerDown(at(110, 10, 30, Button::Left));
  router.pointerUp(at(110, 10, 31, Button::Left));
  expect(router.focused() == s.f[3], "an explicit focus request in the handler wins");
  p2.hook = nullptr;

  // FocusOut handler redirecting focus: its choice stands (spec 01 rule 7).
  router.focus(s.f[0]);
  p0.hook = [&](Event& e, Router& r) {
    if (e.type == EventType::FocusOut) r.focus(s.f[3]);
  };
  const bool moved = router.focus(s.f[2]);
  expect(!moved && router.focused() == s.f[3], "the losing widget's own choice stands");
  p0.hook = nullptr;

  // Destroying the focused widget clears focus; the observer is told with an invalid next.
  router.focus(s.f[4]);
  observer.changes.clear();
  (void)s.tree.destroy(s.f[4]);
  expect(!router.focused().valid(), "focused() never returns a dead widget");
  router.sync();
  expect(observer.changes.size() == 1 && observer.changes[0].previous == s.f[4] && !observer.changes[0].next.valid(),
         "observer is notified of the cleared focus");
  expect(!router.focus(s.f[4]), "cannot focus a destroyed widget");

  // Hiding the focused widget delivers FocusOut.
  router.focus(s.f[1]);
  log.clear();
  s.tree.get(s.f[1])->flags.visible = false;
  router.sync();
  expectLog(log, {"f1:FocusOut:T"}, "hidden focus owner receives FocusOut");
  expect(!router.focused().valid(), "and focus is cleared");

  // Focus restoration hook.
  s.tree.get(s.f[1])->flags.visible = true;
  router.focus(s.f[2]);
  const WidgetId saved = router.saveFocus();
  router.clearFocus();
  expect(router.restoreFocus(saved) && router.focused() == s.f[2], "restore refocuses a surviving widget");
  router.clearFocus();
  (void)s.tree.destroy(s.f[2]);
  expect(!router.restoreFocus(saved), "restore does nothing for a destroyed widget");
  expect(!router.restoreFocus(kNoWidget), "restore of nothing is a no-op");
}

class Shortcuts : public GlobalKeyHandler {
 public:
  std::vector<Key> seen;
  bool use = false;
  bool onGlobalKey(const Event& e, Router&) override {
    seen.push_back(e.key);
    return use;
  }
};

void keyboardRouting() {
  Scene s;
  s.tree.get(s.b)->flags.focusable = true;
  std::vector<std::string> log;
  Probe pr("R", log), pa("A", log), pb("B", log);
  s.tree.get(s.root)->handler = &pr;
  s.tree.get(s.a)->handler = &pa;
  s.tree.get(s.b)->handler = &pb;
  Router router(s.tree, s.root);
  Shortcuts global;
  router.setGlobalKeyHandler(&global);

  // No focus: straight to the global handler.
  expect(!router.keyDown(Key::A, Mod::kCtrl, false) && global.seen.size() == 1 && log.empty(),
         "without focus keys go to the global fallback only");
  global.use = true;
  expect(router.keyDown(Key::Escape, Mod::kNone, false), "global handler's result is returned");
  global.use = false;
  global.seen.clear();

  router.focus(s.b);
  log.clear();
  expect(!router.keyDown(Key::A, Mod::kNone, false), "unused key");
  expectLog(log, {"B:KeyDown:T", "A:KeyDown:B", "R:KeyDown:B", }, "focused widget, then ancestors");
  expect(global.seen.size() == 1, "then the global fallback");

  global.seen.clear();
  log.clear();
  pb.hook = [](Event& e, Router&) { e.markHandled(); };
  expect(router.keyDown(Key::A, Mod::kNone, false), "handled key");
  expectLog(log, {"B:KeyDown:T"}, "the first user wins; ancestors and global are not offered the key");
  expect(global.seen.empty(), "global untouched");

  pb.hook = [](Event& e, Router&) { e.stopPropagation(); };
  log.clear();
  expect(!router.keyDown(Key::A, Mod::kNone, false) && global.seen.empty() && log.size() == 1,
         "stopPropagation also blocks the global fallback");
  pb.hook = nullptr;

  // A handler that disables an ancestor mid-dispatch: that ancestor is skipped, the walk goes on.
  pb.hook = [&](Event&, Router&) { s.tree.get(s.a)->flags.enabled = false; };
  log.clear();
  router.keyDown(Key::A, Mod::kNone, false);
  expectLog(log, {"B:KeyDown:T", "R:KeyDown:B"}, "disabled ancestors do not see keys");
  pb.hook = nullptr;
  s.tree.get(s.a)->flags.enabled = true;

  router.focus(s.b);
  log.clear();
  global.seen.clear();
  router.keyUp(Key::A, Mod::kNone);
  expectLog(log, {"B:KeyUp:T", "A:KeyUp:B", "R:KeyUp:B"}, "key up follows the focus chain");
  expect(global.seen.empty(), "global shortcuts never see key releases");

  log.clear();
  expect(!router.textInput(U'x') && contains(log, "B:Text:T"), "text goes to the focused chain");
  expect(global.seen.empty(), "text never reaches shortcuts");
  log.clear();
  expect(!router.textInput(0) && !router.textInput(0xD800) && !router.textInput(0xDFFF) &&
             !router.textInput(0x110000) && log.empty(),
         "invalid code points are dropped");
  pb.hook = [](Event& e, Router&) {
    if (e.type == EventType::TextInput) e.markHandled();
  };
  expect(router.textInput(U'\U0001F600'), "supplementary code points are accepted");
  pb.hook = nullptr;

  router.clearFocus();
  log.clear();
  expect(!router.keyUp(Key::A, Mod::kNone) && !router.textInput(U'x') && log.empty(), "no focus: key up and text go nowhere");
}

void hostileHandlers() {
  // A handler destroys an ancestor in the middle of a bubbling pass.
  {
    Scene s;
    std::vector<std::string> log;
    Probe pr("R", log), pa("A", log), pb("B", log);
    s.tree.get(s.root)->handler = &pr;
    s.tree.get(s.a)->handler = &pa;
    s.tree.get(s.b)->handler = &pb;
    Router router(s.tree, s.root);
    router.pointerMove(at(60, 60));
    pb.hook = [&](Event& e, Router&) {
      if (e.type == EventType::PointerDown) (void)s.tree.destroy(s.a);
    };
    log.clear();
    router.pointerDown(at(60, 60, 0, Button::Left));
    expect(!s.tree.alive(s.b), "the destroyed subtree is gone");
    expect(!contains(log, "A:Down:B") && contains(log, "R:Down:B"), "bubbling skips the dead ancestor, continues past it");
    pb.hook = nullptr;
    router.pointerUp(at(60, 60, 10, Button::Left));
    expect(router.heldButtons() == 0, "button state stays consistent after the destroy");
  }
  // A handler destroys itself during Enter, and the root during a move.
  {
    Scene s;
    std::vector<std::string> log;
    Probe pb("B", log), pr("R", log);
    s.tree.get(s.b)->handler = &pb;
    s.tree.get(s.root)->handler = &pr;
    pb.hook = [&](Event& e, Router&) {
      if (e.type == EventType::PointerEnter) (void)s.tree.destroy(s.b);
    };
    Router router(s.tree, s.root);
    router.pointerMove(at(60, 60));
    expect(!s.tree.alive(s.b), "widget destroyed itself in its Enter handler");
    router.pointerMove(at(61, 60));
    pr.hook = [&](Event& e, Router&) {
      if (e.type == EventType::PointerMove) (void)s.tree.destroy(s.root);
    };
    router.pointerMove(at(62, 60));
    expect(!s.tree.alive(s.root), "root destroyed inside a handler");
    expect(!router.pointerMove(at(63, 60)) && !router.pointerDown(at(63, 60, 0, Button::Left)) &&
               !router.keyDown(Key::A, Mod::kNone, false),
           "a router without a live root is inert");
    router.pointerUp(at(63, 60, 1, Button::Left));
    router.pointerLeftWindow();
    router.sync();
  }
  // Unconditional re-entrancy is bounded.
  {
    Scene s;
    std::vector<std::string> log;
    Probe pb("B", log);
    int calls = 0;
    pb.hook = [&](Event& e, Router& r) {
      if (e.type == EventType::PointerMove) {
        ++calls;
        r.pointerMove(at(60, 60));
      }
    };
    s.tree.get(s.b)->handler = &pb;
    Router router(s.tree, s.root);
    router.pointerMove(at(60, 60));
    expect(calls > 1 && calls <= kMaxDispatchDepth, "recursive dispatch stops at the depth limit");
  }
  // Bad input.
  {
    Scene s;
    std::vector<std::string> log;
    Probe pb("B", log);
    s.tree.get(s.b)->handler = &pb;
    Router router(s.tree, s.root);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    expect(!router.pointerMove(at(nan, 1)) && !router.pointerDown(at(1, inf, 0, Button::Left)) &&
               !router.pointerDown(at(60, 60)) && !router.pointerUp(at(60, 60, 0, Button::Left)),
           "non-finite coordinates, buttonless down and stray up are dropped");
    PointerInput w = at(60, 60);
    w.wheelY = nan;
    expect(!router.pointerWheel(w) && log.empty(), "non-finite wheel delta dropped");
    RouterConfig bad;
    bad.dragThreshold = nan;
    bad.doubleClickDistance = -3;
    router.setConfig(bad);
    expect(router.config().dragThreshold == 5.0 && router.config().doubleClickDistance == 4.0,
           "bad config values fall back to the defaults");
  }
  // Very deep path (500 levels) is delivered without overflowing anything.
  {
    WidgetTree t(TreeLimits{512, 2000});
    const WidgetId root = addRoot(t, sized(100, 100));
    WidgetId cur = root;
    for (int i = 0; i < 500; ++i) cur = absBox(t, cur, 0, 0, 100, 100);
    (void)layoutTree(t, root, LayoutInput{100, 100}, nullptr);
    std::vector<std::string> log;
    Probe leaf("leaf", log, kAllPhases);
    t.get(cur)->handler = &leaf;
    Router router(t, root);
    router.pointerMove(at(10, 10));
    expect(router.hovered() == cur, "deep leaf is hit");
    log.clear();
    router.pointerWheel(at(10, 10));
    expect(log.size() == 1 && log[0] == "leaf:Wheel:T", "deep path delivery");
  }
}

void eventStorm() {
  Scene s;
  int moves = 0;
  struct Counter : EventHandler {
    int* count;
    explicit Counter(int* c) : count(c) {}
    void onEvent(Event& e, Router&) override {
      if (e.type == EventType::PointerMove) ++*count;
    }
  } counter(&moves);
  s.tree.get(s.root)->handler = &counter;
  Router router(s.tree, s.root);
  const int total = 300000;
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < total; ++i) {
    const int phase = i % 3;
    const double x = phase == 0 ? 60 : (phase == 1 ? 10 : 300);
    const double y = phase == 0 ? 60 : (phase == 1 ? 10 : 250);
    router.pointerMove(at(x, y, static_cast<uint64_t>(i)));
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::fprintf(stderr, "PERF events: %d pointer moves with hover churn: %.1f ms (%.3f us/event)\n", total, ms,
               ms * 1000.0 / total);
  expect(moves == total, "every move reached the root's bubble handler");
  expect(router.hovered() == s.root || router.hovered() == s.a || router.hovered() == s.b, "hover consistent");
}

}  // namespace

int main() {
  runCase("hit_testing", hitTesting);
  runCase("overlapping_siblings", overlappingSiblings);
  runCase("capture_and_bubble", captureAndBubble);
  runCase("hover_tracking", hoverTracking);
  runCase("pointer_capture", pointerCapture);
  runCase("clicks_and_drags", clicksAndDrags);
  runCase("focus_and_tab", focusAndTab);
  runCase("focus_lifecycle", focusLifecycle);
  runCase("keyboard_routing", keyboardRouting);
  runCase("hostile_handlers", hostileHandlers);
  runCase("event_storm", eventStorm);
  return finish("ui-core.events");
}
