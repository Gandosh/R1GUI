// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the fixture of the command widget tests: a headless window plus a registry, overrides, keymap,
//   router and refresh hub, and small helpers to declare commands whose state the test controls.
// Why: every test of the menu builder, the toolbar binder, the key handler and the keybinding editor
//   needs the same eight objects wired the same way; this keeps each test a page of expectations.
// Callers: tests/ui-widgets/commands/*_test.cpp (and the gpu / visual tests of the folder).
#pragma once

#include <map>
#include <string>

#include "TestSupport.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/commands/CommandUiSync.h"

namespace r1test {

namespace cmd = r1ui::commands;
using r1ui::core::events::Key;
namespace Mod = r1ui::core::events::Mod;
using r1ui::widgets::CommandServices;


inline Key letter(char c) { return static_cast<Key>(c); }
inline cmd::ChordSequence chordOf(Key key, uint8_t mods = 0) { return cmd::ChordSequence::single({key, mods, false}); }
inline cmd::ChordSequence pairOf(Key k1, uint8_t m1, Key k2, uint8_t m2 = 0) { return cmd::ChordSequence::pair({k1, m1, false}, {k2, m2, false}); }

struct CommandFixture {
  explicit CommandFixture(int width = 600, int height = 500)
      : t(width, height), clock(t.ui), router(registry, keymap, clock), sync(t.ui, services()) {}

  // The widgets built from the services go first: the context detaches them when it dies, and they talk to
  // the registry then (the registry is destroyed before the context).
  ~CommandFixture() {
    t.ui.overlays().closeAll();
    t.ui.tree().forEachChild(t.ui.root(), [&](r1ui::core::tree::WidgetId child) { t.ui.destroy(child); });
  }

  CommandServices services() { return {registry, overrides, keymap, router}; }

  // Declares a command; runs[id] counts executions, enabled[id] and checked[id] steer the predicates.
  void declare(const std::string& id, const std::string& label, cmd::CommandKind kind = cmd::CommandKind::Action, cmd::ChordSequence primary = {},
                           cmd::ChordSequence alternate = {}, const std::string& icon = {}, const std::string& context = cmd::kGlobalContext, const std::string& category = {}) {
    cmd::CommandDef def;
    def.id = id;
    def.label = label;
    def.description = label + " description";
    def.icon = icon;
    def.kind = kind;
    def.context = context;
    def.category = category;
    if (kind == cmd::CommandKind::Radio) def.radioGroup = "group";
    def.defaultChords = {primary, alternate};
    enabled[id] = true;
    checked[id] = false;
    def.enabled = [this, id] { return enabled[id]; };
    def.checked = [this, id] { return checked[id]; };
    def.execute = [this, id, kind](const cmd::ExecuteArgs&) {
      ++runs[id];
      if (kind == cmd::CommandKind::Toggle) checked[id] = !checked[id];
      if (kind == cmd::CommandKind::Radio) {
        for (auto& entry : checked) {
          if (entry.first.rfind("radio.", 0) == 0) entry.second = entry.first == id;
        }
      }
      return cmd::ExecuteResult::handled();
    };
    const cmd::RegisterResult result = registry.add(def);
    R1_EXPECT(result.ok);
  }

  r1test::TestUi t;
  cmd::CommandRegistry registry;
  cmd::KeybindingOverrides overrides{registry};
  cmd::Keymap keymap{registry, overrides};
  r1ui::widgets::UiClock clock;
  cmd::CommandRouter router;
  r1ui::widgets::CommandUiSync sync;
  std::map<std::string, int> runs;
  std::map<std::string, bool> enabled;
  std::map<std::string, bool> checked;
};

}  // namespace r1test
