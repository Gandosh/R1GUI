// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the conformance suite for IFloatingBackend implementations: BackendRig, the small adapter a
//   backend's test provides (how to create it, settle its windows and simulate the user), and
//   runBackendConformance, which checks every clause of the contract in FloatingBackend.h and
//   docs/dev/docking.md: windows and ids, content areas, rectangle limits, stacking, titles,
//   maximize, hiding, coordinates, drop target queries, the echo rule, user gestures reported to the
//   listener, destruction, limits and optional pointer tracking.
// Why: the native OS-window backend is written later by someone else; it must pass the same suite
//   the in-window backend passes, so it is a reusable header and not a test of one implementation.
// Callers: tests/ui-widgets/dock/in_window_backend_test.cpp (and the native backend's test later).
// Usage: implement BackendRig for the backend and call runBackendConformance("name", factory); the
//   expectations count failures through r1test (TestSupport.h). Operations a backend cannot simulate
//   return false from the rig and the matching clauses are skipped (and reported on stdout).
#pragma once

#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "ExpectWithMessage.h"
#include "r1ui/widgets/dock/FloatingBackend.h"

namespace backend_conformance {

using namespace r1ui::widgets;
namespace dock = r1ui::dock;

// What a backend's test supplies.
class BackendRig {
 public:
  virtual ~BackendRig() = default;
  virtual IFloatingBackend& backend() = 0;
  // Lays out every context the backend uses so rectangles are final.
  virtual void settle() = 0;
  // A screen point inside the main content area.
  virtual dock::Point pointInMain() = 0;
  // Simulated user actions; false = this rig cannot do it.
  virtual bool userMoves(FloatId, const dock::Rect&) { return false; }
  virtual bool userCloses(FloatId) { return false; }
  virtual bool userRaises(FloatId) { return false; }
  virtual bool osDestroys(FloatId) { return false; }
  virtual bool pointerSample(dock::Point, bool) { return false; }
};

using RigFactory = std::function<std::unique_ptr<BackendRig>()>;

// Records everything the backend tells its listener.
struct RecordingListener final : IFloatingListener {
  void onFloatMoved(FloatId w, const dock::Rect& r) override { moved.push_back({w, r}); }
  void onFloatCloseRequested(FloatId w) override { closeRequests.push_back(w); }
  void onFloatActivated(FloatId w) override { activated.push_back(w); }
  void onFloatLost(FloatId w) override { lost.push_back(w); }
  void onFloatScaleChanged(FloatId w, double s) override { scales.push_back({w, s}); }
  void onFloatMaximizedChanged(FloatId w, bool m) override { maximizedChanges.push_back({w, m}); }
  size_t total() const { return moved.size() + closeRequests.size() + activated.size() + lost.size() + scales.size() + maximizedChanges.size(); }
  std::vector<std::pair<FloatId, dock::Rect>> moved;
  std::vector<FloatId> closeRequests, activated, lost;
  std::vector<std::pair<FloatId, double>> scales;
  std::vector<std::pair<FloatId, bool>> maximizedChanges;
};

struct RecordingSink final : IPointerSink {
  void onTrackedPointer(dock::Point p, bool down) override { samples.push_back({p, down}); }
  std::vector<std::pair<dock::Point, bool>> samples;
};

inline bool finiteRect(const dock::Rect& r) { return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h); }

inline FloatRequest request(dock::Rect rect, dock::Point minSize = {64.0, 64.0}) {
  FloatRequest r;
  r.panels = {1};
  r.active = 1;
  r.title = "Window";
  r.contentRect = rect;
  r.minContentSize = minSize;
  return r;
}

inline void runBackendConformance(const char* name, const RigFactory& factory) {
  std::printf("backend conformance: %s\n", name);
  {  // ---- description, main window --------------------------------------------------------
    std::unique_ptr<BackendRig> rig = factory();
    IFloatingBackend& b = rig->backend();
    rig->settle();
    const BackendInfo info = b.describe();
    R1_EXPECT(!info.name.empty());
    R1_EXPECT(info.frame.left >= 0 && info.frame.top >= 0 && info.frame.right >= 0 && info.frame.bottom >= 0 && std::isfinite(info.frame.top));
    const dock::Rect main = b.mainContentRect();
    R1_EXPECT(finiteRect(main) && main.w > 0 && main.h > 0);
    R1_EXPECT(main.contains(rig->pointInMain()));
    const std::optional<FloatContent> mainContent = b.content(kMainWindow);
    R1_EXPECT(mainContent && mainContent->valid() && mainContent->scale > 0.0);
    R1_EXPECT(b.stacking().empty());
    R1_EXPECT(!b.destroyWindow(kMainWindow) && !b.setContentRect(kMainWindow, {0, 0, 100, 100}) && !b.bringToFront(kMainWindow));
    R1_EXPECT(!b.content(12345) && !b.contentRect(12345) && !b.setTitle(12345, "x") && !b.setMaximized(12345, true) && !b.setVisible(12345, false));
    R1_EXPECT(!b.isMaximized(12345) && !b.destroyWindow(12345) && !b.bringToFront(12345) && !b.setContentRect(12345, {0, 0, 100, 100}));
    R1_EXPECT(b.topmostWindowAt(rig->pointInMain(), {}) == std::optional<FloatId>(kMainWindow), "the main window shows the point");
    R1_EXPECT(!b.topmostWindowAt({-1.0e7, -1.0e7}, {}).has_value(), "no window shows a far away point");
    R1_EXPECT(!b.topmostWindowAt({std::nan(""), std::nan("")}, {}).has_value(), "a NaN point is shown by no window");
  }
  {  // ---- creating windows, ids, content areas -------------------------------------------------
    std::unique_ptr<BackendRig> rig = factory();
    IFloatingBackend& b = rig->backend();
    RecordingListener listener;
    b.setListener(&listener);
    rig->settle();
    const dock::Point p = rig->pointInMain();
    const FloatCreateResult a = b.createWindow(request({p.x - 40, p.y - 20, 300, 200}));
    const FloatCreateResult c = b.createWindow(request({p.x + 20, p.y + 10, 240, 180}));
    R1_EXPECT(a.ok && c.ok && a.id != 0 && c.id != 0 && a.id != c.id, "ids are non-zero and unique");
    rig->settle();
    R1_EXPECT(b.stacking() == std::vector<FloatId>({a.id, c.id}), "stacking lists windows bottom to top in creation order");
    const std::optional<FloatContent> content = b.content(a.id);
    R1_EXPECT(content && content->valid() && content->scale > 0.0, "a window offers a content area");
    const std::optional<FloatContent> other = b.content(c.id);
    R1_EXPECT(other && other->valid() && !(content->ui == other->ui && content->parent == other->parent), "each window has its own content widget");
    const std::optional<dock::Rect> rect = b.contentRect(a.id);
    R1_EXPECT(rect && finiteRect(*rect) && std::abs(rect->w - 300) < 1.5 && std::abs(rect->h - 200) < 1.5, "the content rectangle is what was asked for when it is allowed");
    R1_EXPECT(listener.total() == 0, "creating windows is not reported back (echo rule)");

    // Limits: a too small rectangle is raised to the minimum, an infinite one is refused cleanly.
    const FloatCreateResult tiny = b.createWindow(request({p.x, p.y, 3, 4}, {120, 90}));
    R1_EXPECT(tiny.ok);
    if (tiny.ok) {
      const dock::Rect r = *b.contentRect(tiny.id);
      R1_EXPECT(r.w >= 120 - 0.5 && r.h >= 90 - 0.5, "the minimum content size is enforced");
    }
    const size_t before = b.stacking().size();
    const FloatCreateResult nan = b.createWindow(request({std::nan(""), 0, 100, 100}));
    R1_EXPECT(!nan.ok && !nan.error.empty() && b.stacking().size() == before, "a non-finite rectangle is refused with a reason");
    const FloatCreateResult inf = b.createWindow(request({0, 0, std::numeric_limits<double>::infinity(), 100}));
    R1_EXPECT(!inf.ok && b.stacking().size() == before);
    const FloatCreateResult huge = b.createWindow(request({0, 0, 1.0e12, 1.0e12}));
    if (huge.ok) {
      const dock::Rect r = *b.contentRect(huge.id);
      R1_EXPECT(finiteRect(r) && r.w < 1.0e10 && r.h < 1.0e10, "an absurd size is limited");
      b.destroyWindow(huge.id);
    }
    rig->settle();

    // ---- setContentRect, titles, stacking, the echo rule ---------------------------------------
    R1_EXPECT(b.setContentRect(a.id, {p.x, p.y, 280, 190}));
    const dock::Rect moved = *b.contentRect(a.id);
    R1_EXPECT(std::abs(moved.w - 280) < 1.5 && std::abs(moved.h - 190) < 1.5, "setContentRect resizes");
    R1_EXPECT(!b.setContentRect(a.id, {std::nan(""), 0, 100, 100}) || finiteRect(*b.contentRect(a.id)), "NaN never ends in the stored rectangle");
    R1_EXPECT(b.setTitle(a.id, "A title") && b.setTitle(a.id, std::string(5000, 'x')) && b.setTitle(a.id, ""), "titles of any length are accepted");
    R1_EXPECT(b.bringToFront(a.id) && b.stacking().back() == a.id, "bringToFront puts the window last");
    R1_EXPECT(b.bringToFront(a.id), "raising the top window is a harmless no-op");
    R1_EXPECT(listener.total() == 0, "calls made by the host are never echoed to the listener");

    // ---- maximize --------------------------------------------------------------------------
    if (b.describe().maximize) {
      const dock::Rect restore = *b.contentRect(c.id);
      R1_EXPECT(b.setMaximized(c.id, true) && b.isMaximized(c.id));
      const dock::Rect big = *b.contentRect(c.id);
      R1_EXPECT(big.w >= restore.w && big.h >= restore.h, "maximized is not smaller");
      R1_EXPECT(b.setMaximized(c.id, true) && b.isMaximized(c.id), "idempotent");
      R1_EXPECT(b.setMaximized(c.id, false) && !b.isMaximized(c.id));
      const dock::Rect back = *b.contentRect(c.id);
      R1_EXPECT(std::abs(back.x - restore.x) < 1.5 && std::abs(back.w - restore.w) < 1.5 && std::abs(back.h - restore.h) < 1.5, "restoring returns to the previous rectangle");
      R1_EXPECT(listener.total() == 0);
    }

    // ---- hiding and drop target queries -----------------------------------------------------
    R1_EXPECT(b.setContentRect(a.id, {p.x - 60, p.y - 40, 260, 160}));
    R1_EXPECT(b.setContentRect(c.id, {p.x - 20, p.y - 10, 260, 160}));
    rig->settle();
    const dock::Rect ra = *b.contentRect(a.id);
    const dock::Rect rc = *b.contentRect(c.id);
    const dock::Point inBoth{(std::max(ra.x, rc.x) + std::min(ra.right(), rc.right())) / 2.0, (std::max(ra.y, rc.y) + std::min(ra.bottom(), rc.bottom())) / 2.0};
    R1_EXPECT(b.bringToFront(c.id));
    R1_EXPECT(b.topmostWindowAt(inBoth, {}) == std::optional<FloatId>(c.id), "the topmost window shows a point covered by two");
    const FloatId skip[] = {c.id};
    R1_EXPECT(b.topmostWindowAt(inBoth, skip) == std::optional<FloatId>(a.id), "excluded windows are skipped");
    const size_t totalWindows = b.stacking().size();
    R1_EXPECT(b.setVisible(c.id, false));
    R1_EXPECT(b.topmostWindowAt(inBoth, {}) == std::optional<FloatId>(a.id), "a hidden window takes no part");
    R1_EXPECT(b.stacking().size() == totalWindows, "hiding does not remove the window");
    R1_EXPECT(b.content(c.id).has_value() && b.contentRect(c.id).has_value(), "a hidden window keeps its content");
    R1_EXPECT(b.setVisible(c.id, true) && b.topmostWindowAt(inBoth, {}) == std::optional<FloatId>(c.id));
    const std::optional<FloatId> remaining = b.topmostWindowAt(inBoth, b.stacking());
    R1_EXPECT(!remaining || *remaining == kMainWindow, "with every floating window excluded only the main window (or nothing) shows the point");

    // ---- coordinates ------------------------------------------------------------------------
    for (const FloatId w : {kMainWindow, a.id, c.id}) {
      for (const dock::Point q : {dock::Point{0, 0}, dock::Point{13.5, -7.25}, dock::Point{1000, 800}}) {
        const dock::Point round = b.toWindow(w, b.toScreen(w, q));
        R1_EXPECT(std::abs(round.x - q.x) < 1e-6 && std::abs(round.y - q.y) < 1e-6, "toWindow inverts toScreen");
      }
    }

    // ---- destruction ------------------------------------------------------------------------
    const size_t count = b.stacking().size();
    R1_EXPECT(b.destroyWindow(c.id) && !b.destroyWindow(c.id), "destroying twice is harmless and reports false the second time");
    R1_EXPECT(!b.content(c.id) && !b.contentRect(c.id) && b.stacking().size() == count - 1, "a destroyed window is gone");
    const FloatCreateResult again = b.createWindow(request({p.x, p.y, 200, 150}));
    R1_EXPECT(again.ok && again.id != c.id && again.id != a.id, "ids are not reused");
    R1_EXPECT(listener.lost.empty(), "destroyWindow does not report the window as lost");
  }
  {  // ---- the user acts on windows ----------------------------------------------------------
    std::unique_ptr<BackendRig> rig = factory();
    IFloatingBackend& b = rig->backend();
    RecordingListener listener;
    b.setListener(&listener);
    rig->settle();
    const dock::Point p = rig->pointInMain();
    const FloatCreateResult lower = b.createWindow(request({p.x - 150, p.y - 100, 240, 160}));
    const FloatCreateResult upper = b.createWindow(request({p.x + 100, p.y + 20, 240, 160}));
    rig->settle();
    if (rig->userMoves(upper.id, {p.x + 60, p.y + 40, 240, 160})) {
      rig->settle();
      R1_EXPECT(!listener.moved.empty() && listener.moved.back().first == upper.id, "a user move is reported");
      R1_EXPECT(listener.moved.back().second == *b.contentRect(upper.id), "with the final content rectangle");
      R1_EXPECT(listener.closeRequests.empty() && b.stacking().size() == 2);
    } else {
      std::printf("  (user move not simulated by this rig)\n");
    }
    listener = {};
    if (rig->userRaises(lower.id)) {
      rig->settle();
      R1_EXPECT(b.stacking().back() == lower.id, "the pressed window is on top");
      R1_EXPECT(!listener.activated.empty() && listener.activated.back() == lower.id, "and the listener is told");
    } else {
      std::printf("  (user raise not simulated by this rig)\n");
    }
    listener = {};
    if (rig->userCloses(upper.id)) {
      rig->settle();
      R1_EXPECT(listener.closeRequests == std::vector<FloatId>({upper.id}), "the close button is reported once");
      R1_EXPECT(b.content(upper.id).has_value() && b.stacking().size() == 2, "and nothing is destroyed until the host destroys it");
      R1_EXPECT(listener.lost.empty());
    } else {
      std::printf("  (user close not simulated by this rig)\n");
    }
    listener = {};
    if (rig->osDestroys(lower.id)) {
      rig->settle();
      R1_EXPECT(listener.lost == std::vector<FloatId>({lower.id}), "a window destroyed from outside is reported exactly once");
      R1_EXPECT(!b.content(lower.id) && !b.contentRect(lower.id), "and is gone");
    } else {
      std::printf("  (external destruction not simulated by this rig)\n");
    }
    b.setListener(nullptr);
    rig->userMoves(upper.id, {p.x, p.y, 240, 160});
    rig->settle();
  }
  {  // ---- limits ---------------------------------------------------------------------------
    std::unique_ptr<BackendRig> rig = factory();
    IFloatingBackend& b = rig->backend();
    rig->settle();
    const dock::Point p = rig->pointInMain();
    std::vector<FloatId> made;
    bool refused = false;
    for (int i = 0; i < 300 && !refused; ++i) {
      const FloatCreateResult r = b.createWindow(request({p.x + i, p.y + i, 120, 90}));
      if (r.ok) {
        made.push_back(r.id);
      } else {
        refused = true;
        R1_EXPECT(!r.error.empty(), "a refusal has a reason");
      }
    }
    R1_EXPECT(!made.empty());
    R1_EXPECT(b.stacking().size() == made.size() && b.contentRect(made.front()).has_value(), "refusing does not damage existing windows");
    rig->settle();
    for (const FloatId w : made) R1_EXPECT(b.destroyWindow(w));
    R1_EXPECT(b.stacking().empty());
  }
  {  // ---- pointer tracking ---------------------------------------------------------------------
    std::unique_ptr<BackendRig> rig = factory();
    IFloatingBackend& b = rig->backend();
    rig->settle();
    RecordingSink sink;
    if (b.describe().pointerTracking) {
      R1_EXPECT(b.beginPointerTracking(sink));
      if (rig->pointerSample({10, 20}, true) && rig->pointerSample({-500, 30}, true) && rig->pointerSample({-500, 30}, false)) {
        R1_EXPECT(sink.samples.size() == 3 && sink.samples[1].first.x == -500 && sink.samples[1].second && !sink.samples[2].second,
                  "samples outside every window are delivered, ending with the release");
      }
      b.endPointerTracking();
      const size_t n = sink.samples.size();
      rig->pointerSample({1, 1}, true);
      R1_EXPECT(sink.samples.size() == n, "nothing is delivered after endPointerTracking");
    } else {
      R1_EXPECT(!b.beginPointerTracking(sink), "a backend without pointer tracking says so");
      b.endPointerTracking();
    }
  }
}

}  // namespace backend_conformance
