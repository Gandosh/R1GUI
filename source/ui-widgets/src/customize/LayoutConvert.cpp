// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of LayoutConvert.h.
// Invariants: flattenSections returns entries and (when asked) overrides of the same length and order,
//   so applyLabelOverrides can walk them together; generated built-in ids are unique inside one call;
//   nesting follows the model's depth limit (the model never hands out deeper trees).
// Callers: CustomizableMenuBar, CustomizableToolbar, the gallery, tests.
#include "r1ui/widgets/customize/LayoutConvert.h"

#include <unordered_set>

namespace r1ui::widgets {

namespace cz = commands::customize;

namespace {

LabelOverride placeholder(cz::Kind kind) {
  LabelOverride o;
  o.kind = kind;
  return o;
}

void pushEntry(std::vector<CommandMenuEntry>& entries, std::vector<LabelOverride>* overrides, CommandMenuEntry entry, LabelOverride override) {
  entries.push_back(std::move(entry));
  if (overrides != nullptr) overrides->push_back(std::move(override));
}

}  // namespace

std::vector<CommandMenuEntry> flattenSections(const std::vector<cz::Node>& sections, const cz::Customization& model, std::vector<LabelOverride>* overrides) {
  std::vector<CommandMenuEntry> entries;
  bool first = true;
  for (const cz::Node& section : sections) {
    if (section.kind != cz::Kind::Section) continue;
    const std::string& heading = section.shownLabel();
    if (section.children.empty() && heading.empty()) continue;
    if (!first) pushEntry(entries, overrides, CommandMenuEntry::separator(), placeholder(cz::Kind::Separator));
    first = false;
    if (!heading.empty()) pushEntry(entries, overrides, CommandMenuEntry::heading(heading), placeholder(cz::Kind::Heading));
    for (const cz::Node& node : section.children) {
      switch (node.kind) {
        case cz::Kind::Command: {
          LabelOverride o = placeholder(cz::Kind::Command);
          o.commandId = node.commandId;
          o.label = node.userLabel;
          pushEntry(entries, overrides, CommandMenuEntry::command(node.commandId), std::move(o));
          break;
        }
        case cz::Kind::Separator: pushEntry(entries, overrides, CommandMenuEntry::separator(), placeholder(cz::Kind::Separator)); break;
        case cz::Kind::Heading: pushEntry(entries, overrides, CommandMenuEntry::heading(node.shownLabel()), placeholder(cz::Kind::Heading)); break;
        case cz::Kind::Submenu: {
          LabelOverride o = placeholder(cz::Kind::Submenu);
          std::vector<CommandMenuEntry> children = flattenSections(node.children, model, &o.children);
          pushEntry(entries, overrides, CommandMenuEntry::submenu(node.shownLabel(), std::move(children)), std::move(o));
          break;
        }
        default: break;
      }
    }
  }
  return entries;
}

MenuConversion convertMenuBar(const cz::MenuLayout& effective, const cz::Customization& model) {
  MenuConversion out;
  for (const cz::Node& menu : effective.menus) {
    if (menu.kind != cz::Kind::Menu) continue;
    CommandMenuTitle title;
    title.title = menu.shownLabel();
    std::vector<LabelOverride> overrides;
    title.entries = flattenSections(menu.children, model, &overrides);
    out.titles.push_back(std::move(title));
    out.overrides.push_back(std::move(overrides));
  }
  return out;
}

std::vector<ToolbarRun> convertToolbar(const cz::ToolbarLayout& effective) {
  std::vector<ToolbarRun> runs;
  ToolbarRun current;
  for (const cz::Node& node : effective.items) {
    switch (node.kind) {
      case cz::Kind::Command: current.items.push_back(CommandToolbarItem::command(node.commandId)); break;
      case cz::Kind::Separator: current.items.push_back(CommandToolbarItem::separator()); break;
      case cz::Kind::Group: {
        std::vector<std::string> ids;
        for (const cz::Node& c : node.children) ids.push_back(c.commandId);
        current.items.push_back(CommandToolbarItem::group(std::move(ids)));
        break;
      }
      case cz::Kind::Spacer:
        current.spacerAfter = true;
        runs.push_back(std::move(current));
        current = {};
        break;
      default: break;
    }
  }
  runs.push_back(std::move(current));
  return runs;
}

// ---- the built-in side --------------------------------------------------------------------------

namespace {

class IdMaker {
 public:
  explicit IdMaker(std::string prefix) : prefix_(std::move(prefix)) {}
  std::string make(const std::string& tail) {
    std::string id = prefix_ + "." + tail;
    for (int n = 2; !used_.insert(id).second; ++n) id = prefix_ + "." + tail + "-" + std::to_string(n);
    return id;
  }

 private:
  std::string prefix_;
  std::unordered_set<std::string> used_;
};

std::vector<cz::Node> sectionsFromEntries(IdMaker& ids, const std::vector<CommandMenuEntry>& entries) {
  std::vector<cz::Node> sections;
  cz::Node current = cz::Node::section(ids.make("s"), "");
  const auto close = [&] {
    if (!current.children.empty() || !current.label.empty()) sections.push_back(std::move(current));
    current = cz::Node::section(ids.make("s"), "");
  };
  for (const CommandMenuEntry& entry : entries) {
    switch (entry.kind) {
      case CommandMenuEntry::Kind::Separator: close(); break;
      case CommandMenuEntry::Kind::Heading:
        if (current.children.empty() && current.label.empty()) {
          current.label = entry.text;
        } else {
          current.children.push_back(cz::Node::heading(ids.make("h"), entry.text));
        }
        break;
      case CommandMenuEntry::Kind::Command: current.children.push_back(cz::Node::command(ids.make(entry.commandId), entry.commandId)); break;
      case CommandMenuEntry::Kind::Submenu: current.children.push_back(cz::Node::submenu(ids.make("m"), entry.text, sectionsFromEntries(ids, entry.children))); break;
    }
  }
  close();
  return sections;
}

}  // namespace

cz::Node menuNodeFromEntries(const std::string& menuId, const std::string& title, const std::vector<CommandMenuEntry>& entries) {
  IdMaker ids(menuId);
  return cz::Node::menu(menuId, title, sectionsFromEntries(ids, entries));
}

cz::ToolbarLayout toolbarFromItems(const std::string& toolbarId, const std::string& title, const std::vector<CommandToolbarItem>& items) {
  IdMaker ids(toolbarId);
  cz::ToolbarLayout t;
  t.id = toolbarId;
  t.title = title;
  for (const CommandToolbarItem& item : items) {
    switch (item.kind) {
      case CommandToolbarItem::Kind::Command:
        if (!item.commandIds.empty()) t.items.push_back(cz::Node::command(ids.make(item.commandIds.front()), item.commandIds.front()));
        break;
      case CommandToolbarItem::Kind::Separator: t.items.push_back(cz::Node::separator(ids.make("sep"))); break;
      case CommandToolbarItem::Kind::Group: {
        std::vector<cz::Node> children;
        for (const std::string& c : item.commandIds) children.push_back(cz::Node::command(ids.make(c), c));
        t.items.push_back(cz::Node::group(ids.make("group"), std::move(children)));
        break;
      }
    }
  }
  return t;
}

// ---- label overrides ----------------------------------------------------------------------------

void applyLabelOverrides(std::vector<MenuItemSpec>& items, const std::vector<CommandMenuEntry>& entries, const std::vector<LabelOverride>& overrides,
                         const CommandServices& services) {
  size_t j = 0;
  for (MenuItemSpec& item : items) {
    if (item.kind == MenuItemKind::Separator || item.kind == MenuItemKind::Heading) continue;
    const bool submenu = item.kind == MenuItemKind::Submenu;
    while (j < entries.size()) {
      const CommandMenuEntry& e = entries[j];
      if (submenu ? (e.kind == CommandMenuEntry::Kind::Submenu && sanitizeMenuText(e.text) == item.label)
                  : (e.kind == CommandMenuEntry::Kind::Command && e.commandId == item.id)) {
        break;
      }
      ++j;
    }
    if (j >= entries.size() || j >= overrides.size()) return;
    if (submenu) {
      applyLabelOverrides(item.children, entries[j].children, overrides[j].children, services);
    } else if (!overrides[j].label.empty()) {
      const std::string real = item.id;
      item.label = sanitizeMenuText(overrides[j].label);
      item.id = "customize:" + real;
      commands::CommandRouter* router = &services.router;
      item.onActivate = [router, real](const MenuItemSpec&) { router->execute(real, commands::ExecuteSource::Menu); };
    }
    ++j;
  }
}

}  // namespace r1ui::widgets
