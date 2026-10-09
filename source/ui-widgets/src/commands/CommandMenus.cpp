// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CommandMenus.h.
// Invariants: a built row's id is the command id and its callbacks only call the router; the refresh
//   touches a row only when something it shows differs (an idle open menu requests no frames).
// Callers: hosts, the gallery, tests.
#include "r1ui/widgets/commands/CommandMenus.h"

#include <algorithm>
#include <functional>

#include "r1ui/widgets/menu/MenuPanel.h"
#include "r1ui/widgets/overlay/OverlayManager.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace cmd = commands;
using core::tree::WidgetId;

// ---- entries ------------------------------------------------------------------------------------

CommandMenuEntry CommandMenuEntry::command(std::string id) {
  CommandMenuEntry e;
  e.commandId = std::move(id);
  return e;
}

CommandMenuEntry CommandMenuEntry::separator() {
  CommandMenuEntry e;
  e.kind = Kind::Separator;
  return e;
}

CommandMenuEntry CommandMenuEntry::heading(std::string text) {
  CommandMenuEntry e;
  e.kind = Kind::Heading;
  e.text = std::move(text);
  return e;
}

CommandMenuEntry CommandMenuEntry::submenu(std::string text, std::vector<CommandMenuEntry> children) {
  CommandMenuEntry e;
  e.kind = Kind::Submenu;
  e.text = std::move(text);
  e.children = std::move(children);
  return e;
}

// ---- rows ---------------------------------------------------------------------------------------

MenuItemSpec commandMenuItem(const CommandServices& services, const cmd::CommandDef& command, const CommandMenuOptions& options) {
  MenuItemSpec spec;
  spec.id = command.id;
  spec.label = command.label;
  if (options.showIcons) spec.icon = command.icon;
  switch (command.kind) {
    case cmd::CommandKind::Toggle: spec.kind = MenuItemKind::Check; break;
    case cmd::CommandKind::Radio: spec.kind = MenuItemKind::Radio; break;
    default: spec.kind = MenuItemKind::Action; break;
  }
  spec.shortcut = services.keymap.displayText(command.id, options.upperCaseShortcuts);
  spec.tooltip = command.tooltipText();
  spec.enabled = command.isEnabled();
  spec.checked = spec.kind != MenuItemKind::Action && command.isChecked();
  cmd::CommandRouter* router = &services.router;
  spec.onActivate = [router](const MenuItemSpec& item) { router->execute(item.id, cmd::ExecuteSource::Menu); };
  return spec;
}

namespace {

// Drops leading, trailing and doubled separators (a collapsed command must not leave a stray line).
void tidySeparators(std::vector<MenuItemSpec>& items) {
  std::vector<MenuItemSpec> out;
  out.reserve(items.size());
  for (MenuItemSpec& item : items) {
    if (item.kind == MenuItemKind::Separator && (out.empty() || out.back().kind == MenuItemKind::Separator)) continue;
    out.push_back(std::move(item));
  }
  while (!out.empty() && out.back().kind == MenuItemKind::Separator) out.pop_back();
  items = std::move(out);
}

std::vector<MenuItemSpec> buildItems(const CommandServices& services, std::span<const CommandMenuEntry> entries, const CommandMenuOptions& options, int depth) {
  std::vector<MenuItemSpec> items;
  for (const CommandMenuEntry& entry : entries) {
    if (items.size() >= kMaxMenuItems) break;
    switch (entry.kind) {
      case CommandMenuEntry::Kind::Command: {
        const cmd::CommandDef* command = services.registry.find(entry.commandId);
        if (command != nullptr && command->isVisible()) items.push_back(commandMenuItem(services, *command, options));
        break;
      }
      case CommandMenuEntry::Kind::Separator: items.push_back(menuSeparator()); break;
      case CommandMenuEntry::Kind::Heading: items.push_back(menuHeading(sanitizeMenuText(entry.text))); break;
      case CommandMenuEntry::Kind::Submenu: {
        if (depth + 1 >= kMaxMenuDepth) break;
        std::vector<MenuItemSpec> children = buildItems(services, entry.children, options, depth + 1);
        if (!children.empty()) items.push_back(menuSubmenu(sanitizeMenuText(entry.text), std::move(children)));
        break;
      }
    }
  }
  tidySeparators(items);
  return items;
}

}  // namespace

MenuSpec buildCommandMenu(const CommandServices& services, std::span<const CommandMenuEntry> entries, const CommandMenuOptions& options) {
  MenuSpec spec;
  spec.items = buildItems(services, entries, options, 1);
  return spec;
}

// ---- bars and context menus ---------------------------------------------------------------------

void bindCommandMenuBar(MenuBar& bar, const CommandServices& services, std::vector<CommandMenuTitle> titles, const CommandMenuOptions& options) {
  auto layouts = std::make_shared<std::vector<std::pair<int, std::vector<CommandMenuEntry>>>>();  // menu index -> entries
  for (CommandMenuTitle& title : titles) {
    const int index = bar.addMenu(title.title, buildCommandMenu(services, title.entries, options));
    if (index >= 0) layouts->emplace_back(index, std::move(title.entries));
  }
  const CommandServices copy = services;
  bar.setBeforeOpen([copy, layouts, options](int index, MenuSpec& spec) {
    for (const auto& [menuIndex, entries] : *layouts) {
      if (menuIndex == index) spec.items = buildCommandMenu(copy, entries, options).items;
    }
  });
}

bool openCommandContextMenu(MenuController& controller, const CommandServices& services, std::span<const CommandMenuEntry> entries, double x, double y,
                            const CommandMenuOptions& options) {
  return controller.openContextMenu(buildCommandMenu(services, entries, options), x, y);
}

// ---- live refresh -------------------------------------------------------------------------------

size_t refreshOpenCommandMenus(UiContext& ui, const CommandServices& services, const CommandMenuOptions& options) {
  std::vector<WidgetId> panels;
  for (const OverlayId overlay : ui.overlays().stack()) {
    ui.tree().forEachDescendant(ui.overlays().hostOf(overlay), [&](WidgetId id) {
      if (ui.objectAs<MenuPanel>(id) != nullptr) panels.push_back(id);
    });
  }
  size_t updated = 0;
  for (const WidgetId id : panels) {
    MenuPanel* panel = ui.objectAs<MenuPanel>(id);
    if (panel == nullptr) continue;
    for (int i = 0; i < panel->itemCount(); ++i) {
      const MenuItemSpec& shown = panel->item(i);
      if (shown.id.empty() || shown.kind == MenuItemKind::Submenu || shown.kind == MenuItemKind::Separator || shown.kind == MenuItemKind::Heading) continue;
      const cmd::CommandDef* command = services.registry.find(shown.id);
      if (command == nullptr) continue;
      const MenuItemSpec live = commandMenuItem(services, *command, options);
      const bool same = shown.label == sanitizeMenuText(live.label) && shown.shortcut == sanitizeMenuText(live.shortcut) && shown.tooltip == sanitizeMenuText(live.tooltip) &&
                        shown.enabled == live.enabled && shown.checked == live.checked;
      if (!same && panel->refreshItem(i, live)) ++updated;
    }
  }
  return updated;
}

}  // namespace r1ui::widgets
