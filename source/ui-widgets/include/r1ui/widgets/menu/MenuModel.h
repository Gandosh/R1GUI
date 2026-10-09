// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the data model of menus (what the caller describes): item kinds, tone, the item and menu
//   specifications, the text limits that protect the widgets, and the chord text formatter of
//   spec 07 rules 27 to 29.
// Why: a menu is declared as data (like a command table) and the widgets built from it are an
//   implementation detail, so a menu bar, a context menu and a submenu share one description and the
//   caller never touches widget ids. Shortcut text is plain text in the spec; formatChordText()
//   builds it from a key and modifiers in the order Ctrl, Cmd, Alt, Shift so every menu and tooltip
//   shows the same chord format.
// Callers: MenuController, MenuBar, MenuPanel (read), application code (builds specs). Calls: Event.h
//   (Key and Mod values) only.
// Boundaries: labels, descriptions, shortcut and tooltip texts are cut to kMaxMenuTextBytes at a
//   UTF-8 sequence boundary and invalid UTF-8 is replaced by U+FFFD (sanitizeMenuText), a menu holds
//   at most kMaxMenuItems entries per level (extra entries are ignored) and submenus nest at most
//   kMaxMenuDepth levels.
// Activation: an enabled Action, Check or Radio item runs its own onActivate, or the menu's
//   onCommand when it has none, then the whole stack closes unless keepOpen is set (spec 10 rule 26).
//   Check items toggle and Radio items select among the contiguous radio items around them before
//   the callback runs, so the callback sees the new state in item.checked.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/core/events/Event.h"

namespace r1ui::widgets {

inline constexpr size_t kMaxMenuTextBytes = 2048;
inline constexpr size_t kMaxMenuItems = 2000;
inline constexpr int kMaxMenuDepth = 8;

enum class MenuItemKind : uint8_t { Action, Check, Radio, Separator, Heading, Submenu };
// Component tone colours an item with the component colour (spec 2.9).
enum class MenuTone : uint8_t { Default, Component };

struct MenuItemSpec {
  MenuItemKind kind = MenuItemKind::Action;
  std::string id;           // command id handed to the callbacks
  std::string label;
  std::string description;  // optional second line (described items)
  std::string icon;         // icon name (assets/icons) or empty
  std::string shortcut;     // right-aligned text, already formatted (see formatChordText)
  std::string tooltip;      // description shown by the tooltip; the shortcut is appended in brackets
  bool enabled = true;
  bool checked = false;     // Check and Radio
  bool keepOpen = false;    // activation leaves the menu open
  MenuTone tone = MenuTone::Default;
  std::vector<MenuItemSpec> children;  // Submenu
  std::function<void(const MenuItemSpec&)> onActivate;
};

// Look differences between the menus of the reference that are not per row.
struct MenuLook {
  // Draws the arrow of submenu rows as a 14 px muted single angle quotation mark (the canvas context
  // menu of the reference, rows 32 px high) instead of a 12 px chevron icon (menu bar menus, 28 px).
  bool arrowGlyph = false;
  double iconSize = 12.0;  // icon box of a row (the toolbar flyout uses 16)
  double iconGap = 8.0;    // between a row's icon and its label (the toolbar flyout uses 20)
  double submenuMinWidth = 0.0;  // minimum outer width of every submenu (menu bar menus: 174)
};

struct MenuSpec {
  std::vector<MenuItemSpec> items;
  std::function<void(const MenuItemSpec&)> onCommand;  // used by items without their own onActivate
  double minWidth = 0.0;                               // logical px
  MenuLook look;
};

// ---- builders (keep call sites short) ----
MenuItemSpec menuAction(std::string id, std::string label, std::string shortcut = {}, std::string icon = {});
MenuItemSpec menuCheck(std::string id, std::string label, bool checked, std::string shortcut = {});
MenuItemSpec menuRadio(std::string id, std::string label, bool checked);
MenuItemSpec menuSeparator();
MenuItemSpec menuHeading(std::string label);
MenuItemSpec menuSubmenu(std::string label, std::vector<MenuItemSpec> children, std::string icon = {});

// Cuts to `maxBytes` on a UTF-8 boundary and replaces invalid sequences with U+FFFD (shared by the
// popup widgets: tooltips, dialogs and toasts sanitise their texts with it).
std::string sanitizeUtf8(std::string_view text, size_t maxBytes);
// sanitizeUtf8 with kMaxMenuTextBytes.
std::string sanitizeMenuText(std::string_view text);

// ---- chord text (spec 07 rule 28) ----
// Modifiers appear in the order Ctrl, Cmd, Alt, Shift, each followed by '+', then the key name; only
// set modifiers appear. `upperCase` converts the result to upper case (spec 07 rule 27 shows menu
// shortcuts in capitals; the reference application shows mixed case, so the default is false). A key
// the formatter has no name for yields an empty string (an unbound chord shows nothing).
std::string formatChordText(core::events::Key key, uint8_t modifiers, bool upperCase = false);
// "<description> (<chord>)" or the description alone when the chord is empty (spec 07 rule 29).
std::string tooltipWithShortcut(std::string_view description, std::string_view chordText);

}  // namespace r1ui::widgets
