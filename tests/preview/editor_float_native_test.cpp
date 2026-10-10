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
//   for every panel type of the preview. The loop waits 16 ms per step like the application's; with
//   R1GUI_TEST_PACE=-1 it never waits. That used to stall the display driver's present call for good (a
//   bug of the renderer, fixed: docs/dev/native-windows.md section 12), so both variants must pass;
//   preview-editor-float-stress-test covers the unpaced loop in depth.
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

#include "EditorNativeRig.h"

using namespace editor_test;

namespace {

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
  char pace[16] = {};
  if (GetEnvironmentVariableA("R1GUI_TEST_PACE", pace, sizeof(pace)) != 0) e.pace = std::atoi(pace);  // -1: never wait between steps
  ed::EditorApp& app = *e.app;
  const std::vector<dock::PanelId> panels = {ed::panel::kViewport, ed::panel::kOutliner, ed::panel::kInspector, ed::panel::kAssets, ed::panel::kCurves,
                                             ed::panel::kConsole,  ed::panel::kCommands, ed::panel::kShortcuts};

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
  // 3b. The brush library in a native floating window: B pressed in that window's own context opens the library
  // there (bounded by that window), one letter picks Inflate (the only brush starting with I), a window that
  // is destroyed while the library is open takes it along, and the main window opens it afterwards.
  {
    note("scenario: brush library in a floating window");
    auto& library = app.brushLibrary();
    showPanel(e, ed::panel::kOutliner);
    e.press(*e.rig.ui, letter('F'), kCtrlAlt);
    e.pump(6);
    UiContext* inside = e.contextOf(ed::panel::kOutliner);
    check(inside != nullptr && inside != e.rig.ui.get(), "brush library: the outliner floats");
    if (inside != nullptr && inside != e.rig.ui.get()) {
      e.press(*inside, letter('B'), 0);
      e.pump(4);
      check(library.isOpen() && library.window() == inside, "brush library: B opens it in the floating window");
      if (library.window() == inside && library.popup() != nullptr) {
        const auto popup = inside->absRect(library.popup()->id());
        check(popup.x >= 0 && popup.y >= 0 && popup.w > 0 && popup.h > 0, "brush library: the popup has a place in the floating window");
      }
      inside->setTime(inside->now() + 50);
      inside->keyDown(letter('I'), 0);
      inside->textInput(U'i', 0);
      inside->keyUp(letter('I'), 0);
      e.pump(4);
      check(!library.isOpen() && app.model().brushName == "Inflate", "brush library: one letter picked Inflate");
      e.press(*inside, letter('B'), 0);
      e.pump(4);
      check(library.isOpen(), "brush library: open again before the window goes");
      e.reset();  // the window is destroyed while the library is open in it
      check(!library.isOpen(), "brush library: it went with its window");
    }
    e.press(*e.rig.ui, letter('B'), 0);
    e.pump(4);
    check(library.isOpen() && library.window() == e.rig.ui.get(), "brush library: the main window opens it");
    e.press(*e.rig.ui, events::Key::Escape, 0);
    e.pump(4);
    check(!library.isOpen(), "brush library: Escape closes it");
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





