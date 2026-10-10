// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the oracle for the Editor's window commands (Float tab, Move tab group, Next/Previous tab,
//   Close tab) pressed as their key chords over the NATIVE floating backend: every panel of the Editor
//   is floated in turn with the keyboard focus inside its own content (every focusable widget of it, one
//   at a time), the chord is pressed again inside the floating window the panel already sits in, twice
//   in a row without a loop step between, and after each scenario the arrangement is reset and the
//   window count, the parked windows and the Vulkan validation layer are checked (nothing leaks).
// Why: a panel's content lives in the context of the window it is shown in, so floating (or docking)
//   the panel destroys the content, and the context, that is dispatching the very key event that asked
//   for it. The contract (docs/dev/docking.md section 5) is that this is legal; this test proves it
//   for every panel type of the preview. The loop is paced like the application's (a short wait per step):
//   a hot loop of frames with no wait stalls the display driver's present call for good, which is a
//   property of the test loop, not of the commands.
// Callers: CTest (label gpu). Skips itself (SKIPPED, exit 0) without an interactive desktop or GPU.
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <filesystem>
#include <string>
#include <vector>

#include "ExpectWithMessage.h"
#include "HangWatchdog.h"
#include "NativeRig.h"
#include "editor/EditorApp.h"
#include "r1ui/core/events/TreeQueries.h"
#include "r1ui/widgets/dock/DockHost.h"

using namespace native_test;
namespace ed = preview::editor;
namespace events = r1ui::core::events;
using r1ui::core::tree::WidgetId;

namespace {

// A named expectation: the message says which scenario failed.
void check(bool ok, const std::string& what) { r1test::report(ok, what.c_str(), __FILE__, __LINE__); }

// Progress line on stderr when R1GUI_TEST_TRACE is set (a stuck run shows where it stopped).
void note(const std::string& text) {
  static const bool trace = GetEnvironmentVariableA("R1GUI_TEST_TRACE", nullptr, 0) != 0;
  if (trace) std::fprintf(stderr, "%s\n", text.c_str());
}

constexpr uint8_t kCtrlAlt = events::Mod::kCtrl | events::Mod::kAlt;

events::Key letter(char c) { return static_cast<events::Key>(c); }

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

  void pump(int steps = 4) {
    for (int i = 0; i < steps; ++i) {
      if (watchdog != nullptr) watchdog->progress();
      app->update();
      rig.settle(1);
      Sleep(16);
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
  std::filesystem::path root;
  std::unique_ptr<ed::EditorApp> app;
};

// Opens the panel, makes it the active one and returns it ready to be floated.
void showPanel(EditorRig& e, dock::PanelId panel) {
  e.app->dock().openPanel(panel);
  e.app->dock().activatePanel(panel);
  e.pump(4);
}

// ---- scenarios -------------------------------------------------------------------------------------

// Float the panel from the main window with the focus on `focus` (invalid: nowhere).
void floatFromMain(EditorRig& e, dock::PanelId panel, WidgetId focus, const std::string& what) {
  note("scenario: " + what);
  showPanel(e, panel);
  if (focus.valid() && e.rig.ui->alive(focus)) e.rig.ui->focusWidget(focus);
  const size_t before = e.rig.backend->windowCount();
  e.press(*e.rig.ui, letter('F'), kCtrlAlt);
  e.pump(6);
  if (!e.floating(panel)) {
    note("  the chord did not float the panel (focus owner takes text: " + what + ")");
  } else {
    check(e.rig.backend->windowCount() == before + 1, what + ": one window more");
    check(e.app->dock().activePanel() == panel, what + ": the floated panel stays active");
    check(e.contextOf(panel) != nullptr && e.contextOf(panel) != e.rig.ui.get(), what + ": the content lives in the new window");
    check(e.app->dock().contentOf(panel).valid(), what + ": the content exists");
  }
}

}  // namespace

int main() {
  std::string skip;
  std::unique_ptr<NativeRig> rig = NativeRig::create(skip);
  if (!rig) {
    std::printf("SKIPPED: %s\n", skip.c_str());
    return 0;
  }
  r1test::HangWatchdog watchdog(std::chrono::seconds(30));
  EditorRig e(*rig);
  e.watchdog = &watchdog;
  ed::EditorApp& app = *e.app;
  const std::vector<dock::PanelId> panels = {ed::panel::kViewport, ed::panel::kOutliner, ed::panel::kInspector, ed::panel::kAssets, ed::panel::kCurves,
                                             ed::panel::kConsole,  ed::panel::kCommands, ed::panel::kQuickActions, ed::panel::kShortcuts};

  // 1. Every panel, the focus on one widget of each kind it contains (and nowhere), the chord pressed in the
  // main window.
  for (dock::PanelId panel : panels) {
    showPanel(e, panel);
    std::vector<std::string> kinds;
    std::vector<WidgetId> targets = {WidgetId{}};
    for (WidgetId w : EditorRig::focusables(*e.rig.ui, app.dock().contentOf(panel))) {
      const std::string kind = e.rig.ui->objectAs<WidgetObject>(w)->typeName();
      if (std::find(kinds.begin(), kinds.end(), kind) != kinds.end()) continue;
      kinds.push_back(kind);
      targets.push_back(w);
    }
    for (size_t i = 0; i < targets.size(); ++i) {
      const std::string what = "panel " + std::to_string(panel) + " focus " + (i == 0 ? std::string("none") : kinds[i - 1]);
      showPanel(e, panel);
      WidgetId target;
      if (i > 0) {
        for (WidgetId w : EditorRig::focusables(*e.rig.ui, app.dock().contentOf(panel))) {
          if (kinds[i - 1] == e.rig.ui->objectAs<WidgetObject>(w)->typeName()) {
            target = w;
            break;
          }
        }
      }
      floatFromMain(e, panel, target, what);
      e.reset();
    }
  }
  // 2. The chord pressed again inside the window the panel already floats in (the key comes from the
  // context that the command destroys), and twice in a row without a loop step between.
  for (dock::PanelId panel : panels) {
    note("scenario: press inside the window of panel " + std::to_string(panel));
    showPanel(e, panel);
    e.press(*e.rig.ui, letter('F'), kCtrlAlt);
    e.pump(6);
    UiContext* inside = e.contextOf(panel);
    check(inside != nullptr && inside != e.rig.ui.get(), "second press scenario: the panel floats");
    if (inside != nullptr && inside != e.rig.ui.get()) {
      const std::vector<WidgetId> inner = EditorRig::focusables(*inside, app.dock().contentOf(panel));
      if (!inner.empty()) inside->focusWidget(inner.front());
      e.press(*inside, letter('F'), kCtrlAlt);
      e.press(*inside, letter('F'), kCtrlAlt);
      e.pump(6);
      check(e.rig.backend->windowCount() == 1, "panel " + std::to_string(panel) + ": still one window after pressing inside it twice");
      check(e.floating(panel), "panel " + std::to_string(panel) + ": still floating");
    }
    e.reset();
  }

  // 3. The other window commands from their chords, in the main window and in a floating one.
  struct Chord {
    const char* name;
    events::Key key;
    uint8_t mods;
  };
  const Chord chords[] = {{"Move tab group", letter('M'), kCtrlAlt},
                          {"Next tab", events::Key::PageDown, events::Mod::kCtrl},
                          {"Previous tab", events::Key::PageUp, events::Mod::kCtrl},
                          {"Close tab", letter('W'), events::Mod::kCtrl}};
  for (const Chord& chord : chords) {
    for (dock::PanelId panel : panels) {
      note(std::string("scenario: ") + chord.name + " on panel " + std::to_string(panel));
      showPanel(e, panel);
      e.press(*e.rig.ui, chord.key, chord.mods);
      e.pump(6);
      if (UiContext* inside = e.contextOf(panel); inside != nullptr && inside != e.rig.ui.get()) {
        e.press(*inside, chord.key, chord.mods);
        e.pump(6);
      }
      e.reset();
    }
  }
  // 4. A fixed pseudo-random mix of everything above: chords pressed in random contexts (main or any floating
  // window) with the focus on a random widget, panels opened, closed and activated, loop steps skipped or
  // repeated, the layout reset. The state is checked as it goes (a window per floating area, every docked
  // panel's content alive in the context that shows it) and the arrangement is reset at the end.
  uint32_t state = 0x9E3779B9u;
  const auto next = [&state](uint32_t bound) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state % bound;
  };
  const struct {
    events::Key key;
    uint8_t mods;
  } fuzzChords[] = {{letter('F'), kCtrlAlt}, {letter('F'), kCtrlAlt}, {letter('M'), kCtrlAlt}, {events::Key::PageDown, events::Mod::kCtrl},
                    {events::Key::PageUp, events::Mod::kCtrl}, {letter('W'), events::Mod::kCtrl}, {events::Key::Tab, 0}, {events::Key::Escape, 0}};
  for (int step = 0; step < 700; ++step) {
    std::vector<UiContext*> contexts = {e.rig.ui.get()};
    for (const dock::Area& area : app.dock().layout().areas()) {
      if (const std::optional<FloatId> window = app.dock().windowOfArea(area.id); window && *window != kMainWindow) {
        if (const std::optional<FloatContent> content = e.rig.backend->content(*window)) contexts.push_back(content->ui);
      }
    }
    UiContext& ctx = *contexts[next(static_cast<uint32_t>(contexts.size()))];
    const dock::PanelId panel = panels[next(static_cast<uint32_t>(panels.size()))];
    const uint32_t op = next(15);
    note("fuzz " + std::to_string(step) + ": op " + std::to_string(op) + " panel " + std::to_string(panel));
    if (op <= 1) {
      const std::vector<WidgetId> targets = EditorRig::focusables(ctx, app.dock().contentOf(panel));
      if (!targets.empty()) ctx.focusWidget(targets[next(static_cast<uint32_t>(targets.size()))]);
    } else if (op <= 6) {
      const auto& chord = fuzzChords[next(static_cast<uint32_t>(std::size(fuzzChords)))];
      e.press(ctx, chord.key, chord.mods);
      if (next(3) == 0) e.press(ctx, chord.key, chord.mods);
    } else if (op == 7) {
      app.dock().openPanel(panel);
    } else if (op == 8) {
      app.dock().closePanel(panel);
    } else if (op == 9) {
      app.dock().activatePanel(panel);
    } else if (op == 10 && next(8) == 0) {
      app.dock().resetLayout();
    } else if (op >= 11) {
      const double x = static_cast<double>(next(800)), y = static_cast<double>(next(500));
      ctx.setTime(ctx.now() + 40 + next(900));  // long enough for hover timers (tooltips) to fire
      ctx.pointerMove(x, y);
      if (op == 14) {
        ctx.pointerDown(x, y, events::Button::Left);
        ctx.pointerUp(x, y, events::Button::Left);
      }
      ctx.tick();
    }
    e.pump(static_cast<int>(next(4)));
    if (step % 10 == 0) {
      e.pump(6);
      size_t floatAreas = 0;
      for (const dock::Area& area : app.dock().layout().areas()) floatAreas += area.id != dock::kMainAreaId ? 1 : 0;
      check(e.rig.backend->windowCount() == floatAreas, "fuzz step " + std::to_string(step) + ": one window per floating area");
      for (dock::PanelId p : panels) {
        UiContext* shown = e.contextOf(p);
        if (shown == nullptr) continue;
        const WidgetId content = app.dock().contentOf(p);
        const bool ok = !content.valid() || shown->alive(content) || e.rig.ui->alive(content);
        if (!ok) {
          const std::optional<dock::PanelSlot> slot = app.dock().layout().locate(p);
          std::fprintf(stderr, "stale content: panel %u content %llu:%u shown-ctx=%s area=%u front=%d\n", p, static_cast<unsigned long long>(content.index),
                       content.generation, shown == e.rig.ui.get() ? "main" : "float", slot ? slot->area : 999u, slot ? slot->front : -1);
        }
        check(ok, "fuzz step " + std::to_string(step) + ": content of panel " + std::to_string(p) + " alive");
      }
    }
  }
  e.reset();
  return r1test::finish();
}





