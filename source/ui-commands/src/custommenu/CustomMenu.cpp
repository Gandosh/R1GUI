// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the validators and small helpers declared in CustomMenu.h (names, labels, entries, whole menus).
// Invariants: every function is total and allocation-bounded by the limits in the header; text is made
//   well-formed UTF-8 without control characters before it is stored anywhere.
// Callers: CustomMenuSet, CustomMenuIo, tests.
#include "r1ui/commands/custommenu/CustomMenu.h"

#include <cctype>
#include <cmath>

#include "r1ui/commands/Text.h"

namespace r1ui::commands::custommenu {

const char* kindName(MenuKind kind) { return kind == MenuKind::Pie ? "pie" : "panel"; }

bool parseKind(std::string_view text, MenuKind& out) {
  if (text == "pie") {
    out = MenuKind::Pie;
    return true;
  }
  if (text == "panel") {
    out = MenuKind::Panel;
    return true;
  }
  return false;
}

bool isValidPieSlotCount(int count) { return count == 4 || count == 6 || count == 8; }

// ---- text ---------------------------------------------------------------------------------------

namespace {

bool isSpace(char c) { return c == ' '; }

std::string trimmed(std::string text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && isSpace(text[begin])) ++begin;
  while (end > begin && isSpace(text[end - 1])) --end;
  return text.substr(begin, end - begin);
}

}  // namespace

std::string cleanName(std::string_view name) {
  // sanitizeText cuts on a sequence boundary; trimming afterwards can only shorten the result further.
  return trimmed(sanitizeText(name, kMaxNameBytes));
}

std::string cleanLabel(std::string_view label) { return trimmed(sanitizeText(label, kMaxLabelBytes)); }

bool sameName(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const unsigned char x = static_cast<unsigned char>(a[i]);
    const unsigned char y = static_cast<unsigned char>(b[i]);
    const unsigned char lx = x < 0x80 ? static_cast<unsigned char>(std::tolower(x)) : x;
    const unsigned char ly = y < 0x80 ? static_cast<unsigned char>(std::tolower(y)) : y;
    if (lx != ly) return false;
  }
  return true;
}

bool normalizeEntry(MenuEntry& entry, std::string& reason) {
  if (!entry.commandId.empty() && !isValidIdentifier(entry.commandId, kMaxIdBytes)) {
    reason = "the command id is not valid";
    return false;
  }
  if (!entry.icon.empty() && !isValidIconName(entry.icon)) {
    reason = "the icon name is not valid";
    return false;
  }
  entry.label = cleanLabel(entry.label);
  return true;
}

std::string menuIdFor(uint32_t serial) { return "menu." + std::to_string(serial); }

CustomMenu makeEmptyMenu(MenuKind kind, std::string name) {
  CustomMenu menu;
  menu.kind = kind;
  menu.name = std::move(name);
  if (kind == MenuKind::Pie) menu.entries.resize(static_cast<size_t>(menu.slotCount));
  return menu;
}

bool validateMenu(const CustomMenu& menu, std::string& reason) {
  if (menu.id.empty() || !isValidIdentifier(menu.id, kMaxIdBytes)) {
    reason = "the menu id is not valid";
    return false;
  }
  if (cleanName(menu.name).empty() || cleanName(menu.name) != menu.name) {
    reason = "the menu name is empty or not clean";
    return false;
  }
  if (menu.kind == MenuKind::Pie) {
    if (!isValidPieSlotCount(menu.slotCount)) {
      reason = "a pie menu has 4, 6 or 8 slots";
      return false;
    }
    if (menu.entries.size() != static_cast<size_t>(menu.slotCount)) {
      reason = "a pie menu holds exactly one entry per slot";
      return false;
    }
  } else {
    if (menu.entries.size() > kMaxPanelEntries) {
      reason = "a panel menu holds at most " + std::to_string(kMaxPanelEntries) + " entries";
      return false;
    }
    const PanelSettings& p = menu.panel;
    if (p.columns < kMinColumns || p.columns > kMaxColumns || p.buttonSize < kMinButtonSize || p.buttonSize > kMaxButtonSize) {
      reason = "the panel columns or button size are out of range";
      return false;
    }
    const auto dimensionOk = [](double v) { return v == 0.0 || (std::isfinite(v) && v >= kMinPanelDimension && v <= kMaxPanelDimension); };
    if (!dimensionOk(p.width) || !dimensionOk(p.height)) {
      reason = "the panel size is out of range";
      return false;
    }
  }
  for (const MenuEntry& original : menu.entries) {
    MenuEntry entry = original;
    std::string why;
    if (!normalizeEntry(entry, why) || entry != original) {
      reason = why.empty() ? "an entry is not clean" : why;
      return false;
    }
    if (menu.kind == MenuKind::Panel && entry.commandId.empty()) {
      reason = "a panel entry needs a command";
      return false;
    }
  }
  return true;
}

bool isMissing(const MenuEntry& entry, const CommandExists& exists) {
  return !entry.commandId.empty() && exists && !exists(entry.commandId);
}

}  // namespace r1ui::commands::custommenu
