// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of MenuModel.h: the item builders, text sanitising and chord formatting.
// Invariants: sanitizeUtf8 always returns well-formed UTF-8 no longer than maxBytes;
//   formatChordText never throws and is a pure function of its arguments.
// Callers: MenuController, MenuPanel, MenuBar, application code.
#include "r1ui/widgets/menu/MenuModel.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace r1ui::widgets {

namespace events = core::events;

MenuItemSpec menuAction(std::string id, std::string label, std::string shortcut, std::string icon) {
  MenuItemSpec spec;
  spec.id = std::move(id);
  spec.label = std::move(label);
  spec.shortcut = std::move(shortcut);
  spec.icon = std::move(icon);
  return spec;
}

MenuItemSpec menuCheck(std::string id, std::string label, bool checked, std::string shortcut) {
  MenuItemSpec spec = menuAction(std::move(id), std::move(label), std::move(shortcut));
  spec.kind = MenuItemKind::Check;
  spec.checked = checked;
  return spec;
}

MenuItemSpec menuRadio(std::string id, std::string label, bool checked) {
  MenuItemSpec spec = menuAction(std::move(id), std::move(label));
  spec.kind = MenuItemKind::Radio;
  spec.checked = checked;
  return spec;
}

MenuItemSpec menuSeparator() {
  MenuItemSpec spec;
  spec.kind = MenuItemKind::Separator;
  return spec;
}

MenuItemSpec menuHeading(std::string label) {
  MenuItemSpec spec;
  spec.kind = MenuItemKind::Heading;
  spec.label = std::move(label);
  return spec;
}

MenuItemSpec menuSubmenu(std::string label, std::vector<MenuItemSpec> children, std::string icon) {
  MenuItemSpec spec = menuAction({}, std::move(label), {}, std::move(icon));
  spec.kind = MenuItemKind::Submenu;
  spec.children = std::move(children);
  return spec;
}

// ---- text sanitising ----------------------------------------------------------------------------

namespace {

// Length of the well-formed UTF-8 sequence at the start of `s`, or 0 when it is malformed
// (overlong forms, surrogates, values above U+10FFFF and truncated sequences are malformed).
size_t validSequenceLength(std::string_view s) {
  const auto byte = [&](size_t i) { return static_cast<unsigned char>(s[i]); };
  const unsigned char b0 = byte(0);
  if (b0 < 0x80) return 1;
  const auto cont = [&](size_t i) { return i < s.size() && (byte(i) & 0xC0) == 0x80; };
  if (b0 >= 0xC2 && b0 <= 0xDF) return cont(1) ? 2 : 0;
  if (b0 >= 0xE0 && b0 <= 0xEF) {
    if (!cont(1) || !cont(2)) return 0;
    if (b0 == 0xE0 && byte(1) < 0xA0) return 0;   // overlong
    if (b0 == 0xED && byte(1) >= 0xA0) return 0;   // surrogate
    return 3;
  }
  if (b0 >= 0xF0 && b0 <= 0xF4) {
    if (!cont(1) || !cont(2) || !cont(3)) return 0;
    if (b0 == 0xF0 && byte(1) < 0x90) return 0;    // overlong
    if (b0 == 0xF4 && byte(1) >= 0x90) return 0;   // above U+10FFFF
    return 4;
  }
  return 0;
}

}  // namespace

std::string sanitizeUtf8(std::string_view text, size_t maxBytes) {
  static constexpr std::string_view kReplacement = "\xEF\xBF\xBD";
  std::string out;
  out.reserve(std::min(text.size(), maxBytes));
  size_t i = 0;
  while (i < text.size()) {
    size_t length = validSequenceLength(text.substr(i));
    const std::string_view piece = length != 0 ? text.substr(i, length) : kReplacement;
    if (length == 0) length = 1;
    if (out.size() + piece.size() > maxBytes) break;
    out.append(piece);
    i += length;
  }
  return out;
}

std::string sanitizeMenuText(std::string_view text) { return sanitizeUtf8(text, kMaxMenuTextBytes); }

// ---- chord text ---------------------------------------------------------------------------------

namespace {

std::string keyName(events::Key key) {
  const auto code = static_cast<unsigned>(key);
  if (code >= 65 && code <= 90) return std::string(1, static_cast<char>(code));
  if (code >= 48 && code <= 57) return std::string(1, static_cast<char>(code));
  if (code >= 112 && code <= 123) return "F" + std::to_string(code - 111);
  switch (key) {
    case events::Key::Backspace: return "Backspace";
    case events::Key::Tab: return "Tab";
    case events::Key::Enter: return "Enter";
    case events::Key::Escape: return "Escape";
    case events::Key::Space: return "Space";
    case events::Key::PageUp: return "Page Up";
    case events::Key::PageDown: return "Page Down";
    case events::Key::End: return "End";
    case events::Key::Home: return "Home";
    case events::Key::Left: return "Left";
    case events::Key::Up: return "Up";
    case events::Key::Right: return "Right";
    case events::Key::Down: return "Down";
    case events::Key::Insert: return "Insert";
    case events::Key::Delete: return "Delete";
    default: return {};
  }
}

}  // namespace

std::string formatChordText(events::Key key, uint8_t modifiers, bool upperCase) {
  const std::string name = keyName(key);
  if (name.empty()) return {};
  std::string text;
  if ((modifiers & events::Mod::kCtrl) != 0) text += "Ctrl+";
  if ((modifiers & events::Mod::kMeta) != 0) text += "Cmd+";
  if ((modifiers & events::Mod::kAlt) != 0) text += "Alt+";
  if ((modifiers & events::Mod::kShift) != 0) text += "Shift+";
  text += name;
  if (upperCase) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return text;
}

std::string tooltipWithShortcut(std::string_view description, std::string_view chordText) {
  std::string text(description);
  if (chordText.empty()) return text;
  if (!text.empty()) text += ' ';
  text += '(';
  text += chordText;
  text += ')';
  return text;
}

}  // namespace r1ui::widgets
