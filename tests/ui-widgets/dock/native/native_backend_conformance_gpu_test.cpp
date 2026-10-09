// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the run of the IFloatingBackend conformance suite against NativeFloatingBackend over REAL
//   windows (every optional clause: user move, raise, close, OS destruction, pointer tracking outside
//   every window), and the clauses only a native backend has: window styles (tool window, owner,
//   rounded corners, no taskbar button), coordinate conversion against the OS, Alt+F4, user maximize,
//   hiding that keeps the pointer capture, a window destroyed from inside its own handler,
//   minimizing the main window, and bringing an unreachable window back.
// Why: slice 5.2 acceptance. The in-window rig could not simulate these clauses.
// Callers: CTest (label gpu). Skips itself (prints SKIPPED, exit 0) without an interactive desktop or
//   a usable Vulkan device. All input is synthetic (window messages, SetWindowPos, a scripted pointer
//   source): no real mouse or keyboard event is injected, no window outlives the test.
#include <dwmapi.h>

#include "BackendConformance.h"
#include "NativeRig.h"

using namespace backend_conformance;
using namespace native_test;

namespace {

std::string g_skip;

// The conformance rig: user gestures are simulated at the OS-window level (the way the OS would act
// on the window), never through the backend.
class NativeConformanceRig final : public BackendRig {
 public:
  NativeConformanceRig() : rig_(NativeRig::create(g_skip)) {
    if (!rig_) throw std::runtime_error(g_skip);
  }
  IFloatingBackend& backend() override { return *rig_->backend; }
  void settle() override { rig_->settle(); }
  dock::Point pointInMain() override { return rig_->pointInMain(); }

  // The OS (or the user dragging the caption) moves the window: SetWindowPos on the window itself.
  bool userMoves(FloatId w, const dock::Rect& target) override {
    platform::Window* win = rig_->backend->nativeWindow(w);
    if (win == nullptr) return false;
    const native::FrameMetrics f;
    const dock::Rect outer = native::outerOfContent(target, f);
    const dock::Point p = rig_->backend->screenSpace().toPhysical({outer.x, outer.y});
    const double s = win->dpiScale();
    return win->setWindowRect({static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y)), static_cast<int>(std::lround(outer.w * s)),
                               static_cast<int>(std::lround(outer.h * s))});
  }
  // Alt+F4 / the close button end in WM_CLOSE.
  bool userCloses(FloatId w) override {
    platform::Window* win = rig_->backend->nativeWindow(w);
    if (win == nullptr) return false;
    win->requestClose();
    return true;
  }
  // A press in the window, delivered to its window procedure (the way the OS does).
  bool userRaises(FloatId w) override {
    HWND hwnd = hwndOf(rig_->backend->nativeWindow(w));
    if (hwnd == nullptr) return false;
    const LPARAM at = MAKELPARAM(60, 60);
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, at);
    SendMessageW(hwnd, WM_LBUTTONUP, 0, at);
    return true;
  }
  bool osDestroys(FloatId w) override {
    HWND hwnd = hwndOf(rig_->backend->nativeWindow(w));
    return hwnd != nullptr && DestroyWindow(hwnd) != FALSE;
  }
  // The scripted pointer the backend samples while tracking.
  bool pointerSample(dock::Point p, bool down) override {
    rig_->setPointer(p, down);
    rig_->settle(1);
    return true;
  }
  NativeRig& rig() { return *rig_; }

 private:
  std::unique_ptr<NativeRig> rig_;
};

// ---- clauses only native windows have ---------------------------------------------------------------------

const platform::Window* nativeOf(NativeRig& rig, FloatId w) { return rig.backend->nativeWindow(w); }

void window_styles() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  NativeFloatingBackend& b = *rig->backend;
  const BackendInfo info = b.describe();
  R1_EXPECT(info.nativeWindows && info.pointerTracking && info.maximize && info.name == "native");
  R1_EXPECT(info.frame.left == 1 && info.frame.top == 34 && info.frame.right == 1 && info.frame.bottom == 1, "1 px border and a 34 px title bar around the content");
  const dock::Point p = rig->pointInMain();
  const FloatCreateResult made = b.createWindow(request({p.x - 100, p.y - 60, 320, 220}));
  R1_EXPECT(made.ok);
  rig->settle();
  HWND hwnd = hwndOf(b.nativeWindow(made.id));
  HWND mainHwnd = hwndOf(rig->window.get());
  R1_EXPECT(hwnd != nullptr && hwnd != mainHwnd && IsWindow(hwnd) != FALSE);
  R1_EXPECT(GetWindow(hwnd, GW_OWNER) == mainHwnd, "owned by the main window: stays above it, minimizes and restores with it");
  R1_EXPECT(GetParent(hwnd) == nullptr, "owned, not a child window");
  const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  R1_EXPECT((ex & WS_EX_TOOLWINDOW) != 0 && (ex & WS_EX_APPWINDOW) == 0, "a tool window has no taskbar button");
  R1_EXPECT((style & WS_CHILD) == 0 && (style & WS_VISIBLE) != 0 && (style & WS_THICKFRAME) != 0, "a real top-level window that the OS can resize and snap");
  DWORD corner = 0;
  R1_EXPECT(SUCCEEDED(DwmGetWindowAttribute(hwnd, 33, &corner, sizeof(corner))) && corner == 2, "rounded corners (DWMWCP_ROUND) requested from the window manager");
  R1_EXPECT(IsWindowVisible(hwnd) != FALSE);

  // Coordinates against the OS: the client origin on screen, in logical pixels, is toScreen(w, {0, 0}).
  POINT origin{0, 0};
  ClientToScreen(hwnd, &origin);
  const dock::Point logical = rig->backend->screenSpace().toLogical(origin.x, origin.y);
  const dock::Point screen = b.toScreen(made.id, {0, 0});
  R1_EXPECT(std::abs(screen.x - logical.x) < 1e-6 && std::abs(screen.y - logical.y) < 1e-6, "toScreen(window, 0 0) is the OS client origin");
  const dock::Rect content = *b.contentRect(made.id);
  R1_EXPECT(std::abs(content.x - (screen.x + 1)) < 1e-6 && std::abs(content.y - (screen.y + 34)) < 1e-6, "the content rectangle is the client area minus the drawn frame");
  RECT client{};
  GetClientRect(hwnd, &client);
  const double scale = nativeOf(*rig, made.id)->dpiScale();
  R1_EXPECT(std::abs(content.w - (client.right / scale - 2)) < 1e-6 && std::abs(content.h - (client.bottom / scale - 35)) < 1e-6);
  R1_EXPECT(b.content(made.id)->scale == scale, "FloatContent::scale is the window's display scale");
  R1_EXPECT(!b.monitorAt(screen).empty() || rig->backend->screenSpace().empty(), "a monitor name is stored with the layout");
  std::printf("  main window dpi scale %.2f, floating window scale %.2f, monitor '%s', %zu monitor(s)\n", static_cast<double>(rig->window->dpiScale()), scale,
              b.monitorAt(screen).c_str(), rig->backend->screenSpace().monitors().size());

  // The window really is the topmost thing at its own content: the drop-target query agrees with the OS.
  R1_EXPECT(b.topmostWindowAt({content.x + 20, content.y + 20}, {}) == std::optional<FloatId>(made.id));
  R1_EXPECT(b.topmostWindowAt({content.x + 20, content.y - 10}, {}) == std::optional<FloatId>(made.id), "the title bar belongs to the window");
}

void alt_f4_and_user_maximize() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  NativeFloatingBackend& b = *rig->backend;
  RecordingListener l;
  b.setListener(&l);
  const dock::Point p = rig->pointInMain();
  const FloatCreateResult made = b.createWindow(request({p.x - 100, p.y - 60, 320, 220}));
  rig->settle();
  HWND hwnd = hwndOf(b.nativeWindow(made.id));
  R1_EXPECT(l.total() == 0, "creating a window is not reported");
  // Alt+F4 reaches the window as a system command.
  SendMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
  rig->settle();
  R1_EXPECT(l.closeRequests == std::vector<FloatId>({made.id}) && b.content(made.id).has_value() && IsWindow(hwnd) != FALSE, "Alt+F4 is a close REQUEST: nothing is destroyed until the host acts");
  // The user maximizes (title bar double click, snap, or the system command).
  const dock::Rect before = *b.contentRect(made.id);
  l = {};
  SendMessageW(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
  rig->settle();
  const std::vector<std::pair<FloatId, bool>> maximizedOnce{{made.id, true}};
  const std::vector<std::pair<FloatId, bool>> restoredOnce{{made.id, false}};
  R1_EXPECT(b.isMaximized(made.id) && l.maximizedChanges == maximizedOnce, "a user maximize is reported once");
  R1_EXPECT(!l.moved.empty() && l.moved.back().second == *b.contentRect(made.id), "with the maximized content rectangle, as the in-window backend does");
  const dock::Rect big = *b.contentRect(made.id);
  R1_EXPECT(big.w > before.w && big.h > before.h);
  l = {};
  SendMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
  rig->settle();
  const dock::Rect back = *b.contentRect(made.id);
  R1_EXPECT(!b.isMaximized(made.id) && l.maximizedChanges == restoredOnce, "restoring is reported");
  R1_EXPECT(std::abs(back.w - before.w) < 1.5 && std::abs(back.h - before.h) < 1.5 && std::abs(back.x - before.x) < 1.5, "to the rectangle before");
  // A host maximize is not echoed.
  l = {};
  b.setMaximized(made.id, true);
  rig->settle();
  b.setMaximized(made.id, false);
  rig->settle();
  R1_EXPECT(l.total() == 0, "host maximize and restore are never echoed");
  b.setListener(nullptr);
}

void hiding_keeps_the_capture() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  NativeFloatingBackend& b = *rig->backend;
  RecordingListener l;
  b.setListener(&l);
  const dock::Point p = rig->pointInMain();
  const FloatCreateResult made = b.createWindow(request({p.x - 100, p.y - 60, 320, 220}));
  rig->settle();
  platform::Window* win = b.nativeWindow(made.id);
  HWND hwnd = hwndOf(win);
  SetCapture(hwnd);  // what the press on a tab does
  R1_EXPECT(GetCapture() == hwnd);
  R1_EXPECT(b.setVisible(made.id, false));
  R1_EXPECT(IsWindowVisible(hwnd) == FALSE && GetCapture() == hwnd, "a hidden window keeps the pointer capture (the drag goes on)");
  rig->settle();
  R1_EXPECT(l.total() == 0 && b.content(made.id).has_value(), "hiding is silent and keeps the content");
  R1_EXPECT(b.topmostWindowAt({b.contentRect(made.id)->x + 10, b.contentRect(made.id)->y + 10}, {}) != std::optional<FloatId>(made.id), "a hidden window takes no part in the drop query");
  // Moves reach the hidden window while the button is down, also far outside every window.
  SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-3000 & 0xFFFF, 40));
  win->takeEvents();  // discard: the point is only that the window is still addressable
  ReleaseCapture();
  R1_EXPECT(b.setVisible(made.id, true) && IsWindowVisible(hwnd) != FALSE);
  rig->settle();
  R1_EXPECT(b.topmostWindowAt({b.contentRect(made.id)->x + 10, b.contentRect(made.id)->y + 10}, {}) == std::optional<FloatId>(made.id));
  b.setListener(nullptr);
}

void destroyed_from_its_own_handler() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  NativeFloatingBackend& b = *rig->backend;
  const dock::Point p = rig->pointInMain();
  const FloatCreateResult made = b.createWindow(request({p.x - 100, p.y - 60, 320, 220}));
  rig->settle();
  UiContext* ui = b.content(made.id)->ui;
  const FloatId id = made.id;
  bool ran = false;
  bool contextStillUsable = false;
  // A timer of the window's own context destroys the window from inside that context's dispatch, the
  // way the dock destroys the window of the last tab from that window's pointer-up handler.
  ui->setTimer(0, [&] {
    ran = true;
    R1_EXPECT(b.destroyWindow(id));
    ui->frame();  // the context must still be alive here
    contextStillUsable = ui->widgetCount() > 0;
  });
  rig->settle(1);
  R1_EXPECT(ran && contextStillUsable, "the window object outlives the handler that destroyed it");
  R1_EXPECT(!b.content(id) && !b.contentRect(id) && !b.destroyWindow(id), "and is gone from every lookup at once");
  R1_EXPECT(b.parkedCount() <= 1);
  rig->settle(2);
  R1_EXPECT(b.parkedCount() == 0 && b.windowCount() == 0, "freed at the loop's safe point");
}

void os_destruction_is_reported_once() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  NativeFloatingBackend& b = *rig->backend;
  RecordingListener l;
  b.setListener(&l);
  const dock::Point p = rig->pointInMain();
  const FloatCreateResult a = b.createWindow(request({p.x - 100, p.y - 60, 320, 220}));
  const FloatCreateResult c = b.createWindow(request({p.x + 40, p.y - 30, 320, 220}));
  rig->settle();
  DestroyWindow(hwndOf(b.nativeWindow(a.id)));
  rig->settle(6);
  R1_EXPECT(l.lost == std::vector<FloatId>({a.id}), "reported exactly once, however many loop steps follow");
  R1_EXPECT(!b.destroyWindow(a.id) && b.content(c.id).has_value() && b.stacking() == std::vector<FloatId>({c.id}), "the id is invalid, the other window is untouched");
  b.destroyWindow(c.id);
  rig->settle(4);
  R1_EXPECT(l.lost.size() == 1, "a window the host destroyed is never reported lost");
  b.setListener(nullptr);
}

void minimizing_the_main_window() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  NativeFloatingBackend& b = *rig->backend;
  RecordingListener l;
  b.setListener(&l);
  const dock::Point p = rig->pointInMain();
  const FloatCreateResult made = b.createWindow(request({p.x - 100, p.y - 60, 320, 220}));
  rig->settle();
  HWND hwnd = hwndOf(b.nativeWindow(made.id));
  const dock::Rect content = *b.contentRect(made.id);
  const dock::Point inside{content.x + 30, content.y + 30};
  rig->window->minimize();
  rig->settle(3);
  R1_EXPECT(IsWindowVisible(hwnd) == FALSE, "an owned window goes with its owner when the owner is minimized");
  R1_EXPECT(!b.topmostWindowAt(inside, {}).has_value(), "no window of the application shows anything while minimized");
  R1_EXPECT(*b.contentRect(made.id) == content && b.mainContentRect().w > 0, "rectangles stay valid while minimized");
  ShowWindow(hwndOf(rig->window.get()), SW_RESTORE);
  rig->settle(4);
  R1_EXPECT(IsWindowVisible(hwnd) != FALSE && b.topmostWindowAt(inside, {}) == std::optional<FloatId>(made.id), "restored with it");
  R1_EXPECT(l.total() == 0, "minimize and restore are not reported to the host");
  b.setListener(nullptr);
}

void unreachable_windows_come_back() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  NativeFloatingBackend& b = *rig->backend;
  RecordingListener l;
  b.setListener(&l);
  const dock::Point p = rig->pointInMain();
  const FloatCreateResult made = b.createWindow(request({p.x - 100, p.y - 60, 320, 220}));
  rig->settle();
  platform::Window* win = b.nativeWindow(made.id);
  // A monitor was unplugged: the window sits where no monitor is.
  win->setWindowRect({-9000, -9000, 400, 300});
  b.onDisplayChanged();
  const platform::Rect outer = win->windowRect();
  const auto& monitors = rig->backend->screenSpace().monitors();
  bool reachable = false;
  for (const platform::MonitorInfo& m : monitors) {
    const int ox = std::min(outer.x + outer.width, m.workArea.x + m.workArea.width) - std::max(outer.x, m.workArea.x);
    const int oy = std::min(outer.y + outer.height, m.workArea.y + m.workArea.height) - std::max(outer.y, m.workArea.y);
    reachable = reachable || (ox >= 100 && oy >= 100);
  }
  R1_EXPECT(reachable, "brought back with at least the 100 px margin visible on a monitor");
  R1_EXPECT(!l.moved.empty() && l.moved.back().first == made.id && l.moved.back().second == *b.contentRect(made.id), "and the move is reported");
  // A request far away is also brought back at creation.
  const FloatCreateResult distant = b.createWindow(request({-50000, -50000, 300, 200}));
  R1_EXPECT(distant.ok);
  if (distant.ok) {
    const platform::Rect o = b.nativeWindow(distant.id)->windowRect();
    bool seen = false;
    for (const platform::MonitorInfo& m : monitors) {
      const int ox = std::min(o.x + o.width, m.workArea.x + m.workArea.width) - std::max(o.x, m.workArea.x);
      const int oy = std::min(o.y + o.height, m.workArea.y + m.workArea.height) - std::max(o.y, m.workArea.y);
      seen = seen || (ox >= 100 && oy >= 100);
    }
    R1_EXPECT(seen, "a window created off every monitor is placed on one");
  }
  b.setListener(nullptr);
}

void stacking_follows_the_os() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  NativeFloatingBackend& b = *rig->backend;
  const dock::Point p = rig->pointInMain();
  const FloatCreateResult a = b.createWindow(request({p.x - 150, p.y - 60, 300, 200}));
  const FloatCreateResult c = b.createWindow(request({p.x - 60, p.y - 20, 300, 200}));
  rig->settle();
  HWND ha = hwndOf(b.nativeWindow(a.id));
  HWND hc = hwndOf(b.nativeWindow(c.id));
  const auto above = [](HWND upper, HWND lower) {
    for (HWND w = GetWindow(upper, GW_HWNDNEXT); w != nullptr; w = GetWindow(w, GW_HWNDNEXT)) {
      if (w == lower) return true;
    }
    return false;
  };
  R1_EXPECT(above(hc, ha), "creation order is the OS z-order");
  R1_EXPECT(b.bringToFront(a.id) && b.stacking() == std::vector<FloatId>({c.id, a.id}));
  rig->settle();
  R1_EXPECT(above(ha, hc), "bringToFront changes the OS z-order too");
  R1_EXPECT(above(ha, hwndOf(rig->window.get())) && above(hc, hwndOf(rig->window.get())), "both stay above their owner");
}

}  // namespace

int main() try {
  {
    std::string skip;
    auto probe = NativeRig::create(skip);
    if (!probe) {
      std::printf("SKIPPED: %s\n", skip.c_str());
      return 0;
    }
    std::printf("native rig: GPU '%s', %zu monitor(s)\n", probe->device->gpu().name.c_str(), probe->backend->screenSpace().monitors().size());
  }
  runBackendConformance("native", [] { return std::make_unique<NativeConformanceRig>(); });
  window_styles();
  alt_f4_and_user_maximize();
  hiding_keeps_the_capture();
  destroyed_from_its_own_handler();
  os_destruction_is_reported_once();
  minimizing_the_main_window();
  unreachable_windows_come_back();
  stacking_follows_the_os();
  return r1test::finish();
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}
