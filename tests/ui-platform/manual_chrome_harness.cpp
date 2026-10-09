// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: a tiny manual harness (not a test, not in any CTest tier) that opens two borderless
//   windows whose title bar, tab-strip hole and minimize / maximize / close buttons are
//   described to the platform and painted with plain GDI rectangles, so a person can check
//   dragging, edge resize, snapping, Windows 11 snap layouts, maximize inset and the buttons.
// Run: build target ui-platform-manual, then run bin\ui-platform-manual.exe. Colors: blue = caption
//   (drag / double-click), light blue = hole inside the caption (client), grey / green / red =
//   minimize / maximize / close, dark = body. Escape closes a window. The window title (visible
//   in the taskbar tooltip) and the console print the zone under the pointer.
// Callers: a human. Exits when both windows are closed or after `--seconds N` (default: never).
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "r1ui/platform/Window.h"

using namespace r1ui::platform;

namespace {

constexpr float kBarLogical = 32.0f;
constexpr float kButtonLogical = 46.0f;

const char* zoneName(HitZone z) {
  static const char* names[] = {"Client",    "Caption",   "Minimize",  "Maximize", "Close",       "Left",     "Right",
                                "Top",       "Bottom",    "TopLeft",   "TopRight", "BottomLeft",  "BottomRight"};
  return names[static_cast<int>(z)];
}

struct Pane {
  Window window;
  std::string name;
  HitZone hover = HitZone::Client;
  ChromeLayout layout;
  Rect hole;

  Pane(const WindowDesc& desc, std::string label) : window(desc), name(std::move(label)) {}

  // Recomputes the chrome regions for the current client width and dpi.
  void relayout() {
    const float s = window.dpiScale();
    const int bar = logicalToPhysical(kBarLogical, s);
    const int button = logicalToPhysical(kButtonLogical, s);
    const int w = window.clientWidth();
    layout = ChromeLayout{};
    layout.captionRects = {{0, 0, w, bar}};
    hole = {logicalToPhysical(8.0f, s), 0, logicalToPhysical(160.0f, s), bar};
    layout.captionHoles = {hole};
    layout.closeButton = {w - button, 0, button, bar};
    layout.maximizeButton = {w - 2 * button, 0, button, bar};
    layout.minimizeButton = {w - 3 * button, 0, button, bar};
    window.setChromeLayout(layout);
  }

  void fill(HDC dc, const Rect& r, COLORREF color) const {
    const RECT rc{r.x, r.y, r.x + r.width, r.y + r.height};
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
  }

  void paint() const {
    HWND hwnd = static_cast<HWND>(window.nativeHandle().window);
    HDC dc = GetDC(hwnd);
    if (dc == nullptr) return;
    const Rect body{0, 0, window.clientWidth(), window.clientHeight()};
    fill(dc, body, RGB(32, 34, 40));
    for (const Rect& r : layout.captionRects) fill(dc, r, RGB(40, 90, 200));
    fill(dc, hole, RGB(120, 170, 240));
    fill(dc, layout.minimizeButton, hover == HitZone::MinimizeButton ? RGB(190, 190, 190) : RGB(110, 110, 110));
    fill(dc, layout.maximizeButton, hover == HitZone::MaximizeButton ? RGB(90, 220, 120) : RGB(40, 150, 70));
    fill(dc, layout.closeButton, hover == HitZone::CloseButton ? RGB(240, 80, 80) : RGB(170, 40, 40));
    ReleaseDC(hwnd, dc);
  }

  void handleEvents() {
    for (const Event& e : window.takeEvents()) {
      switch (e.type) {
        case EventType::Resized: relayout(); break;
        case EventType::MouseMove:
          hover = window.chromeZoneAt({static_cast<int>(e.x), static_cast<int>(e.y)});
          std::printf("%s: move %.0f,%.0f zone=%s\n", name.c_str(), static_cast<double>(e.x), static_cast<double>(e.y), zoneName(hover));
          std::fflush(stdout);
          window.setTitle(name + " zone=" + zoneName(hover));
          break;
        case EventType::MouseLeave: hover = HitZone::Client; break;
        case EventType::KeyDown:
          if (e.virtualKey == keys::kEscape) window.requestClose();
          break;
        case EventType::CloseRequested: std::printf("%s: close requested\n", name.c_str()); break;
        case EventType::DpiChanged: std::printf("%s: dpi scale %.2f\n", name.c_str(), static_cast<double>(e.dpiScale)); relayout(); break;
        default: break;
      }
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  int seconds = 0;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--seconds") == 0) seconds = std::atoi(argv[i + 1]);
  }
  WindowDesc a;
  a.title = "A";
  a.width = 640;
  a.height = 420;
  a.borderless = true;
  a.position = Point{120, 120};
  a.minSize = Size{360, 200};
  WindowDesc b = a;
  b.title = "B (tool window)";
  b.position = Point{820, 220};
  b.width = 480;
  b.toolWindow = true;
  std::unique_ptr<Pane> panes[] = {std::make_unique<Pane>(a, "A"), std::make_unique<Pane>(b, "B")};
  for (auto& p : panes) p->relayout();

  const ULONGLONG start = GetTickCount64();
  while (panes[0] || panes[1]) {
    for (auto& p : panes) {
      if (!p) continue;
      const bool open = p->window.pumpEvents();
      p->handleEvents();
      if (open) {
        p->paint();
      } else {
        p.reset();  // a closed window disappears when its owner destroys it
      }
    }
    if (seconds > 0 && GetTickCount64() - start > static_cast<ULONGLONG>(seconds) * 1000) break;
    Sleep(16);
  }
  return 0;
}
