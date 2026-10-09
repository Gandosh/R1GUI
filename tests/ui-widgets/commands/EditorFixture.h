// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the fixture of the keybinding editor tests: a command set in three categories and two
//   contexts (global and a layers panel, whose Delete clashes with the global Delete), the editor
//   mounted in a 1000 x 800 window, and the pointer and key helpers the tests share.
// Why: both editor test files (structure and capture, flows and hostile cases) start from the same
//   state; keeping it here keeps each test a page of expectations.
// Callers: keybinding_editor_test.cpp, keybinding_flow_test.cpp.
#pragma once

#include "CommandFixture.h"
#include "r1ui/widgets/commands/KeybindingEditor.h"

namespace r1test {

using r1ui::widgets::ChordBox;
using r1ui::widgets::KeybindingEditor;
using r1ui::core::tree::WidgetId;

inline constexpr Key kF2 = static_cast<Key>(113);

struct EditorFixture : CommandFixture {
  EditorFixture() : CommandFixture(1000, 800) {
    registry.addContext("layers", cmd::kWindowContext);
    using K = cmd::CommandKind;
    declare("edit.copy", "Copy", K::Action, chordOf(letter('C'), Mod::kCtrl), {}, {}, cmd::kGlobalContext, "Edit");
    declare("edit.paste", "Paste", K::Action, chordOf(letter('V'), Mod::kCtrl), {}, {}, cmd::kGlobalContext, "Edit");
    declare("edit.undo", "Undo", K::Action, chordOf(letter('Z'), Mod::kCtrl), {}, {}, cmd::kGlobalContext, "Edit");
    declare("edit.rename", "Rename", K::Action, chordOf(kF2), {}, {}, cmd::kGlobalContext, "Edit");
    declare("global.delete", "Delete", K::Action, chordOf(Key::Delete), chordOf(Key::Backspace), {}, cmd::kGlobalContext, "Edit");
    declare("file.save", "Save", K::Action, chordOf(letter('S'), Mod::kCtrl), {}, {}, cmd::kGlobalContext, "File");
    declare("layers.delete", "Delete layer", K::Action, chordOf(Key::Delete), {}, {}, "layers", "Layers");
    cmd::CommandDef hidden;
    hidden.id = "hidden.cmd";
    hidden.label = "Hidden";
    hidden.hiddenFromEditor = true;
    registry.add(hidden);
    editor = &t.ui.create<KeybindingEditor>(t.ui.root(), services());
    editor->style().height = r1ui::core::layout::Length::px(700);
    t.layout();
  }

  ChordBox& box(const char* id, int slot) { return *editor->box(id, slot); }
  void click(WidgetId id) {
    const auto r = t.ui.absRect(id);
    t.ui.pointerMove(r.x + r.w / 2, r.y + r.h / 2);
    t.ui.pointerDown(r.x + r.w / 2, r.y + r.h / 2);
    t.ui.pointerUp(r.x + r.w / 2, r.y + r.h / 2);
    t.layout();
  }
  // Clicks the left part of a box (the right part holds the remove button).
  void clickBox(const char* id, int slot) {
    const auto r = t.ui.absRect(editor->boxOf(id, slot));
    t.ui.pointerMove(r.x + 20, r.y + r.h / 2);
    t.ui.pointerDown(r.x + 20, r.y + r.h / 2);
    t.ui.pointerUp(r.x + 20, r.y + r.h / 2);
    t.layout();
  }
  bool key(Key k, uint8_t mods = 0) {
    const bool used = t.ui.keyDown(k, mods);
    t.layout();
    return used;
  }
  bool hidden(WidgetId id) { return t.ui.object(id)->style().display == r1ui::core::layout::Display::None; }
  KeybindingEditor* editor = nullptr;
};

}  // namespace r1test
