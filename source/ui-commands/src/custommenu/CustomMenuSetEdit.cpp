// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the entry and setting operations of CustomMenuSet (setSlot, clearSlot, addEntry, moveEntry,
//   removeEntry, setEntryAppearance, setPieSlotCount, the panel settings).
// Invariants: every operation validates its inputs completely before it touches the menu, so a refusal
//   changes nothing; a pie keeps exactly slotCount entries; a panel entry always has a command.
// Callers: CustomMenuSet users (widget layer, hosts, tests).
#include <cmath>

#include "r1ui/commands/custommenu/CustomMenuSet.h"

namespace r1ui::commands::custommenu {

namespace {

// Builds a clean entry or explains why not.
bool makeEntry(const std::string& commandId, const std::string& label, const std::string& icon, bool allowEmptyCommand, MenuEntry& out, std::string& reason) {
  out.commandId = commandId;
  out.label = label;
  out.icon = icon;
  if (!allowEmptyCommand && commandId.empty()) {
    reason = "choose a command for the entry";
    return false;
  }
  return normalizeEntry(out, reason);
}

}  // namespace

MenuEditResult CustomMenuSet::setSlot(const std::string& id, size_t index, const std::string& commandId, const std::string& label, const std::string& icon) {
  CustomMenu* menu = mutableFind(id);
  if (menu == nullptr) return unknown(id);
  if (index >= menu->entries.size()) return fail(MenuError::OutOfRange, "there is no such slot");
  MenuEntry entry;
  std::string reason;
  if (!makeEntry(commandId, label, icon, false, entry, reason)) return fail(MenuError::InvalidEntry, reason);
  if (menu->entries[index].commandId.empty() && totalEntries() >= kMaxTotalEntries) return fail(MenuError::LimitReached, "too many entries in the custom menus");
  menu->entries[index] = std::move(entry);
  return done(id, index);
}

MenuEditResult CustomMenuSet::clearSlot(const std::string& id, size_t index) { return removeEntry(id, index); }

MenuEditResult CustomMenuSet::removeEntry(const std::string& id, size_t index) {
  CustomMenu* menu = mutableFind(id);
  if (menu == nullptr) return unknown(id);
  if (index >= menu->entries.size()) return fail(MenuError::OutOfRange, "there is no such entry");
  if (menu->kind == MenuKind::Pie) {
    menu->entries[index] = MenuEntry{};
  } else {
    menu->entries.erase(menu->entries.begin() + static_cast<std::ptrdiff_t>(index));
  }
  return done(id, index);
}

MenuEditResult CustomMenuSet::addEntry(const std::string& id, const std::string& commandId, size_t index, const std::string& label, const std::string& icon) {
  CustomMenu* menu = mutableFind(id);
  if (menu == nullptr) return unknown(id);
  MenuEntry entry;
  std::string reason;
  if (!makeEntry(commandId, label, icon, false, entry, reason)) return fail(MenuError::InvalidEntry, reason);
  if (totalEntries() >= kMaxTotalEntries) return fail(MenuError::LimitReached, "too many entries in the custom menus");
  if (menu->kind == MenuKind::Pie) {
    for (size_t i = 0; i < menu->entries.size(); ++i) {
      if (menu->entries[i].commandId.empty()) {
        menu->entries[i] = std::move(entry);
        return done(id, i);
      }
    }
    return fail(MenuError::LimitReached, "every slot of the pie menu is in use");
  }
  if (menu->entries.size() >= kMaxPanelEntries) return fail(MenuError::LimitReached, "the menu is full");
  if (index == npos) index = menu->entries.size();
  if (index > menu->entries.size()) return fail(MenuError::OutOfRange, "there is no such position");
  menu->entries.insert(menu->entries.begin() + static_cast<std::ptrdiff_t>(index), std::move(entry));
  return done(id, index);
}

MenuEditResult CustomMenuSet::moveEntry(const std::string& id, size_t from, size_t to) {
  CustomMenu* menu = mutableFind(id);
  if (menu == nullptr) return unknown(id);
  if (from >= menu->entries.size() || to >= menu->entries.size()) return fail(MenuError::OutOfRange, "there is no such entry");
  if (menu->kind == MenuKind::Pie) {
    std::swap(menu->entries[from], menu->entries[to]);
  } else if (from != to) {
    MenuEntry moving = std::move(menu->entries[from]);
    menu->entries.erase(menu->entries.begin() + static_cast<std::ptrdiff_t>(from));
    menu->entries.insert(menu->entries.begin() + static_cast<std::ptrdiff_t>(to), std::move(moving));
  }
  return done(id, to);
}

MenuEditResult CustomMenuSet::setEntryAppearance(const std::string& id, size_t index, const std::string& label, const std::string& icon) {
  CustomMenu* menu = mutableFind(id);
  if (menu == nullptr) return unknown(id);
  if (index >= menu->entries.size()) return fail(MenuError::OutOfRange, "there is no such entry");
  MenuEntry entry = menu->entries[index];
  if (entry.commandId.empty()) return fail(MenuError::InvalidEntry, "the slot is empty");
  entry.label = label;
  entry.icon = icon;
  std::string reason;
  if (!normalizeEntry(entry, reason)) return fail(MenuError::InvalidEntry, reason);
  menu->entries[index] = std::move(entry);
  return done(id, index);
}

// ---- kind-specific settings ----------------------------------------------------------------------

MenuEditResult CustomMenuSet::setPieSlotCount(const std::string& id, int slotCount) {
  CustomMenu* menu = mutableFind(id);
  if (menu == nullptr) return unknown(id);
  if (menu->kind != MenuKind::Pie) return fail(MenuError::WrongKind, "only a pie menu has slots");
  if (!isValidPieSlotCount(slotCount)) return fail(MenuError::OutOfRange, "a pie menu has 4, 6 or 8 slots");
  const size_t count = static_cast<size_t>(slotCount);
  for (size_t i = count; i < menu->entries.size(); ++i) {
    if (!menu->entries[i].commandId.empty()) return fail(MenuError::WouldLoseEntries, "empty the slots that would disappear first");
  }
  menu->entries.resize(count);
  menu->slotCount = slotCount;
  return done(id);
}

MenuEditResult CustomMenuSet::setPanelColumns(const std::string& id, int columns) {
  CustomMenu* menu = mutableFind(id);
  if (menu == nullptr) return unknown(id);
  if (menu->kind != MenuKind::Panel) return fail(MenuError::WrongKind, "only a dockable menu has columns");
  if (columns < kMinColumns || columns > kMaxColumns) return fail(MenuError::OutOfRange, "columns must be between 1 and 12");
  menu->panel.columns = columns;
  return done(id);
}

MenuEditResult CustomMenuSet::setPanelButtonSize(const std::string& id, int buttonSize) {
  CustomMenu* menu = mutableFind(id);
  if (menu == nullptr) return unknown(id);
  if (menu->kind != MenuKind::Panel) return fail(MenuError::WrongKind, "only a dockable menu has buttons");
  if (buttonSize < kMinButtonSize || buttonSize > kMaxButtonSize) return fail(MenuError::OutOfRange, "button size must be between 24 and 96");
  menu->panel.buttonSize = buttonSize;
  return done(id);
}

MenuEditResult CustomMenuSet::setPanelShowLabels(const std::string& id, bool show) {
  CustomMenu* menu = mutableFind(id);
  if (menu == nullptr) return unknown(id);
  if (menu->kind != MenuKind::Panel) return fail(MenuError::WrongKind, "only a dockable menu has button labels");
  menu->panel.showLabels = show;
  return done(id);
}

MenuEditResult CustomMenuSet::setPanelSize(const std::string& id, double width, double height) {
  CustomMenu* menu = mutableFind(id);
  if (menu == nullptr) return unknown(id);
  if (menu->kind != MenuKind::Panel) return fail(MenuError::WrongKind, "only a dockable menu has a panel size");
  const auto ok = [](double v) { return v == 0.0 || (std::isfinite(v) && v >= kMinPanelDimension && v <= kMaxPanelDimension); };
  if (!ok(width) || !ok(height)) return fail(MenuError::OutOfRange, "the panel size is out of range");
  menu->panel.width = width;
  menu->panel.height = height;
  return done(id);
}

}  // namespace r1ui::commands::custommenu
