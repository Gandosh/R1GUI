// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the run of the IFloatingBackend conformance suite against InWindowFloatingBackend, with a
//   rig that drives its frames with real pointer events, and the checks that belong to this
//   backend only (frame insets, keeping the title bar reachable, rounded 8 px corners option).
// Why: the in-window backend is the reference implementation of the contract; a native backend will
//   be tested by supplying its own BackendRig to the same suite.
// Callers: CTest (label fast).
#include "BackendConformance.h"
#include "r1ui/widgets/dock/DockInteraction.h"
#include "r1ui/widgets/dock/InWindowFloatingBackend.h"

using namespace backend_conformance;

namespace {

class Box : public WidgetObject {
 public:
  const char* typeName() const override { return "Box"; }
  void onAttached() override {
    style().flexGrow = 1.0;
    style().minHeight = r1ui::core::layout::Length::px(0);
  }
};

class InWindowRig final : public BackendRig {
 public:
  InWindowRig() : t(900, 600) {
    mainBox = t.ui.create<Box>(t.ui.root()).id();
    backendImpl = std::make_unique<InWindowFloatingBackend>(t.ui, t.ui.root());
    backendImpl->setMainContent(t.ui, mainBox);
  }
  IFloatingBackend& backend() override { return *backendImpl; }
  void settle() override {
    for (int i = 0; i < 3; ++i) t.ui.frame();
  }
  dock::Point pointInMain() override {
    const dock::Rect r = backendImpl->mainContentRect();
    return {r.x + r.w / 2.0, r.y + r.h / 2.0};
  }
  dock::Rect frame(FloatId w) { return toDockRect(t.ui.absRect(backendImpl->frameWidget(w))); }
  bool userMoves(FloatId w, const dock::Rect& target) override {
    const dock::Rect now = *backendImpl->contentRect(w);
    const dock::Rect f = frame(w);
    const dock::Point from{f.x + 40, f.y + 12};
    t.ui.pointerMove(from.x, from.y);
    t.ui.pointerDown(from.x, from.y);
    t.ui.pointerMove(from.x + (target.x - now.x) / 2, from.y + (target.y - now.y) / 2);
    t.ui.pointerMove(from.x + (target.x - now.x), from.y + (target.y - now.y));
    t.ui.pointerUp(from.x + (target.x - now.x), from.y + (target.y - now.y));
    return true;
  }
  bool userCloses(FloatId w) override {
    const dock::Rect f = frame(w);
    t.ui.pointerMove(f.x + f.w - 20, f.y + 17);
    t.ui.pointerDown(f.x + f.w - 20, f.y + 17);
    t.ui.pointerUp(f.x + f.w - 20, f.y + 17);
    return true;
  }
  bool userRaises(FloatId w) override {
    const dock::Rect f = frame(w);
    t.ui.pointerMove(f.x + 40, f.y + 12);
    t.ui.pointerDown(f.x + 40, f.y + 12);
    t.ui.pointerUp(f.x + 40, f.y + 12);
    return true;
  }

  r1test::TestUi t;
  r1ui::core::tree::WidgetId mainBox;
  std::unique_ptr<InWindowFloatingBackend> backendImpl;
};

void in_window_specifics() {
  InWindowRig rig;
  IFloatingBackend& b = rig.backend();
  rig.settle();
  const BackendInfo info = b.describe();
  R1_EXPECT(!info.nativeWindows && !info.pointerTracking && info.maximize);
  R1_EXPECT(info.frame.top == 34 && info.frame.left == 1 && info.frame.bottom == 1, "title bar 34 px (spec 03 rule 57), 1 px border");
  const FloatCreateResult w = b.createWindow(request({100, 100, 300, 200}));
  rig.settle();
  const dock::Rect f = rig.frame(w.id);
  const dock::Rect c = *b.contentRect(w.id);
  R1_EXPECT(f.x == c.x - 1 && f.y == c.y - 34 && f.w == c.w + 2 && f.h == c.h + 34 + 1, "frame = content plus the insets");
  // Toward the screen: window coordinates are the context's.
  R1_EXPECT(b.toScreen(w.id, {5, 6}) == dock::Point({5, 6}));
  // The title bar cannot leave the host window.
  b.setContentRect(w.id, {5000, 5000, 300, 200});
  const dock::Rect gone = *b.contentRect(w.id);
  R1_EXPECT(gone.x < 900 && gone.y < 600);
  b.setContentRect(w.id, {-5000, -5000, 300, 200});
  const dock::Rect away = *b.contentRect(w.id);
  R1_EXPECT(away.x + away.w > 0 && away.y >= 34, "kept reachable");
  // A window larger than the host window is limited to it.
  b.setContentRect(w.id, {0, 34, 5000, 5000});
  const dock::Rect big = *b.contentRect(w.id);
  R1_EXPECT(big.w <= 900 && big.h <= 600, "never larger than the host window");
  // The frame is the widget the pointer hits for the title bar, and the content holder is its child.
  const r1ui::core::tree::WidgetId holder = b.content(w.id)->parent;
  R1_EXPECT(rig.t.ui.tree().parent(holder) == rig.backendImpl->frameWidget(w.id));
  // Destroying the backend's window removes the widgets.
  const r1ui::core::tree::WidgetId frameId = rig.backendImpl->frameWidget(w.id);
  b.destroyWindow(w.id);
  R1_EXPECT(!rig.t.ui.alive(frameId) && !rig.t.ui.alive(holder));
}


// A backend that breaks the echo rule (it reports host-driven rectangle changes to the listener) and
// returns the wrong order from stacking(): the suite must notice both.
class BrokenBackend final : public IFloatingBackend {
 public:
  explicit BrokenBackend(InWindowFloatingBackend& inner) : inner_(inner) {}
  BackendInfo describe() const override { return inner_.describe(); }
  void setListener(IFloatingListener* l) override {
    listener_ = l;
    inner_.setListener(l);
  }
  void setMainContent(UiContext& ui, r1ui::core::tree::WidgetId w) override { inner_.setMainContent(ui, w); }
  dock::Rect mainContentRect() const override { return inner_.mainContentRect(); }
  FloatCreateResult createWindow(const FloatRequest& r) override { return inner_.createWindow(r); }
  bool destroyWindow(FloatId w) override { return inner_.destroyWindow(w); }
  bool setContentRect(FloatId w, const dock::Rect& r) override {
    const bool ok = inner_.setContentRect(w, r);
    if (ok && listener_ != nullptr) listener_->onFloatMoved(w, *inner_.contentRect(w));
    return ok;
  }
  std::optional<dock::Rect> contentRect(FloatId w) const override { return inner_.contentRect(w); }
  std::optional<FloatContent> content(FloatId w) const override { return inner_.content(w); }
  bool bringToFront(FloatId w) override { return inner_.bringToFront(w); }
  std::vector<FloatId> stacking() const override {
    std::vector<FloatId> order = inner_.stacking();
    std::reverse(order.begin(), order.end());
    return order;
  }
  bool setTitle(FloatId w, std::string_view t) override { return inner_.setTitle(w, t); }
  bool setMaximized(FloatId w, bool m) override { return inner_.setMaximized(w, m); }
  bool isMaximized(FloatId w) const override { return inner_.isMaximized(w); }
  bool setVisible(FloatId w, bool v) override { return inner_.setVisible(w, v); }
  dock::Point toScreen(FloatId w, dock::Point p) const override { return inner_.toScreen(w, p); }
  dock::Point toWindow(FloatId w, dock::Point p) const override { return inner_.toWindow(w, p); }
  std::optional<FloatId> topmostWindowAt(dock::Point p, std::span<const FloatId> x) const override { return inner_.topmostWindowAt(p, x); }
  std::string monitorAt(dock::Point p) const override { return inner_.monitorAt(p); }
  bool beginPointerTracking(IPointerSink& s) override { return inner_.beginPointerTracking(s); }
  void endPointerTracking() override { inner_.endPointerTracking(); }

 private:
  InWindowFloatingBackend& inner_;
  IFloatingListener* listener_ = nullptr;
};

class BrokenRig final : public BackendRig {
 public:
  BrokenRig() : inner(), broken(*inner.backendImpl) {}
  IFloatingBackend& backend() override { return broken; }
  void settle() override { inner.settle(); }
  dock::Point pointInMain() override { return inner.pointInMain(); }

 private:
  InWindowRig inner;
  BrokenBackend broken;
};

void the_suite_detects_a_broken_backend() {
  const int before = r1test::failureCount();
  runBackendConformance("BrokenBackend (expected to fail)", [] { return std::unique_ptr<BackendRig>(new BrokenRig()); });
  const int found = r1test::failureCount() - before;
  r1test::failureCount() = before;  // these failures are the point of the test
  std::printf("  the suite reported %d violations of the broken backend\n", found);
  R1_EXPECT(found >= 2, "echo and stacking violations are detected");
}
}  // namespace

int main() {
  runBackendConformance("InWindowFloatingBackend", [] { return std::unique_ptr<BackendRig>(new InWindowRig()); });
  in_window_specifics();
  the_suite_detects_a_broken_backend();
  return r1test::finish();
}
