// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the value types of user-created menus (slice 5.15): CustomMenu (a pie menu or a dockable panel
//   of action buttons), its entries and settings, the limits that bound them, the validators for names
//   and entries, and the result type of every editing operation.
// Why: owner requirement 2026-10-10: users create pie menus (up to 8 slots, each an action) and
//   dockable menus (a panel of buttons). The model is plain data that the set (CustomMenuSet.h), the
//   .r1mn file (CustomMenuIo.h), the widgets (pie, panel) and the future creator window share.
// Callers: CustomMenuSet, CustomMenuIo, the widget layer, tests. Calls: Text.h only.
// Pie slots: slot 0 points up and the slots run clockwise at equal angles (8 slots: up, up-right, right,
//   down-right, down, down-left, left, up-left). A pie holds exactly `slotCount` entries (4, 6 or 8);
//   an entry with an empty commandId is an empty slot. A panel holds 0..kMaxPanelEntries entries and an
//   entry always has a command id.
// Entries refer to commands by id only. The model never checks ids against a registry: an unknown id is
//   kept and shown as a missing command (spec 06 rule 35), so a menu loaded after a clean installation
//   keeps its structure until the commands exist again.
// Failure behavior: nothing here throws.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace r1ui::commands::custommenu {

// ---- limits -------------------------------------------------------------------------------------

inline constexpr size_t kMaxMenus = 2000;
inline constexpr size_t kMaxPanelEntries = 256;
inline constexpr size_t kMaxTotalEntries = 100000;  // across the whole set
inline constexpr size_t kMaxNameBytes = 128;
inline constexpr size_t kMaxLabelBytes = 128;
inline constexpr size_t kMaxIdBytes = 128;
inline constexpr int kMinPieSlots = 4;
inline constexpr int kMaxPieSlots = 8;
inline constexpr int kMinColumns = 1;
inline constexpr int kMaxColumns = 12;
inline constexpr int kMinButtonSize = 24;
inline constexpr int kMaxButtonSize = 96;
inline constexpr double kMinPanelDimension = 120.0;
inline constexpr double kMaxPanelDimension = 4000.0;

enum class MenuKind : uint8_t { Pie, Panel };

const char* kindName(MenuKind kind);                       // "pie" | "panel"
bool parseKind(std::string_view text, MenuKind& out);
bool isValidPieSlotCount(int count);                        // 4, 6 or 8

// ---- data ---------------------------------------------------------------------------------------

struct MenuEntry {
  std::string commandId;  // empty = empty pie slot
  std::string label;      // optional override of the command's label ("" = the command's)
  std::string icon;       // optional override of the command's icon ("" = the command's)
  friend bool operator==(const MenuEntry&, const MenuEntry&) = default;
};

// Settings of a panel menu. width and height are the default floating size of its window; 0 = let the
// dock decide.
struct PanelSettings {
  int columns = 3;
  int buttonSize = 40;   // button height in logical pixels
  bool showLabels = true;
  double width = 0.0;
  double height = 0.0;
  friend bool operator==(const PanelSettings&, const PanelSettings&) = default;
};

struct CustomMenu {
  std::string id;          // "menu.<serial>", stable for the life of the menu, never reused
  uint32_t serial = 0;     // numeric form of the id (hosts derive a dock panel id from it)
  MenuKind kind = MenuKind::Pie;
  std::string name;        // unique inside a set (ASCII case-insensitive)
  int slotCount = 8;       // pie only
  std::vector<MenuEntry> entries;
  PanelSettings panel;     // panel only
  friend bool operator==(const CustomMenu&, const CustomMenu&) = default;
};

// ---- validation helpers --------------------------------------------------------------------------

// Name text as stored: invalid UTF-8 replaced, control characters turned into spaces, ends trimmed, cut at
// kMaxNameBytes on a sequence boundary. Empty result = not a usable name.
std::string cleanName(std::string_view name);
// Label or icon override text (same hygiene, may be empty, not trimmed away).
std::string cleanLabel(std::string_view label);
// ASCII case-insensitive equality used for the uniqueness of names.
bool sameName(std::string_view a, std::string_view b);
// Entry hygiene: command id must be empty (pie slot) or a valid identifier, icon empty or a valid icon
// name; label cleaned. Returns false and fills `reason` when the entry cannot be kept.
bool normalizeEntry(MenuEntry& entry, std::string& reason);
// Builds the id "menu.<serial>".
std::string menuIdFor(uint32_t serial);
// Checks a whole menu (kind, counts, settings, entries); the reason names the first problem.
bool validateMenu(const CustomMenu& menu, std::string& reason);
// A pie with all slots empty / a panel with no entries.
CustomMenu makeEmptyMenu(MenuKind kind, std::string name);

// True when the entry's command is not known to `exists`.
using CommandExists = std::function<bool(const std::string&)>;
bool isMissing(const MenuEntry& entry, const CommandExists& exists);

// ---- results ------------------------------------------------------------------------------------

enum class MenuError : uint8_t {
  None,
  UnknownMenu,
  InvalidName,
  DuplicateName,
  InvalidEntry,
  OutOfRange,
  WrongKind,
  LimitReached,
  WouldLoseEntries,
  Invalid
};

struct MenuEditResult {
  bool ok = false;
  MenuError error = MenuError::None;
  std::string reason;  // empty when ok; a sentence fit for a message line
  std::string id;      // the menu created or changed
  size_t index = 0;    // the entry or slot created or moved to
  explicit operator bool() const { return ok; }
};

}  // namespace r1ui::commands::custommenu
