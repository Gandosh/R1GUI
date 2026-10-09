// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DockHost with the native backend over real windows, driven by synthetic toolkit pointer input:
//   a tab dragged out of the main window to empty desktop space becomes a native floating window; the
//   same tab dragged back into a main-window strip docks and the window disappears; a tab dragged from
//   one floating window to another joins it and the emptied window is destroyed from inside its own
//   pointer handler; the OS moving or resizing a window updates the model; closing a window (Alt+F4)
//   closes its panels into the closed-panel memory; a window the OS destroys does the same; minimizing
//   the main window; plus the idle loop using no CPU.
// Why: slice 5.2 acceptance at the toolkit level (the real-desktop drive with injected OS input is a
//   separate script against the manual harness).
// Callers: CTest (label gpu). Skips itself (SKIPPED, exit 0) without an interactive desktop or GPU.
#include <windows.h>

#include <map>

#include "ExpectWithMessage.h"
#include "NativeRig.h"
#include "r1ui/widgets/dock/DockHost.h"
#include "r1ui/widgets/dock/DockTabStrip.h"

using namespace native_test;
using r1ui::core::tree::WidgetId;

namespace {

struct DockNative {
  explicit DockNative(NativeRig& r) : rig(r) {
    for (dock::PanelId id = 1; id <= 6; ++id) {
      PanelDescriptor d;
      d.id = id;
      d.title = "Panel " + std::to_string(id);
      d.floatSize = {320.0, 220.0};
      d.factory = [this, id](UiContext& ui, WidgetId parent) {
        ++factoryCalls[id];
        return ui.create<Box>(parent).id();
      };
      registry.add(std::move(d));
    }
    host = &rig.ui->create<DockHost>(rig.ui->root(), registry, *rig.backend);
    host->setOnChanged([this](DockChange c) { changes.push_back(c); });
    dock::DockLayoutResult created = dock::DockLayout::create(
        registry.infos(), host->options().config,
        dock::Node::split(dock::Axis::Row, {dock::Node::stack({1, 2, 3}, 0, 1.0), dock::Node::stack({4, 5}, 0, 1.0)}));
    if (!created.ok()) throw std::runtime_error(created.error);
    host->setLayout(std::move(*created.layout));
    rig.settle();
  }

  // The host goes first: it destroys its floating windows through the backend while it detaches.
  ~DockNative() {
    if (rig.ui->alive(host->id())) rig.ui->destroy(host->id());
  }
  DockNative(const DockNative&) = delete;
  DockNative& operator=(const DockNative&) = delete;

  // The tab's centre in screen logical pixels (works for tabs in any window).
  dock::Point tabScreen(dock::PanelId panel) const {
    DockTabStrip* s = host->stripOf(panel);
    const dock::Rect r = s->tabRect(*s->indexOf(panel));
    return rig.backend->toScreen(s->window(), {r.x + r.w / 2.0, r.y + r.h / 2.0});
  }
  // A point of the primary monitor, `fx` and `fy` of the way across its work area (screen logical px):
  // empty desktop space beside the main window, wherever the monitor's size puts it.
  dock::Point desktop(double fx, double fy) const {
    const platform::MonitorInfo& m = rig.backend->screenSpace().monitors().front();
    return rig.backend->screenSpace().toLogical(m.workArea.x + m.workArea.width * fx, m.workArea.y + m.workArea.height * fy);
  }
  FloatId windowOf(dock::PanelId panel) const { return host->stripOf(panel)->window(); }

  // A toolkit-level drag: presses in the window holding `panel`'s tab and moves to `target` (screen
  // logical px) in steps, as the platform would deliver to the capturing window. The loop runs between
  // steps so frames, listener calls and parked windows are handled like in a real run.
  void drag(dock::PanelId panel, dock::Point target, bool release = true) {
    DockTabStrip* strip = host->stripOf(panel);
    UiContext& ui = strip->ui();
    const FloatId window = strip->window();
    const dock::Point from = tabScreen(panel);
    const auto local = [&](dock::Point screen) { return rig.backend->toWindow(window, screen); };
    dock::Point p = local(from);
    ui.pointerMove(p.x, p.y);
    ui.pointerDown(p.x, p.y);
    for (int i = 1; i <= 10; ++i) {
      p = local({from.x + (target.x - from.x) * i / 10.0, from.y + (target.y - from.y) * i / 10.0});
      ui.pointerMove(p.x, p.y);
      rig.settle(1);
    }
    if (release) {
      ui.pointerUp(p.x, p.y);
      rig.settle(4);
    }
  }

  size_t topLevelWindows() const {
    struct Count {
      DWORD pid = GetCurrentProcessId();
      size_t n = 0;
    } c;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
          auto* c = reinterpret_cast<Count*>(lp);
          DWORD pid = 0;
          GetWindowThreadProcessId(h, &pid);
          wchar_t cls[64]{};
          GetClassNameW(h, cls, 64);
          if (pid == c->pid && IsWindowVisible(h) != FALSE && std::wstring(cls) == L"R1GUI.Window") ++c->n;
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&c));
    return c.n;
  }

  const dock::Area* areaOf(dock::PanelId panel) const {
    const auto slot = host->layout().locate(panel);
    if (!slot) return nullptr;
    for (const dock::Area& a : host->layout().areas()) {
      if (a.id == slot->area) return &a;
    }
    return nullptr;
  }

  NativeRig& rig;
  PanelRegistry registry;
  DockHost* host = nullptr;
  std::map<dock::PanelId, int> factoryCalls;
  std::vector<DockChange> changes;
};

void tab_out_and_back() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  DockNative d(*rig);
  R1_EXPECT(d.topLevelWindows() == 1 && d.rig.backend->windowCount() == 0, "just the main window");
  const dock::Point desktop = d.desktop(0.62, 0.45);  // empty desktop space beside the main window (primary monitor)
  R1_EXPECT(d.rig.backend->topmostWindowAt(desktop, {}) == std::nullopt, "no window of ours there");

  // 1. Drag Panel 2 out of the main window and drop it on the desktop.
  d.drag(2, desktop);
  R1_EXPECT(d.rig.backend->windowCount() == 1 && d.topLevelWindows() == 2, "a native window appeared");
  const FloatId w = d.windowOf(2);
  R1_EXPECT(w != kMainWindow && d.rig.backend->nativeWindow(w) != nullptr);
  const dock::Area* area = d.areaOf(2);
  R1_EXPECT(area != nullptr && area->id != dock::kMainAreaId, "Panel 2 left the main area");
  const dock::Rect content = *d.rig.backend->contentRect(w);
  R1_EXPECT(area != nullptr && area->rect == content, "the model rectangle is what the OS shows");
  R1_EXPECT(std::abs(content.x - (desktop.x - 80)) < 400 && content.w >= 64, "placed where the ghost was dropped");
  R1_EXPECT(d.factoryCalls[2] >= 2, "the content is recreated in the new window's own context");
  d.rig.settle(3);
  R1_EXPECT(d.rig.backend->framesPresented(w) > 0, "the new window rendered frames");
  HWND hwnd = hwndOf(d.rig.backend->nativeWindow(w));
  R1_EXPECT(GetWindow(hwnd, GW_OWNER) == hwndOf(d.rig.window.get()) && (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0);

  // 2. Drag it back onto the strip of the second main region: it docks and the window disappears.
  const dock::Point strip4 = d.tabScreen(4);
  d.drag(2, {strip4.x + 70, strip4.y});
  R1_EXPECT(d.rig.backend->windowCount() == 0 && d.topLevelWindows() == 1, "the window is gone");
  const dock::Area* docked = d.areaOf(2);
  R1_EXPECT(docked != nullptr && docked->id == dock::kMainAreaId && d.windowOf(2) == kMainWindow, "Panel 2 is docked in the main area again");
  R1_EXPECT(d.host->stripOf(2) == d.host->stripOf(4), "in the strip of Panel 4");
  R1_EXPECT(d.rig.backend->parkedCount() == 0, "nothing is left parked");
}

void between_floating_windows() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  DockNative d(*rig);
  d.drag(2, d.desktop(0.62, 0.35));
  d.drag(5, d.desktop(0.85, 0.65));
  R1_EXPECT(d.rig.backend->windowCount() == 2 && d.topLevelWindows() == 3);
  const FloatId wa = d.windowOf(2);
  const FloatId wb = d.windowOf(5);
  R1_EXPECT(wa != wb && wa != kMainWindow && wb != kMainWindow);
  const std::vector<FloatId> order = d.rig.backend->stacking();
  R1_EXPECT(order.size() == 2 && order.back() == wb, "the window created last is on top");
  // The two windows must not cover each other's tabs for this test.
  const dock::Rect ra = *d.rig.backend->contentRect(wa);
  const dock::Rect rb = *d.rig.backend->contentRect(wb);
  R1_EXPECT(!(ra.x < rb.right() && rb.x < ra.right() && ra.y - 34 < rb.bottom() && rb.y - 34 < ra.bottom()), "the two windows are apart");
  // Drag Panel 5 from its window onto the strip of Panel 2's window.
  const dock::Point target = d.tabScreen(2);
  d.drag(5, {target.x + 60, target.y});
  R1_EXPECT(d.rig.backend->windowCount() == 1 && d.topLevelWindows() == 2, "the emptied window was destroyed (from inside its own pointer handler)");
  R1_EXPECT(d.host->stripOf(5) == d.host->stripOf(2) && d.windowOf(5) == wa, "Panel 5 joined Panel 2's strip in the other floating window");
  R1_EXPECT(d.rig.backend->parkedCount() == 0);
  d.rig.settle(3);
  R1_EXPECT(d.rig.backend->framesPresented(wa) > 0);
}

void os_moves_resizes_closes() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  DockNative d(*rig);
  d.drag(2, d.desktop(0.62, 0.45));
  const FloatId w = d.windowOf(2);
  platform::Window* win = d.rig.backend->nativeWindow(w);
  const uint32_t areaId = d.areaOf(2)->id;
  const dock::Rect before = *d.rig.backend->contentRect(w);

  // The user drags the title bar (the OS moves the window): the model follows.
  const platform::Rect outer = win->windowRect();
  win->setWindowRect({outer.x + 120, outer.y + 70, outer.width, outer.height});
  d.rig.settle(3);
  const dock::Rect moved = *d.rig.backend->contentRect(w);
  R1_EXPECT(std::abs(moved.x - (before.x + 120)) < 1.5 && std::abs(moved.y - (before.y + 70)) < 1.5 && moved.w == before.w, "moved by the OS");
  R1_EXPECT(d.areaOf(2)->rect == moved && d.changes.back() == DockChange::Window, "the model and the observer followed");
  DockAreaView* view = d.host->areaView(areaId);
  R1_EXPECT(view != nullptr, "the area view lives in the window");

  // Resize: the area view fills the new content rectangle.
  const platform::Rect o2 = win->windowRect();
  win->setWindowRect({o2.x, o2.y, o2.width + 140, o2.height + 90});
  d.rig.settle(4);
  const dock::Rect grown = *d.rig.backend->contentRect(w);
  R1_EXPECT(std::abs(grown.w - (moved.w + 140)) < 1.5 && std::abs(grown.h - (moved.h + 90)) < 1.5, "resized by the OS");
  UiContext& wui = d.host->stripOf(2)->ui();
  const auto holderRect = wui.absRect(d.rig.backend->content(w)->parent);
  R1_EXPECT(std::abs(holderRect.w - grown.w) < 1.5 && std::abs(holderRect.h - grown.h) < 1.5, "the content holder fills the content rectangle");
  R1_EXPECT(d.areaOf(2)->rect == grown);

  // Alt+F4: the host closes the panel, which destroys the window.
  SendMessageW(hwndOf(win), WM_SYSCOMMAND, SC_CLOSE, 0);
  d.rig.settle(4);
  R1_EXPECT(d.rig.backend->windowCount() == 0 && d.topLevelWindows() == 1, "closing the window closed its panel");
  R1_EXPECT(!d.host->layout().isDocked(2), "Panel 2 sits in the closed-panel memory");
  R1_EXPECT(d.host->openPanel(2), "and reopens");
  d.rig.settle(3);
  R1_EXPECT(d.host->layout().isDocked(2));
}

void os_destroys_a_window() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  DockNative d(*rig);
  d.drag(3, d.desktop(0.62, 0.45));
  const FloatId w = d.windowOf(3);
  R1_EXPECT(DestroyWindow(hwndOf(d.rig.backend->nativeWindow(w))) != FALSE);
  d.rig.settle(6);
  R1_EXPECT(d.rig.backend->windowCount() == 0 && !d.host->layout().isDocked(3), "the host closed the panel of a window that vanished");
  R1_EXPECT(d.host->openPanel(3) && d.host->layout().isDocked(3), "it reopens");
}

void minimize_and_restore() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  DockNative d(*rig);
  d.drag(2, d.desktop(0.62, 0.45));
  const FloatId w = d.windowOf(2);
  const dock::Rect rect = *d.rig.backend->contentRect(w);
  const dock::Rect main = d.rig.backend->mainContentRect();
  d.rig.window->minimize();
  d.rig.settle(3);
  R1_EXPECT(!IsWindowVisible(hwndOf(d.rig.backend->nativeWindow(w))), "the floating window went with the main window");
  R1_EXPECT(d.rig.backend->mainContentRect() == main, "the main content rectangle is kept while minimized");
  ShowWindow(hwndOf(d.rig.window.get()), SW_RESTORE);
  d.rig.settle(4);
  R1_EXPECT(IsWindowVisible(hwndOf(d.rig.backend->nativeWindow(w))) != FALSE && *d.rig.backend->contentRect(w) == rect, "and came back where it was");
  R1_EXPECT(d.host->layout().isDocked(2) && d.rig.backend->windowCount() == 1);
}

void idle_uses_no_cpu() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  DockNative d(*rig);
  d.drag(2, d.desktop(0.62, 0.45));
  d.rig.settle(8);
  const auto cpuMs = [] {
    FILETIME create, exit, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
    const auto to100ns = [](FILETIME f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return static_cast<double>(to100ns(kernel) + to100ns(user)) / 10000.0;
  };
  const uint64_t waitsBefore = d.rig.loop->idleWaits();
  const uint64_t framesA = d.rig.backend->framesPresented(d.windowOf(2));
  const double cpu0 = cpuMs();
  const auto t0 = GetTickCount64();
  while (GetTickCount64() - t0 < 700) d.rig.loop->step(true, 200);
  const double cpu = cpuMs() - cpu0;
  R1_EXPECT(d.rig.loop->idleWaits() - waitsBefore <= 20, "the loop slept instead of spinning");
  R1_EXPECT(d.rig.backend->framesPresented(d.windowOf(2)) == framesA, "an idle window draws nothing");
  std::printf("  idle: 700 ms of blocking steps used %.1f ms of CPU, %llu waits\n", cpu, static_cast<unsigned long long>(d.rig.loop->idleWaits() - waitsBefore));
  R1_EXPECT(cpu < 100.0, "no spinning while idle");
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
  }
  tab_out_and_back();
  between_floating_windows();
  os_moves_resizes_closes();
  os_destroys_a_window();
  minimize_and_restore();
  idle_uses_no_cpu();
  return r1test::finish();
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}
