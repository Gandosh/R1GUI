// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: EditorRig, the fixture of the preview's native-window tests: the Editor over a native rig with a
//   throwaway data folder, the pump that does what the application's loop does per iteration (the
//   editor's refresh, then the loop step), key chords as the loop delivers them, and the helpers that
//   find a panel's context and floating state.
// Callers: tests/preview/editor_float_native_test.cpp, editor_float_stress_test.cpp.
// Pacing: `pace` selects how pump() waits per step: a fixed sleep in milliseconds (the default, 16),
//   kNoWait for a loop that never waits (the stress test's unpaced variant) or kBlocking for the
//   application's own blocking step.
#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ExpectWithMessage.h"
#include "HangWatchdog.h"
#include "NativeRig.h"
#include "editor/EditorApp.h"
#include "r1ui/core/events/TreeQueries.h"
#include "r1ui/widgets/dock/DockHost.h"

namespace editor_test {

using namespace native_test;
namespace ed = preview::editor;
namespace events = r1ui::core::events;
using r1ui::core::tree::WidgetId;

// A named expectation: the message says which scenario failed.
inline void check(bool ok, const std::string& what) { r1test::report(ok, what.c_str(), __FILE__, __LINE__); }

// Progress line on stderr when R1GUI_TEST_TRACE is set (a stuck run shows where it stopped).
inline void note(const std::string& text) {
  static const bool trace = GetEnvironmentVariableA("R1GUI_TEST_TRACE", nullptr, 0) != 0;
  if (trace) std::fprintf(stderr, "%s\n", text.c_str());
}

inline constexpr uint8_t kCtrlAlt = events::Mod::kCtrl | events::Mod::kAlt;

inline events::Key letter(char c) { return static_cast<events::Key>(c); }

// The Editor over a native rig with a throwaway data folder. pump() is what the preview's loop does per
// iteration: the editor's per-frame refresh, then the backend's loop step (events, garbage collection).
struct EditorRig {
  explicit EditorRig(NativeRig& r) : rig(r) {
    root = std::filesystem::temp_directory_path() / ("r1gui-float-test-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    ed::EditorHost host;
    host.dataRoot = root;
    host.isDark = [] { return true; };
    host.setDarkTheme = [](bool) {};
    host.quit = [] {};
    host.screenNames = [] { return std::vector<std::string>{"Editor"}; };
    app = std::make_unique<ed::EditorApp>(*rig.ui, rig.mainBox, *rig.backend, std::move(host));
    pump(6);
  }
  ~EditorRig() {
    app.reset();
    rig.settle(4);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
  EditorRig(const EditorRig&) = delete;
  EditorRig& operator=(const EditorRig&) = delete;

  static constexpr int kNoWait = -1;    // never wait between steps
  static constexpr int kBlocking = -2;  // the application's loop: a blocking step with a 16 ms bound

  void pump(int steps = 4) {
    for (int i = 0; i < steps; ++i) {
      if (watchdog != nullptr) watchdog->progress();
      app->update();
      if (pace == kBlocking) {
        rig.loop->step(true, 16);
      } else {
        rig.settle(1);
        if (pace >= 0) Sleep(static_cast<DWORD>(pace));
      }
    }
  }

  // The chord as the loop delivers it: key down, key up (the editor's momentary-command hook).
  void press(UiContext& ui, events::Key key, uint8_t mods) {
    ui.setTime(ui.now() + 50);
    ui.keyDown(key, mods);
    ui.keyUp(key, mods);
    if (&ui == rig.ui.get()) app->onKeyUp(key, mods);
  }

  // Every focusable widget below `root` in the context of `ui`.
  static std::vector<WidgetId> focusables(UiContext& ui, WidgetId root) {
    std::vector<WidgetId> found;
    if (!root.valid() || !ui.alive(root)) return found;
    ui.tree().forEachDescendant(root, [&](WidgetId w) {
      if (events::isFocusable(ui.tree(), w)) found.push_back(w);
    }, true);
    return found;
  }

  // The context a panel's content is in: the main one or its window's.
  UiContext* contextOf(dock::PanelId panel) {
    for (const dock::Area& area : app->dock().layout().areas()) {
      std::vector<const dock::Node*> pending;
      if (area.root) pending.push_back(&*area.root);
      while (!pending.empty()) {
        const dock::Node* n = pending.back();
        pending.pop_back();
        for (dock::PanelId t : n->tabs) {
          if (t != panel) continue;
          const std::optional<FloatId> window = app->dock().windowOfArea(area.id);
          if (!window || *window == kMainWindow) return rig.ui.get();
          const std::optional<FloatContent> content = rig.backend->content(*window);
          return content ? content->ui : nullptr;
        }
        for (const dock::Node& c : n->children) pending.push_back(&c);
      }
    }
    return nullptr;
  }

  bool floating(dock::PanelId panel) {
    const std::optional<dock::PanelSlot> slot = app->dock().layout().locate(panel);
    return slot && slot->area != dock::kMainAreaId;
  }

  // Back to the default arrangement: every window goes, the parked ones are collected.
  void reset() {
    app->dock().resetLayout();
    pump(6);
    check(rig.backend->windowCount() == 0, "reset leaves no floating window");
    check(rig.backend->parkedCount() == 0, "reset leaves no parked window");
  }

  NativeRig& rig;
  r1test::HangWatchdog* watchdog = nullptr;
  int pace = 16;
  std::filesystem::path root;
  std::unique_ptr<ed::EditorApp> app;
};

// Opens the panel, makes it the active one and returns it ready to be floated.
inline void showPanel(EditorRig& e, dock::PanelId panel) {
  e.app->dock().openPanel(panel);
  e.app->dock().activatePanel(panel);
  e.pump(4);
}

}  // namespace editor_test
