// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the visual oracle of menus built from commands. The hand-written menus of the reference
//   captures (File, Edit, View and the canvas context menu with its submenu) are turned into commands
//   (one command per row: label, icon, kind, enabled and checked answers, default chord parsed from the
//   row's shortcut text), menus are built back from those commands, and the result is
//     (1) compared field by field with the hand-written specs (the shortcut text produced from a chord
//         equals the reference text; rows whose reference text is not a chord, like the delete glyph,
//         are patched back and counted), and
//     (2) rendered and compared with the same reference crops the hand-built menus use (menu_visual_test),
//         the File menu through bindCommandMenuBar (the real MenuBar path with the rebuild before open).
// Callers: CTest (label gpu, offscreen, no window).
// Method notes: identical to menu_visual_test: luminance on text crops, the screens' side panels are
//   excluded with `only`. The patched rows are listed in the output so nothing is hidden.
#include "../menu/ReferenceMenus.h"
#include "../menu/ScreenCompare.h"
#include "r1ui/commands/Clock.h"
#include "r1ui/widgets/commands/CommandMenus.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/menu/MenuPanel.h"

namespace {

using namespace r1test::screen;
using r1ui::theme::ThemeId;
namespace m = r1test::menus;
namespace cmd = r1ui::commands;

// The command objects behind the menus; no UiContext is needed to build a MenuSpec.
struct Factory {
  Factory() : router(registry, keymap, clock) { registry.addContext("m.context"); registry.addContext("m.file"); registry.addContext("m.edit"); registry.addContext("m.view"); }
  CommandServices services() { return {registry, overrides, keymap, router}; }

  cmd::CommandRegistry registry;
  cmd::KeybindingOverrides overrides{registry};
  cmd::Keymap keymap{registry, overrides};
  cmd::ManualClock clock;
  cmd::CommandRouter router;
  std::vector<std::string> patched;  // rows whose reference shortcut text is not a chord
  int rows = 0;
};

// One command per row of a hand-written menu; returns the entries that rebuild its layout.
std::vector<CommandMenuEntry> declareFrom(Factory& f, const std::string& context, const std::string& prefix, const std::vector<MenuItemSpec>& items) {
  std::vector<CommandMenuEntry> entries;
  for (const MenuItemSpec& item : items) {
    if (item.kind == MenuItemKind::Separator) {
      entries.push_back(CommandMenuEntry::separator());
      continue;
    }
    if (item.kind == MenuItemKind::Submenu) {
      entries.push_back(CommandMenuEntry::submenu(item.label, declareFrom(f, context, prefix + "sub" + std::to_string(entries.size()) + ".", item.children)));
      continue;
    }
    cmd::CommandDef def;
    def.id = prefix + item.id;
    def.label = item.label;
    def.icon = item.icon;
    def.context = context;
    def.kind = item.kind == MenuItemKind::Check ? cmd::CommandKind::Toggle : item.kind == MenuItemKind::Radio ? cmd::CommandKind::Radio : cmd::CommandKind::Action;
    if (def.kind == cmd::CommandKind::Radio) def.radioGroup = prefix + "group";
    if (const auto chord = cmd::parseSequence(item.shortcut); chord && !item.shortcut.empty()) def.defaultChords[0] = *chord;
    const bool enabled = item.enabled;
    const bool checked = item.checked;
    def.enabled = [enabled] { return enabled; };
    def.checked = [checked] { return checked; };
    def.execute = [](const cmd::ExecuteArgs&) { return cmd::ExecuteResult::handled(); };
    const std::string id = def.id;
    R1_EXPECT(f.registry.add(std::move(def)).ok);
    entries.push_back(CommandMenuEntry::command(id));
    ++f.rows;
  }
  return entries;
}

// Compares a command-built menu with its hand-written original and patches the rows whose reference
// text is not a chord; everything else must already be equal.
void reconcile(Factory& f, std::vector<MenuItemSpec>& built, const std::vector<MenuItemSpec>& hand) {
  R1_EXPECT(built.size() == hand.size());
  for (size_t i = 0; i < built.size() && i < hand.size(); ++i) {
    MenuItemSpec& b = built[i];
    const MenuItemSpec& h = hand[i];
    R1_EXPECT(b.kind == h.kind && b.label == h.label && b.icon == h.icon && b.enabled == h.enabled && b.checked == h.checked);
    if (b.shortcut != h.shortcut) {
      R1_EXPECT(!cmd::parseSequence(h.shortcut).has_value() || h.shortcut.empty());  // only non-chord texts may differ
      f.patched.push_back(h.label + " [" + h.shortcut + "]");
      b.shortcut = h.shortcut;
    }
    b.tone = h.tone;
    reconcile(f, b.children, h.children);
  }
}

MenuSpec commandMenu(Factory& f, const std::string& context, const std::string& prefix, const MenuSpec& hand) {
  const std::vector<CommandMenuEntry> entries = declareFrom(f, context, prefix, hand.items);
  MenuSpec built = buildCommandMenu(f.services(), entries);
  built.minWidth = hand.minWidth;
  reconcile(f, built.items, hand.items);
  return built;
}

MenuPanel* panelOf(UiContext& ui, const MenuController& c, int level) { return ui.objectAs<MenuPanel>(c.panelAt(level)); }

BuildFn contextMenu(const MenuSpec& spec, int row, bool openSubmenu, bool inSubmenu) {
  return [=](UiContext& ui, WidgetId) {
    MenuController controller(ui);
    controller.openContextMenu(spec, 412, 188);
    MenuPanel* root = panelOf(ui, controller, 0);
    ui.frame();
    if (openSubmenu) {
      root->setHighlight(28, false);
      root->itemActivated(28, false);
    }
    MenuPanel* target = inSubmenu ? panelOf(ui, controller, 1) : root;
    return target->itemWidget(row);
  };
}

// The menu bar of the reference; File through bindCommandMenuBar when `bound`, else the given specs.
BuildFn menuBar(Factory& f, const std::vector<std::pair<std::string, MenuSpec>>& menus, const std::vector<CommandMenuEntry>* boundFile, int open, int submenuRow = -1) {
  return [&f, menus, boundFile, open, submenuRow](UiContext& ui, WidgetId parent) {
    MenuBar& bar = ui.create<MenuBar>(parent);
    bar.style().margin[r1ui::core::layout::kLeft] = r1ui::core::layout::Length::px(6);
    bar.style().margin[r1ui::core::layout::kTop] = r1ui::core::layout::Length::px(36);
    if (boundFile != nullptr) {
      bindCommandMenuBar(bar, f.services(), {{"File", *boundFile}});
    } else {
      bar.addMenu(menus[0].first, menus[0].second);
    }
    for (size_t i = 1; i < menus.size(); ++i) bar.addMenu(menus[i].first, menus[i].second);
    bar.addMenu("Object", MenuSpec{{menuAction("x", "Object")}});
    ui.frame();
    if (open >= 0) {
      bar.openMenu(open);
      ui.frame();
      if (submenuRow >= 0) {
        MenuPanel* panel = ui.objectAs<MenuPanel>(bar.controller().panelAt(0));
        panel->setHighlight(submenuRow, false);
        panel->itemActivated(submenuRow, false);
      }
    }
    return bar.itemWidget(0);
  };
}

Case crop(const char* reference, double x, double y, ThemeId theme, VisualState state = VisualState::Idle) {
  Case c;
  c.reference = reference;
  c.clipX = x;
  c.clipY = y;
  c.theme = theme;
  c.state = state;
  c.luminance = true;
  c.profile = "text";
  return c;
}

}  // namespace

int main() {
  Factory f;
  const MenuSpec context = commandMenu(f, "m.context", "ctx.", m::contextMenu());
  const MenuSpec fileHand = m::fileMenu();
  const std::vector<CommandMenuEntry> fileEntries = declareFrom(f, "m.file", "file.", fileHand.items);
  MenuSpec file = buildCommandMenu(f.services(), fileEntries);
  reconcile(f, file.items, fileHand.items);
  const MenuSpec edit = commandMenu(f, "m.edit", "edit.", m::editMenu());
  const MenuSpec view = commandMenu(f, "m.view", "view.", m::viewMenu());
  std::printf("commands %d, rows patched to the reference text (not chords): %zu\n", f.rows, f.patched.size());
  for (const std::string& row : f.patched) std::printf("  patched: %s\n", row.c_str());
  R1_EXPECT(f.rows == 56 && f.patched.size() == 9);  // delete glyph x2, Alt+] / [ x4, Ctrl+= / - / backslash

  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    R1_EXPECT_CROP(contextMenu(context, 7, false, false), crop("widget-menu-content-idle", 406, 182, theme));
    R1_EXPECT_CROP(contextMenu(context, 7, false, false), crop("widget-menu-item-idle", 411, 364, theme));
    R1_EXPECT_CROP(contextMenu(context, 7, false, false), crop("widget-menu-item-hover", 411, 364, theme, VisualState::Hover));
    R1_EXPECT_CROP(contextMenu(context, 12, false, false), crop("widget-menu-item-disabled-idle", 411, 485, theme));
    R1_EXPECT_CROP(contextMenu(context, 20, false, false), crop("widget-menu-item-component-idle", 411, 690, theme));
    R1_EXPECT_CROP(contextMenu(context, 28, true, false), crop("widget-menu-item-submenu-open", 411, 857, theme));
    R1_EXPECT_CROP(contextMenu(context, 28, true, true), crop("widget-menu-submenu-content-idle", 625, 711, theme));

    const std::vector<std::pair<std::string, MenuSpec>> menus{{"File", file}, {"Edit", edit}, {"View", view}};
    Case fileCase = crop("screen-file-menu-submenu-open", 0, 0, theme);
    fileCase.cropW = 400;
    fileCase.cropH = 320;
    fileCase.only = {{6, 66, 205, 228}, {213, 200, 172, 92}};
    R1_EXPECT_CROP(menuBar(f, menus, &fileEntries, 0, 6), fileCase);  // the real MenuBar binding
    Case editCase = crop("screen-menubar-edit-open", 0, 0, theme);
    editCase.cropW = 300;
    editCase.cropH = 360;
    editCase.only = {{43, 66, 205, 276}};
    R1_EXPECT_CROP(menuBar(f, menus, nullptr, 1), editCase);
    Case viewCase = crop("screen-menubar-view-open", 0, 0, theme);
    viewCase.cropW = 340;
    viewCase.cropH = 360;
    viewCase.only = {{83, 66, 205, 276}};
    R1_EXPECT_CROP(menuBar(f, menus, nullptr, 2), viewCase);
  }
  R1_EXPECT(r1ui::widgets::testing::validationMessageCount() == 0);
  return r1test::finish();
}
