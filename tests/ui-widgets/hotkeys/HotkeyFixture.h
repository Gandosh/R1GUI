// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the fixture of the hotkey editor tests: a headless window with a registry, overrides, keymap and
//   router, a command set in four categories and two contexts (global and a layers panel whose Delete
//   clashes with the global Delete), the editor mounted in a 1250 x 760 window, and the pointer and key
//   helpers the tests share.
// Why: the editor, keyboard and hostile tests start from the same state; keeping it here keeps each test
//   a page of expectations.
// Callers: tests/ui-widgets/hotkeys/*_test.cpp.
#pragma once

#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/commands/CommandRouter.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/hotkeys/HotkeyEditor.h"

namespace r1test {

namespace cmd = r1ui::commands;
using r1ui::core::events::Key;
using r1ui::core::layout::RectD;
using r1ui::core::tree::WidgetId;
using r1ui::widgets::HotkeyEditor;
namespace Mod = r1ui::core::events::Mod;

inline Key letter(char c) { return static_cast<Key>(c); }
inline cmd::ChordSequence chordOf(Key key, uint8_t mods = 0) { return cmd::ChordSequence::single({key, mods, false}); }

struct HotkeyScene {
  explicit HotkeyScene(bool withCommands = true, int width = 1300, int height = 800) : t(width, height), clock(t.ui), router(registry, keymap, clock) {
    registry.addContext("layers", cmd::kWindowContext);
    if (withCommands) declareAll();
    editor = &t.ui.create<HotkeyEditor>(t.ui.root(), services());
    editor->style().width = r1ui::core::layout::Length::px(1250);
    editor->style().height = r1ui::core::layout::Length::px(740);
    editor->style().flexShrink = 0.0;
    t.layout();
  }
  // Widgets built from the services go first: the registry is destroyed before the context.
  ~HotkeyScene() {
    t.ui.overlays().closeAll();
    t.ui.tree().forEachChild(t.ui.root(), [&](WidgetId child) { t.ui.destroy(child); });
  }

  r1ui::widgets::CommandServices services() { return {registry, overrides, keymap, router}; }

  bool declare(const std::string& id, const std::string& label, const std::string& category, cmd::ChordSequence primary = {}, cmd::ChordSequence alternate = {},
               const std::string& context = cmd::kGlobalContext, const std::string& description = {}) {
    cmd::CommandDef def;
    def.id = id;
    def.label = label;
    def.description = description.empty() ? label + " does what it says" : description;
    def.category = category;
    def.context = context;
    def.defaultChords = {primary, alternate};
    def.execute = [](const cmd::ExecuteArgs&) { return cmd::ExecuteResult::handled(); };
    return registry.add(std::move(def)).ok;
  }

  void declareAll() {
    declare("edit.copy", "Copy", "Edit", chordOf(letter('C'), Mod::kCtrl));
    declare("edit.paste", "Paste", "Edit", chordOf(letter('V'), Mod::kCtrl));
    declare("edit.undo", "Undo", "Edit", chordOf(letter('Z'), Mod::kCtrl));
    declare("edit.redo", "Redo", "Edit", chordOf(letter('Z'), Mod::kCtrl | Mod::kShift));
    declare("global.delete", "Delete", "Edit", chordOf(Key::Delete), chordOf(Key::Backspace));
    declare("edit.comment", "Comment line", "Edit", cmd::ChordSequence::pair({letter('K'), Mod::kCtrl, false}, {letter('C'), Mod::kCtrl, false}));
    declare("file.save", "Save", "File", chordOf(letter('S'), Mod::kCtrl));
    declare("file.open", "Open", "File", chordOf(letter('O'), Mod::kCtrl));
    declare("view.fit", "Fit to window", "View", chordOf(letter('F')));
    declare("free.one", "Free one", "Misc");
    declare("free.two", "Free two", "Misc");
    declare("layers.delete", "Delete layer", "Layers", chordOf(Key::Delete), {}, "layers");
    declare("layers.hide", "Hide layer", "Layers", {}, {}, "layers");
  }

  // Window rectangle of the cap with this id.
  RectD cap(const char* id) {
    const auto& caps = editor->keyboard().layout().caps();
    for (size_t i = 0; i < caps.size(); ++i) {
      if (caps[i].id == id) return editor->keyboard().capRect(i);
    }
    return {};
  }
  void clickAt(double x, double y) {
    t.ui.pointerMove(x, y);
    t.ui.pointerDown(x, y);
    t.ui.pointerUp(x, y);
    t.layout();
  }
  void clickCap(const char* id) {
    const RectD r = cap(id);
    clickAt(r.x + r.w / 2, r.y + r.h / 2);
  }
  void click(WidgetId id) {
    const auto r = t.ui.absRect(id);
    clickAt(r.x + r.w / 2, r.y + r.h / 2);
  }
  bool key(Key k, uint8_t mods = 0) {
    const bool used = t.ui.keyDown(k, mods);
    t.layout();
    return used;
  }
  void paintOnce() {
    r1ui::render::Painter painter;
    painter.begin(1300, 800);
    t.ui.paint(painter);
    painter.end();
  }
  std::optional<cmd::ChordSequence> effective(const char* id, int slot) { return keymap.effective(id, slot); }

  TestUi t;
  cmd::CommandRegistry registry;
  cmd::KeybindingOverrides overrides{registry};
  cmd::Keymap keymap{registry, overrides};
  r1ui::widgets::UiClock clock;
  cmd::CommandRouter router;
  HotkeyEditor* editor = nullptr;
};

}  // namespace r1test
