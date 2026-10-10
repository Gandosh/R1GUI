// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tests of CustomMenuCommands and the "Custom Menus" main menu on the widget side: commands
//   registered for new menus (dockable menus get an open command, every menu edit / save / delete), the
//   global create and load commands, relabel on rename, removal on delete, no clash with foreign
//   commands, hooks (called with the menu id, missing hooks refuse, throwing hooks are contained, the
//   default delete), the destructor cleaning the registry, the entries converted for the menu builders
//   (built into real menu rows, with the layout node), and 300 menus.
// Callers: CTest (label fast).
#include <algorithm>

#include "MenuPanelFixture.h"
#include "r1ui/widgets/commands/CommandMenus.h"
#include "r1ui/widgets/custommenu/CustomMenuCommands.h"
#include "r1ui/widgets/custommenu/CustomMenusMenu.h"

namespace {

using namespace r1test;
using r1ui::widgets::CustomMenuCommands;
using r1ui::widgets::CustomMenuHooks;

struct HookLog {
  std::vector<std::string> calls;
  CustomMenuHooks hooks() {
    CustomMenuHooks h;
    h.openPanel = [this](const std::string& id) { calls.push_back("open " + id); };
    h.edit = [this](const std::string& id) { calls.push_back("edit " + id); };
    h.save = [this](const std::string& id) { calls.push_back("save " + id); };
    h.create = [this] { calls.push_back("create"); };
    h.load = [this] { calls.push_back("load"); };
    return h;
  }
};

void testRegistration() {
  MenuPanelFixture f;
  HookLog log;
  const size_t base = f.registry.size();
  {
    CustomMenuCommands commands(f.registry, f.set, log.hooks());
    R1_EXPECT(f.registry.find(cm::kCommandCreate) && f.registry.find(cm::kCommandLoad));
    R1_EXPECT(commands.registeredCount() == 2 && f.registry.size() == base + 2);
    const std::string panel = f.set.createMenu(cm::MenuKind::Panel, "Quick").id;
    const std::string pie = f.set.createMenu(cm::MenuKind::Pie, "Radial").id;
    // A dockable menu has open, edit, save and delete; a pie has no open command.
    const auto* open = f.registry.find(cm::commandIdFor(cm::MenuAction::Open, panel));
    R1_EXPECT(open != nullptr && open->label == "Quick" && open->category == "Custom Menus" && !open->hiddenFromEditor);
    R1_EXPECT(f.registry.find(cm::commandIdFor(cm::MenuAction::Edit, panel)) && f.registry.find(cm::commandIdFor(cm::MenuAction::Delete, panel)));
    R1_EXPECT(f.registry.find(cm::commandIdFor(cm::MenuAction::Edit, panel))->hiddenFromEditor);
    R1_EXPECT(!f.registry.find(cm::commandIdFor(cm::MenuAction::Open, pie)) && f.registry.find(cm::commandIdFor(cm::MenuAction::Save, pie)));
    R1_EXPECT(commands.registeredCount() == 2 + 4 + 3);

    // Hooks receive the menu id.
    R1_EXPECT(f.router.execute(cm::commandIdFor(cm::MenuAction::Open, panel), cmd::ExecuteSource::Menu).isHandled());
    R1_EXPECT(f.router.execute(cm::commandIdFor(cm::MenuAction::Edit, pie), cmd::ExecuteSource::Menu).isHandled());
    R1_EXPECT(f.router.execute(cm::commandIdFor(cm::MenuAction::Save, pie), cmd::ExecuteSource::Menu).isHandled());
    R1_EXPECT(f.router.execute(cm::kCommandCreate, cmd::ExecuteSource::Menu).isHandled() && f.router.execute(cm::kCommandLoad, cmd::ExecuteSource::Menu).isHandled());
    R1_EXPECT(log.calls == (std::vector<std::string>{"open " + panel, "edit " + pie, "save " + pie, "create", "load"}));

    // Rename relabels the open command; a changed name is a new registration.
    f.set.renameMenu(panel, "Fast tools");
    R1_EXPECT(f.registry.find(cm::commandIdFor(cm::MenuAction::Open, panel))->label == "Fast tools");
    R1_EXPECT(commands.registeredCount() == 9);
    // Entry edits do not touch the registry.
    const uint64_t version = f.registry.version();
    f.set.addEntry(panel, "edit.undo");
    R1_EXPECT(f.registry.version() == version);
    // Delete removes everything of that menu.
    f.set.deleteMenu(panel);
    R1_EXPECT(!f.registry.find(cm::commandIdFor(cm::MenuAction::Open, panel)) && !f.registry.find(cm::commandIdFor(cm::MenuAction::Edit, panel)));
    R1_EXPECT(commands.registeredCount() == 2 + 3);
    R1_EXPECT(!f.router.execute(cm::commandIdFor(cm::MenuAction::Edit, panel), cmd::ExecuteSource::Menu).isHandled());
  }
  // The binder removes its commands when it goes away, and only its own.
  R1_EXPECT(f.registry.size() == base);
}

void testHooksAndRefusals() {
  MenuPanelFixture f;
  CustomMenuCommands commands(f.registry, f.set, {});
  const std::string id = f.set.createMenu(cm::MenuKind::Panel, "Quick").id;
  // No hooks: refusals with a reason, except delete which acts.
  const auto openResult = f.router.execute(cm::commandIdFor(cm::MenuAction::Open, id), cmd::ExecuteSource::Menu);
  R1_EXPECT(!openResult.isHandled() && !openResult.reason.empty());
  R1_EXPECT(!f.router.execute(cm::kCommandCreate, cmd::ExecuteSource::Menu).isHandled());
  // A throwing hook is contained.
  CustomMenuHooks hooks;
  hooks.edit = [](const std::string&) { throw std::runtime_error("dialog failed"); };
  commands.setHooks(hooks);
  const auto thrown = f.router.execute(cm::commandIdFor(cm::MenuAction::Edit, id), cmd::ExecuteSource::Menu);
  R1_EXPECT(!thrown.isHandled() && thrown.reason.find("dialog failed") != std::string::npos);
  // Delete: the default deletes at once, a hook decides otherwise.
  std::vector<std::string> asked;
  hooks.remove = [&](const std::string& menuId) { asked.push_back(menuId); };
  commands.setHooks(hooks);
  R1_EXPECT(f.router.execute(cm::commandIdFor(cm::MenuAction::Delete, id), cmd::ExecuteSource::Menu).isHandled());
  R1_EXPECT(asked.size() == 1 && asked[0] == id && f.set.find(id) != nullptr);
  commands.setHooks({});
  R1_EXPECT(f.router.execute(cm::commandIdFor(cm::MenuAction::Delete, id), cmd::ExecuteSource::Menu).isHandled());
  R1_EXPECT(f.set.find(id) == nullptr && !f.registry.find(cm::commandIdFor(cm::MenuAction::Delete, id)));
}

void testClashesAndLimits() {
  MenuPanelFixture f;
  // A foreign command with the id of the global create command: the binder neither replaces nor removes it.
  cmd::CommandDef foreign;
  foreign.id = cm::kCommandCreate;
  foreign.label = "Foreign";
  foreign.execute = [](const cmd::ExecuteArgs&) { return cmd::ExecuteResult::handled(); };
  R1_EXPECT(f.registry.add(foreign).ok);
  {
    CustomMenuCommands commands(f.registry, f.set, {});
    R1_EXPECT(commands.registrationFailures() == 1 && f.registry.find(cm::kCommandCreate)->label == "Foreign");
  }
  R1_EXPECT(f.registry.find(cm::kCommandCreate) != nullptr && f.registry.find(cm::kCommandCreate)->label == "Foreign");

  // 300 menus: every one gets its commands; deleting them all empties the registry of them.
  MenuPanelFixture g;
  CustomMenuCommands many(g.registry, g.set, {});
  const size_t base = g.registry.size();
  for (int i = 0; i < 300; ++i) g.set.createMenu(i % 2 == 0 ? cm::MenuKind::Panel : cm::MenuKind::Pie, "Menu " + std::to_string(i));
  R1_EXPECT(many.registeredCount() == 2 + 150 * 4 + 150 * 3);
  R1_EXPECT(g.registry.size() == base + (many.registeredCount() - 2));
  while (!g.set.menus().empty()) g.set.deleteMenu(g.set.menus().front().id);
  R1_EXPECT(many.registeredCount() == 2);
}

void testMenuEntriesAndNode() {
  MenuPanelFixture f;
  CustomMenuCommands commands(f.registry, f.set, {});
  const std::string panel = f.set.createMenu(cm::MenuKind::Panel, "Quick").id;
  f.set.createMenu(cm::MenuKind::Pie, "Radial");
  const auto entries = r1ui::widgets::customMenusMenuEntries(f.set);
  const auto desc = cm::describeCustomMenusMenu(f.set);
  R1_EXPECT(entries.size() == desc.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    R1_EXPECT(static_cast<int>(entries[i].kind) == static_cast<int>(desc[i].kind));
    R1_EXPECT(entries[i].commandId == desc[i].commandId && entries[i].text == desc[i].text && entries[i].children.size() == desc[i].children.size());
  }
  // The builders turn it into rows: the dockable menu's entry shows its name, the pie has a submenu.
  const r1ui::widgets::MenuSpec spec = r1ui::widgets::buildCommandMenu(f.services(), entries);
  std::vector<std::string> labels;
  std::vector<std::string> submenus;
  for (const auto& item : spec.items) {
    if (item.kind == r1ui::widgets::MenuItemKind::Submenu) submenus.push_back(item.label);
    if (item.kind == r1ui::widgets::MenuItemKind::Action) labels.push_back(item.label);
  }
  R1_EXPECT(std::find(labels.begin(), labels.end(), "Quick") != labels.end());
  R1_EXPECT(std::find(labels.begin(), labels.end(), "Create Custom Menu...") != labels.end() && std::find(labels.begin(), labels.end(), "Load Custom Menu...") != labels.end());
  R1_EXPECT(std::find(submenus.begin(), submenus.end(), "Radial") != submenus.end() && std::find(submenus.begin(), submenus.end(), "Edit Dockable Menu") != submenus.end());
  const auto title = r1ui::widgets::customMenusMenuTitle(f.set);
  R1_EXPECT(title.title == "Custom Menus" && title.entries.size() == entries.size());
  const auto node = r1ui::widgets::customMenusMenuNode(f.set);
  R1_EXPECT(node.id == "menu.custom" && node.label == "Custom Menus" && !node.children.empty());
  // Without a binder the commands are unknown and the builders drop the rows instead of failing.
  MenuPanelFixture bare;
  bare.set.createMenu(cm::MenuKind::Panel, "Orphan");
  const r1ui::widgets::MenuSpec bareSpec = r1ui::widgets::buildCommandMenu(bare.services(), r1ui::widgets::customMenusMenuEntries(bare.set));
  size_t bareActions = 0;
  for (const auto& item : bareSpec.items) bareActions += item.kind == r1ui::widgets::MenuItemKind::Action ? 1 : 0;
  R1_EXPECT(bareActions == 0);
  (void)panel;
}

}  // namespace

int main() {
  testRegistration();
  testHooksAndRefusals();
  testClashesAndLimits();
  testMenuEntriesAndNode();
  return r1test::finish();
}
