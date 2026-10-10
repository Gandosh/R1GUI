// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the stress oracle for native floating windows over the Editor: a fixed pseudo-random mix of
//   creating windows from a key handler (Ctrl+Alt+F), destroying them (the chord inside the window,
//   closing the panel, resetting the layout, several in one loop step), hiding and showing them,
//   minimizing and restoring the main window, and moving windows to the other monitor, with every
//   window that needs a frame drawn on every step. Runs until a time budget ends; the arrangement is
//   reset at the end and nothing may leak (windows, parked windows, validation messages).
// Why: the owner saw the preview stop responding after a window was floated and destroyed; the
//   stalls sat in the GPU/compositor waits of window creation, drawing and destruction (see
//   docs/dev/native-windows.md, "Waits"). A deterministic mix that exercises those paths for tens of
//   seconds is the regression guard; the hang watchdog turns a stall into a stack, a dump and a failure.
// Usage: preview-editor-float-stress-test [mode [seconds [seed]]]  mode: unpaced | paced | blocking.
//   unpaced never waits between loop steps (the hardest case), paced sleeps 16 ms per step, blocking
//   is the application's own blocking step. Defaults: blocking, 20 s, seed 1.
// Callers: CTest (label gpu), tools and the stress drivers. Skips itself without a desktop or GPU.
#include "EditorNativeRig.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

using namespace editor_test;
using Clock = std::chrono::steady_clock;

namespace {

struct Mix {
  EditorRig& e;
  std::vector<dock::PanelId> panels;
  uint32_t state;

  uint32_t next(uint32_t bound) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state % bound;
  }

  std::vector<FloatId> windows() const { return e.rig.backend->stacking(); }

  UiContext* anyFloatingContext() {
    const std::vector<FloatId> ids = windows();
    if (ids.empty()) return nullptr;
    const std::optional<FloatContent> content = e.rig.backend->content(ids[next(static_cast<uint32_t>(ids.size()))]);
    return content ? content->ui : nullptr;
  }

  void floatPanel(dock::PanelId panel) {
    e.app->dock().openPanel(panel);
    e.app->dock().activatePanel(panel);
    e.press(*e.rig.ui, letter('F'), kCtrlAlt);
  }

  // One random operation. Windows are created from the key handler of the main window, destroyed from
  // their own key handler, by closing or resetting, and disturbed by hiding, minimizing and moving.
  void step() {
    const dock::PanelId panel = panels[next(static_cast<uint32_t>(panels.size()))];
    switch (next(12)) {
      case 0:
      case 1:
      case 2: floatPanel(panel); break;
      case 3:
        if (UiContext* inside = anyFloatingContext()) e.press(*inside, letter('F'), kCtrlAlt);
        break;
      case 4: e.app->dock().resetLayout(); break;
      case 5: e.app->dock().closePanel(panel); break;
      case 6: {  // two windows created inside one loop step
        floatPanel(panel);
        floatPanel(panels[next(static_cast<uint32_t>(panels.size()))]);
        break;
      }
      case 7: {  // hide a window for a few steps, show it again
        const std::vector<FloatId> ids = windows();
        if (ids.empty()) break;
        const FloatId id = ids[next(static_cast<uint32_t>(ids.size()))];
        e.rig.backend->setVisible(id, false);
        e.pump(1 + static_cast<int>(next(3)));
        e.rig.backend->setVisible(id, true);
        break;
      }
      case 8: {  // minimize the main window, restore it
        const HWND main = hwndOf(e.rig.window.get());
        ShowWindow(main, SW_MINIMIZE);
        e.pump(1 + static_cast<int>(next(4)));
        ShowWindow(main, SW_RESTORE);
        break;
      }
      case 9: {  // move a window to a random monitor
        const std::vector<FloatId> ids = windows();
        const auto& monitors = e.rig.backend->screenSpace().monitors();
        if (ids.empty() || monitors.empty()) break;
        const r1ui::platform::MonitorInfo& m = monitors[next(static_cast<uint32_t>(monitors.size()))];
        const dock::Point origin = e.rig.backend->screenSpace().toLogical(m.workArea.x + 80.0 + next(200), m.workArea.y + 80.0 + next(200));
        e.rig.backend->setContentRect(ids[next(static_cast<uint32_t>(ids.size()))], {origin.x, origin.y, 360.0 + next(200), 260.0 + next(120)});
        break;
      }
      default: break;
    }
    e.pump(static_cast<int>(next(4)));
  }
};

}  // namespace

int main(int argc, char** argv) {
  const char* modeName = argc > 1 ? argv[1] : "blocking";
  const int seconds = argc > 2 ? std::atoi(argv[2]) : 20;
  const uint32_t seed = argc > 3 ? static_cast<uint32_t>(std::atoi(argv[3])) : 1u;
  int pace = EditorRig::kBlocking;
  if (std::strcmp(modeName, "unpaced") == 0) pace = EditorRig::kNoWait;
  else if (std::strcmp(modeName, "paced") == 0) pace = 16;

  std::string skip;
  std::unique_ptr<NativeRig> rig = NativeRig::create(skip);
  if (!rig) {
    std::printf("SKIPPED: %s\n", skip.c_str());
    return 0;
  }
  r1test::HangWatchdog watchdog(std::chrono::seconds(20));
  EditorRig e(*rig);
  e.watchdog = &watchdog;
  e.pace = pace;
  Mix mix{e, {ed::panel::kViewport, ed::panel::kOutliner, ed::panel::kInspector, ed::panel::kAssets, ed::panel::kCurves, ed::panel::kConsole,
              ed::panel::kCommands, ed::panel::kShortcuts}, seed * 2654435761u + 1u};
  const Clock::time_point end = Clock::now() + std::chrono::seconds(seconds);
  uint64_t steps = 0;
  size_t most = 0;
  while (Clock::now() < end) {
    mix.step();
    ++steps;
    most = std::max(most, rig->backend->windowCount());
  }
  std::printf("stress %s: %llu operations, at most %zu windows at once\n", modeName, static_cast<unsigned long long>(steps), most);
  e.reset();
  check(rig->backend->windowCount() == 0, "no window left");
  return r1test::finish();
}
