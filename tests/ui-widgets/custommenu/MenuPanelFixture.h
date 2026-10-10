// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the fixture of the custom menu widget tests: a headless window, a registry with a dozen commands
//   (actions, toggles, a radio group, a disabled one, one whose predicates throw), the usual router and
//   refresh hub, a CustomMenuSet and helpers that find buttons and click them.
// Callers: tests/ui-widgets/custommenu/*_test.cpp (and the GPU tests of the folder).
#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/commands/custommenu/CustomMenuSet.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/custommenu/CustomMenuPanel.h"

namespace r1test {

namespace cmd = r1ui::commands;
namespace cm = r1ui::commands::custommenu;
using r1ui::core::tree::WidgetId;
using r1ui::widgets::CommandServices;
using r1ui::widgets::CustomMenuButton;
using r1ui::widgets::CustomMenuPanel;

struct MenuPanelFixture {
  explicit MenuPanelFixture(int width = 420, int height = 360)
      : t(width, height), clock(t.ui), router(registry, keymap, clock), sync(t.ui, services()) {
    t.ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
    declare("tool.move", "Move", "move-3d", cmd::CommandKind::Radio);
    declare("tool.rotate", "Rotate", "rotate-cw", cmd::CommandKind::Radio);
    declare("edit.undo", "Undo", "undo2", cmd::CommandKind::Action);
    declare("edit.redo", "Redo", "redo2", cmd::CommandKind::Action);
    declare("view.grid", "Show grid", "grid-3x3", cmd::CommandKind::Toggle);
    declare("file.save", "Save", "save", cmd::CommandKind::Action);
    declare("file.open", "Open", "folder-open", cmd::CommandKind::Action);
    declare("edit.copy", "Copy", "copy", cmd::CommandKind::Action);
    // A command whose predicates throw and one that throws when run.
    cmd::CommandDef bad;
    bad.id = "bad.predicates";
    bad.label = "Bad";
    bad.enabled = []() -> bool { throw std::runtime_error("enabled"); };
    bad.checked = []() -> bool { throw std::runtime_error("checked"); };
    bad.execute = [](const cmd::ExecuteArgs&) -> cmd::ExecuteResult { throw std::runtime_error("run"); };
    R1_EXPECT(registry.add(bad).ok);
    enabled["edit.redo"] = false;
  }

  ~MenuPanelFixture() {
    t.ui.overlays().closeAll();
    t.ui.tree().forEachChild(t.ui.root(), [&](WidgetId child) { t.ui.destroy(child); });
  }

  CommandServices services() { return {registry, overrides, keymap, router}; }

  void declare(const std::string& id, const std::string& label, const std::string& icon, cmd::CommandKind kind) {
    cmd::CommandDef def;
    def.id = id;
    def.label = label;
    def.description = label + " description";
    def.icon = icon;
    def.kind = kind;
    if (kind == cmd::CommandKind::Radio) def.radioGroup = "tools";
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

  std::string makePanelMenu(const std::string& name, std::vector<std::string> commands, int columns = 2, int buttonSize = 36) {
    const std::string id = set.createMenu(cm::MenuKind::Panel, name).id;
    for (const std::string& c : commands) R1_EXPECT(set.addEntry(id, c).ok);
    R1_EXPECT(set.setPanelColumns(id, columns).ok && set.setPanelButtonSize(id, buttonSize).ok);
    return id;
  }

  CustomMenuPanel& makePanel(const std::string& menuId) {
    CustomMenuPanel& panel = t.ui.create<CustomMenuPanel>(t.ui.root(), services(), sync, set, menuId);
    panel.style().width = r1ui::core::layout::Length::px(360);
    panel.style().height = r1ui::core::layout::Length::px(300);
    t.layout();
    return panel;
  }

  void clickWidget(WidgetId id) {
    const auto r = t.ui.absRect(id);
    const double x = r.x + r.w / 2.0;
    const double y = r.y + r.h / 2.0;
    t.ui.pointerMove(x, y);
    t.ui.pointerDown(x, y);
    t.ui.pointerUp(x, y);
    t.layout();
  }

  r1test::TestUi t;
  cmd::CommandRegistry registry;
  cmd::KeybindingOverrides overrides{registry};
  cmd::Keymap keymap{registry, overrides};
  r1ui::widgets::UiClock clock;
  cmd::CommandRouter router;
  r1ui::widgets::CommandUiSync sync;
  cm::CustomMenuSet set;
  std::map<std::string, int> runs;
  std::map<std::string, bool> enabled;
  std::map<std::string, bool> checked;
};

}  // namespace r1test
