// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the stress oracle for native floating windows over the Editor: a fixed pseudo-random mix of
//   creating windows from a key handler (Ctrl+Alt+F), destroying them (the chord inside the window,
//   closing the panel, resetting the layout, several in one loop step), hiding and showing them,
//   minimizing and restoring the main window, and moving windows to the other monitor, with every
//   window that needs a frame drawn on every step. Runs until a time budget ends; the arrangement is
//   reset at the end and nothing may leak (windows, parked windows).
// Why: the owner saw the preview stop responding after a window was floated and destroyed. Every
//   present of the process stopped because a frame's GPU work waited for the presentation engine in the
//   one queue all windows share (docs/dev/native-windows.md, "Waits"); it showed in an unpaced loop when
//   a window appeared or went away next to busy ones, and several processes made it near certain. The
//   hang watchdog turns a stall into a stack of every thread, a dump and exit code 3.
// Usage: preview-editor-float-stress-test [mode [seconds [seed]]]
//   mode: unpaced (no wait between loop steps, the hardest case) | paced (16 ms per step) | blocking (the
//   application's own blocking step, 16 ms bound) | pace<N> (N ms per step); add "-again" for the
//   scenario "the chord pressed twice inside a new window, with a widget focused" and "-churn" for plain
//   create and reset; prefix "x3-" runs three such processes at once and fails if any of them does.
//   Defaults: blocking, 20 s, seed 1.
// Callers: CTest (label gpu), the real-desktop stress drive and tools. Skips itself without a desktop or GPU.
#include "EditorNativeRig.h"

#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace editor_test;
using Clock = std::chrono::steady_clock;

namespace {

struct Mix {
  EditorRig& e;
  std::vector<dock::PanelId> panels;
  uint32_t state;
  uint64_t created = 0;
  bool again = false;  // create, then replace the window twice from its own key handler (scenario of the window-command test)
  bool churn = false;  // only create and destroy: the shortest path to a window appearing next to busy presents

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
    e.pump(4);
    const size_t before = e.rig.backend->windowCount();
    e.press(*e.rig.ui, letter('F'), kCtrlAlt);
    created += e.rig.backend->windowCount() > before ? 1 : 0;
  }

  // One random operation. Windows are created from the key handler of the main window, destroyed from
  // their own key handler, by closing or resetting, and disturbed by hiding, minimizing and moving.
  void step() {
    const dock::PanelId panel = panels[next(static_cast<uint32_t>(panels.size()))];
    if (churn) {
      floatPanel(panel);
      e.pump(1 + static_cast<int>(next(3)));
      e.app->dock().resetLayout();
      e.pump(1 + static_cast<int>(next(3)));
      return;
    }
    if (again) {  // the chord pressed twice inside the new window: each press replaces its window with a new one
      floatPanel(panel);
      e.pump(6);
      if (UiContext* inside = anyFloatingContext()) {
        const std::vector<WidgetId> inner = EditorRig::focusables(*inside, e.app->dock().contentOf(panel));
        if (!inner.empty()) inside->focusWidget(inner.front());
        e.press(*inside, letter('F'), kCtrlAlt);
        e.press(*inside, letter('F'), kCtrlAlt);
      }
      e.pump(6);
      e.app->dock().resetLayout();
      e.pump(6);
      return;
    }
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

// Runs `count` copies of this program with the mode minus its "x3-" prefix and different seeds, all at
// once; returns 0 when every copy exits 0. Several processes presenting at once load the compositor the
// way a busy desktop does, which is what made the original stall reproducible on demand.
static int runParallel(const char* mode, int seconds, uint32_t seed, int count) {
  char self[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, self, MAX_PATH);
  std::vector<PROCESS_INFORMATION> children;
  for (int i = 0; i < count; ++i) {
    std::string command = std::string("\"") + self + "\" " + mode + " " + std::to_string(seconds) + " " + std::to_string(seed + static_cast<uint32_t>(i));
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info) == FALSE) {
      std::fprintf(stderr, "cannot start child %d\n", i);
      return 1;
    }
    children.push_back(info);
  }
  int result = 0;
  for (size_t i = 0; i < children.size(); ++i) {
    const DWORD waited = WaitForSingleObject(children[i].hProcess, static_cast<DWORD>(seconds + 90) * 1000);
    DWORD code = 1;
    if (waited != WAIT_OBJECT_0) {
      TerminateProcess(children[i].hProcess, 4);
      std::fprintf(stderr, "child %zu did not finish: killed\n", i);
      result = 1;
    } else if (GetExitCodeProcess(children[i].hProcess, &code) != FALSE && code != 0) {
      std::fprintf(stderr, "child %zu exited with %lu\n", i, code);
      result = 1;
    }
    CloseHandle(children[i].hProcess);
    CloseHandle(children[i].hThread);
  }
  return result;
}

int main(int argc, char** argv) {
  const char* modeName = argc > 1 ? argv[1] : "blocking";
  const int seconds = argc > 2 ? std::atoi(argv[2]) : 20;
  const uint32_t seed = argc > 3 ? static_cast<uint32_t>(std::atoi(argv[3])) : 1u;
  if (std::strncmp(modeName, "x3-", 3) == 0) return runParallel(modeName + 3, seconds, seed, 3);
  int pace = EditorRig::kBlocking;
  const bool churn = std::strstr(modeName, "churn") != nullptr;
  if (std::strncmp(modeName, "unpaced", 7) == 0) {
    pace = EditorRig::kNoWait;
  } else if (std::strncmp(modeName, "paced", 5) == 0) {
    pace = 16;
  } else if (std::strncmp(modeName, "pace", 4) == 0) {
    pace = std::atoi(modeName + 4);
  }
  timeBeginPeriod(1);  // a sleep of N ms is N ms, as in an application that asks for a fine timer

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
  mix.churn = churn;
  mix.again = std::strstr(modeName, "again") != nullptr;
  const Clock::time_point end = Clock::now() + std::chrono::seconds(seconds);
  uint64_t steps = 0;
  size_t most = 0;
  while (Clock::now() < end) {
    mix.step();
    ++steps;
    most = std::max(most, rig->backend->windowCount());
  }
  std::printf("stress %s: %llu operations, %llu windows created, at most %zu at once\n", modeName, static_cast<unsigned long long>(steps),
              static_cast<unsigned long long>(mix.created), most);
  e.reset();
  check(rig->backend->windowCount() == 0, "no window left");
  return r1test::finish();
}
