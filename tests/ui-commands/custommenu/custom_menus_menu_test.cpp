// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tests of the "Custom Menus" main menu description and its command ids: the shape for no menus,
//   for dockable and pie menus, the order, the round trip of the command ids, hostile ids and names.
// Callers: CTest (label fast).
#include "MenuFixtures.h"
#include "r1ui/commands/custommenu/CustomMenusMenu.h"

namespace {

using namespace r1test;
using cm::CustomMenuSet;
using cm::MenuAction;
using cm::MenuDescEntry;
using cm::MenuKind;
using Kind = MenuDescEntry::Kind;

void testCommandIds() {
  for (const MenuAction action : {MenuAction::Open, MenuAction::Edit, MenuAction::Save, MenuAction::Delete}) {
    for (const std::string id : {"menu.1", "menu.42", "menu.4000000"}) {
      const std::string commandId = cm::commandIdFor(action, id);
      R1_EXPECT(r1ui::commands::isValidIdentifier(commandId, 128));
      const auto ref = cm::parseMenuCommandId(commandId);
      R1_EXPECT(ref && ref->action == action && ref->menuId == id);
    }
  }
  R1_EXPECT(cm::commandIdFor(MenuAction::Open, "menu.3") == "custommenu.open.menu.3");
  R1_EXPECT(!cm::parseMenuCommandId(cm::kCommandCreate) && !cm::parseMenuCommandId(cm::kCommandLoad));
  R1_EXPECT(!cm::parseMenuCommandId("") && !cm::parseMenuCommandId("custommenu.") && !cm::parseMenuCommandId("custommenu.open") &&
            !cm::parseMenuCommandId("custommenu.open.") && !cm::parseMenuCommandId("custommenu.fly.menu.1") && !cm::parseMenuCommandId("edit.undo"));
}

void testDescription() {
  CustomMenuSet set;
  // No menus: only the two global commands.
  auto d = cm::describeCustomMenusMenu(set);
  R1_EXPECT(d.size() == 2 && d[0].commandId == cm::kCommandCreate && d[1].commandId == cm::kCommandLoad);

  set.createMenu(MenuKind::Panel, "Quick");
  set.createMenu(MenuKind::Pie, "Radial");
  set.createMenu(MenuKind::Panel, "Second");
  d = cm::describeCustomMenusMenu(set);
  // Dockable menus, pie menus, edit submenu, globals; separators between the groups.
  R1_EXPECT(d.size() == 11);
  R1_EXPECT(d[0].kind == Kind::Heading && d[0].text == "Dockable menus");
  R1_EXPECT(d[1].kind == Kind::Command && d[1].commandId == "custommenu.open.menu.1");
  R1_EXPECT(d[2].kind == Kind::Command && d[2].commandId == "custommenu.open.menu.3");
  R1_EXPECT(d[3].kind == Kind::Separator);
  R1_EXPECT(d[4].kind == Kind::Heading && d[4].text == "Pie menus");
  R1_EXPECT(d[5].kind == Kind::Submenu && d[5].text == "Radial" && d[5].children.size() == 4);
  R1_EXPECT(d[5].children[0].commandId == "custommenu.edit.menu.2" && d[5].children[1].commandId == "custommenu.save.menu.2" &&
            d[5].children[2].kind == Kind::Separator && d[5].children[3].commandId == "custommenu.delete.menu.2");
  R1_EXPECT(d[6].kind == Kind::Separator);
  R1_EXPECT(d[7].kind == Kind::Submenu && d[7].text == "Edit Dockable Menu" && d[7].children.size() == 2 && d[7].children[1].text == "Second");
  R1_EXPECT(d[8].kind == Kind::Separator);
  R1_EXPECT(d[9].commandId == cm::kCommandCreate && d[10].commandId == cm::kCommandLoad);

  // Only pies: no dockable parts.
  CustomMenuSet pies;
  pies.createMenu(MenuKind::Pie, "P");
  d = cm::describeCustomMenusMenu(pies);
  R1_EXPECT(d.size() == 5 && d[0].text == "Pie menus" && d[2].kind == Kind::Separator);

  // Deleting a menu removes its entries; the description follows the set.
  set.deleteMenu("menu.1");
  set.deleteMenu("menu.3");
  d = cm::describeCustomMenusMenu(set);
  R1_EXPECT(d.size() == 5);
}

void testHostileNames() {
  CustomMenuSet set;
  set.createMenu(MenuKind::Panel, std::string("evil\0name\xff", 10));
  set.createMenu(MenuKind::Panel, std::string(300, 'z'));
  const auto d = cm::describeCustomMenusMenu(set);
  for (const MenuDescEntry& e : d) {
    R1_EXPECT(r1ui::commands::isValidUtf8(e.text));
    if (e.kind == Kind::Command) R1_EXPECT(e.commandId.size() <= 128);
  }
  for (const MenuDescEntry& e : d[3].kind == Kind::Submenu ? d[3].children : d[5].children) R1_EXPECT(e.text.find('\0') == std::string::npos);
}

}  // namespace

int main() {
  testCommandIds();
  testDescription();
  testHostileNames();
  return r1test::finish();
}
