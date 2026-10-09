// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the fixture of the customize widget tests: a headless window plus a registry, overrides, keymap,
//   router, refresh hub, a Customization over built-in layouts (a menu bar with a locked Help menu, a
//   toolbar with a flyout group, a locked toolbar, a free-form panel), a memory store and the controller;
//   and helpers that drive the UiContext with synthetic pointer input (click, double click, drag with
//   intermediate moves, right click) and find widgets.
// Why: every test of the edit displays, the palette, the free-form panel and the bound bars needs the same
//   dozen objects wired the same way; this keeps each test a page of expectations.
// Callers: tests/ui-widgets/customize/*_test.cpp (and the gpu tests of the folder).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "NoDialogs.h"
#include "TestSupport.h"
#include "r1ui/commands/customize/CustomizationIo.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/customize/CustomizeController.h"
#include "r1ui/widgets/customize/LayoutConvert.h"

namespace r1test {

namespace cmd = r1ui::commands;
namespace cz = r1ui::commands::customize;
using r1ui::core::events::Key;
namespace Mod = r1ui::core::events::Mod;
using r1ui::core::tree::WidgetId;
using r1ui::widgets::CommandMenuEntry;
using r1ui::widgets::CommandServices;
using r1ui::widgets::CommandToolbarItem;

inline Key letter(char c) { return static_cast<Key>(c); }
inline cmd::ChordSequence chordOf(Key key, uint8_t mods = 0) { return cmd::ChordSequence::single({key, mods, false}); }

inline cz::LayoutSet fixtureLayouts() {
  using E = CommandMenuEntry;
  using I = CommandToolbarItem;
  cz::LayoutSet set;
  set.menuBar.menus.push_back(r1ui::widgets::menuNodeFromEntries("menu.file", "File", {E::command("file.open"), E::command("file.save")}));
  set.menuBar.menus.push_back(r1ui::widgets::menuNodeFromEntries(
      "menu.edit", "Edit", {E::command("edit.undo"), E::command("edit.redo"), E::separator(), E::command("edit.cut"), E::command("edit.copy"), E::command("edit.paste")}));
  set.menuBar.menus.push_back(r1ui::widgets::menuNodeFromEntries("menu.view", "View", {E::command("view.grid"), E::submenu("Zoom", {E::command("view.zoomIn")})}));
  cz::Node help = r1ui::widgets::menuNodeFromEntries("menu.help", "Help", {E::command("help.about")});
  help.locked = true;
  set.menuBar.menus.push_back(std::move(help));
  set.toolbars.push_back(r1ui::widgets::toolbarFromItems(
      "tb.main", "Tools", {I::command("tool.select"), I::command("tool.pen"), I::separator(), I::command("edit.undo"), I::group({"tool.rect", "tool.ellipse"})}));
  cz::ToolbarLayout locked = r1ui::widgets::toolbarFromItems("tb.locked", "Locked bar", {I::command("file.open")});
  locked.locked = true;
  set.toolbars.push_back(std::move(locked));
  cz::FreeFormPanelLayout panel;
  panel.id = "fp.main";
  panel.title = "Quick";
  panel.width = 320;
  panel.height = 200;
  panel.buttons = {cz::Node::freeButton("fp.save", "file.save", {16, 16, 96, 32}), cz::Node::freeButton("fp.undo", "edit.undo", {128, 16, 96, 32})};
  set.panels.push_back(std::move(panel));
  return set;
}

struct CustomizeFixture {
  explicit CustomizeFixture(int width = 900, int height = 700)
      : t(width, height), clock(t.ui), router(registry, keymap, clock), sync(t.ui, services()), model(fixtureLayouts()), storage(model, store), controller(t.ui, services(), sync, model) {
    t.ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
    struct Spec {
      const char* id;
      const char* label;
      const char* icon;
      const char* category;
      cmd::CommandKind kind;
      cmd::ChordSequence chord;
    };
    const Spec specs[] = {
        {"file.open", "Open...", "folder-open", "File", cmd::CommandKind::Action, chordOf(letter('O'), Mod::kCtrl)},
        {"file.save", "Save", "save", "File", cmd::CommandKind::Action, chordOf(letter('S'), Mod::kCtrl)},
        {"edit.undo", "Undo", "undo2", "Edit", cmd::CommandKind::Action, chordOf(letter('Z'), Mod::kCtrl)},
        {"edit.redo", "Redo", "redo2", "Edit", cmd::CommandKind::Action, chordOf(letter('Y'), Mod::kCtrl)},
        {"edit.cut", "Cut", "scissors", "Edit", cmd::CommandKind::Action, chordOf(letter('X'), Mod::kCtrl)},
        {"edit.copy", "Copy", "copy", "Edit", cmd::CommandKind::Action, chordOf(letter('C'), Mod::kCtrl)},
        {"edit.paste", "Paste", "clipboard", "Edit", cmd::CommandKind::Action, chordOf(letter('V'), Mod::kCtrl)},
        {"view.grid", "Show grid", "grid-3x3", "View", cmd::CommandKind::Toggle, chordOf(letter('G'), Mod::kCtrl)},
        {"view.zoomIn", "Zoom in", "zoom-in", "View", cmd::CommandKind::Action, {}},
        {"tool.select", "Select", "mouse-pointer", "Tools", cmd::CommandKind::Radio, chordOf(letter('V'))},
        {"tool.pen", "Pen", "pen-tool", "Tools", cmd::CommandKind::Radio, chordOf(letter('P'))},
        {"tool.hand", "Hand", "hand", "Tools", cmd::CommandKind::Radio, chordOf(letter('H'))},
        {"tool.rect", "Rectangle", "square", "Tools", cmd::CommandKind::Radio, {}},
        {"tool.ellipse", "Ellipse", "circle", "Tools", cmd::CommandKind::Radio, {}},
        {"help.about", "About", "circle-alert", "Help", cmd::CommandKind::Action, {}},
    };
    for (const Spec& s : specs) declare(s.id, s.label, s.icon, s.category, s.kind, s.chord);
  }

  // The widgets built from the controller go first (the controller must outlive them).
  ~CustomizeFixture() {
    t.ui.overlays().closeAll();
    t.ui.tree().forEachChild(t.ui.root(), [&](WidgetId child) { t.ui.destroy(child); });
  }

  CommandServices services() { return {registry, overrides, keymap, router}; }

  void declare(const std::string& id, const std::string& label, const std::string& icon, const std::string& category, cmd::CommandKind kind, cmd::ChordSequence chord) {
    cmd::CommandDef def;
    def.id = id;
    def.label = label;
    def.description = label + " description";
    def.icon = icon;
    def.category = category;
    def.kind = kind;
    if (kind == cmd::CommandKind::Radio) def.radioGroup = "tools";
    def.defaultChords = {chord, {}};
    enabled[id] = true;
    def.enabled = [this, id] { return enabled[id]; };
    def.checked = [this, id] { return checked[id]; };
    def.execute = [this, id, kind](const cmd::ExecuteArgs&) {
      ++runs[id];
      if (kind == cmd::CommandKind::Toggle) checked[id] = !checked[id];
      if (kind == cmd::CommandKind::Radio) {
        for (auto& entry : checked) {
          if (entry.first.rfind("tool.", 0) == 0) entry.second = entry.first == id;
        }
      }
      registry.touch();
      return cmd::ExecuteResult::handled();
    };
    R1_EXPECT(registry.add(def).ok);
  }

  // ---- synthetic input ------------------------------------------------------------------------
  void click(double x, double y) {
    t.ui.pointerMove(x, y);
    t.ui.pointerDown(x, y);
    t.ui.pointerUp(x, y);
    t.layout();
  }
  void clickWidget(WidgetId id) {
    const auto r = t.ui.absRect(id);
    click(r.x + r.w / 2.0, r.y + r.h / 2.0);
  }
  void doubleClick(double x, double y) {
    t.ui.setTime(1000);
    t.ui.pointerMove(x, y);
    t.ui.pointerDown(x, y);
    t.ui.pointerUp(x, y);
    t.ui.setTime(1100);
    t.ui.pointerDown(x, y);
    t.ui.pointerUp(x, y);
    t.layout();
  }
  void rightClick(double x, double y) {
    t.ui.pointerMove(x, y);
    t.ui.pointerDown(x, y, r1ui::core::events::Button::Right);
    t.ui.pointerUp(x, y, r1ui::core::events::Button::Right);
    t.layout();
  }
  // Presses at `from`, moves in steps to `to` (calling `during` at the last position before the release)
  // and releases there.
  template <class Fn>
  void drag(double fx, double fy, double tx, double ty, Fn during, bool release = true) {
    t.ui.pointerMove(fx, fy);
    t.ui.pointerDown(fx, fy);
    const int steps = 6;
    for (int i = 1; i <= steps; ++i) t.ui.pointerMove(fx + (tx - fx) * i / steps, fy + (ty - fy) * i / steps);
    t.layout();
    during();
    if (release) t.ui.pointerUp(tx, ty);
    t.layout();
  }
  void drag(double fx, double fy, double tx, double ty) {
    drag(fx, fy, tx, ty, [] {});
  }
  void type(const std::string& ascii) {
    for (const char c : ascii) t.ui.textInput(static_cast<char32_t>(static_cast<unsigned char>(c)));
  }
  void enterEditMode() {
    router.execute(r1ui::widgets::kCmdCustomizeToggle, cmd::ExecuteSource::Api);
    t.layout();
  }

  template <class T>
  T* find(WidgetId root) {
    T* found = nullptr;
    t.ui.tree().forEachDescendant(root, [&](WidgetId id) {
      if (found == nullptr) found = t.ui.objectAs<T>(id);
    });
    return found;
  }
  template <class T>
  std::vector<T*> findAll(WidgetId root) {
    std::vector<T*> out;
    t.ui.tree().forEachDescendant(root, [&](WidgetId id) {
      if (T* p = t.ui.objectAs<T>(id)) out.push_back(p);
    });
    return out;
  }

  r1test::TestUi t;
  cmd::CommandRegistry registry;
  cmd::KeybindingOverrides overrides{registry};
  cmd::Keymap keymap{registry, overrides};
  r1ui::widgets::UiClock clock;
  cmd::CommandRouter router;
  r1ui::widgets::CommandUiSync sync;
  cz::Customization model;
  cz::MemoryTextStore store;
  cz::CustomizationStorage storage;
  r1ui::widgets::CustomizeController controller;
  std::map<std::string, int> runs;
  std::map<std::string, bool> enabled;
  std::map<std::string, bool> checked;
};

}  // namespace r1test
