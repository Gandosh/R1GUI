// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of KeyboardLayout.h: the ANSI cap table and the lookups.
// Invariants: the table is built once (function-local static) from string literals, so the
//   string_views in the caps never dangle; lookups are linear over about 90 caps.
// Callers: the hotkey editor's keyboard widget, tests.
#include "r1ui/commands/keyboard/KeyboardLayout.h"

#include <algorithm>
#include <array>

namespace r1ui::commands {

namespace {

constexpr Key keyOf(int code) { return static_cast<Key>(code); }

// Row builder: places caps left to right from x = 0, each with its own width.
struct RowBuilder {
  std::vector<KeyCap>& out;
  float y;
  float x = 0.0f;

  RowBuilder& gap(float w) {
    x += w;
    return *this;
  }
  RowBuilder& cap(std::string_view id, std::string_view label, Key key, float w = 1.0f, ModifierRole role = ModifierRole::None) {
    out.push_back({id, label, key, role, x, y, w, 1.0f});
    x += w;
    return *this;
  }
};

std::vector<KeyCap> buildAnsi() {
  std::vector<KeyCap> caps;
  caps.reserve(96);

  // Row 0: Escape, then three groups of four function keys.
  {
    RowBuilder r{caps, 0.0f};
    r.cap("esc", "Esc", Key::Escape);
    r.gap(1.0f);
    static constexpr std::array<std::string_view, 12> kFnIds{"f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12"};
    static constexpr std::array<std::string_view, 12> kFnLabels{"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"};
    for (int i = 0; i < 12; ++i) {
      if (i > 0 && i % 4 == 0) r.gap(0.5f);
      r.cap(kFnIds[static_cast<size_t>(i)], kFnLabels[static_cast<size_t>(i)], keyOf(static_cast<int>(Key::F1) + i));
    }
  }

  // Row 1: grave, digits, minus, equal, Backspace. Punctuation caps have no Key code.
  {
    RowBuilder r{caps, 1.5f};
    r.cap("grave", "`", Key::Unknown);
    static constexpr std::array<std::string_view, 10> kDigitIds{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
    for (int i = 0; i < 10; ++i) r.cap(kDigitIds[static_cast<size_t>(i)], kDigitIds[static_cast<size_t>(i)], keyOf(static_cast<int>(Key::Digit0) + (i + 1) % 10));
    r.cap("minus", "-", Key::Unknown);
    r.cap("equal", "=", Key::Unknown);
    r.cap("backspace", "Backspace", Key::Backspace, 2.0f);
  }

  // Letter rows: a table of the letters per row keeps the code short.
  struct Letter {
    std::string_view id;
    std::string_view label;
  };
  const auto letter = [](char c) { return keyOf(c); };
  {
    RowBuilder r{caps, 2.5f};
    r.cap("tab", "Tab", Key::Tab, 1.5f);
    static constexpr std::array<Letter, 10> kRow{{{"q", "Q"}, {"w", "W"}, {"e", "E"}, {"r", "R"}, {"t", "T"}, {"y", "Y"}, {"u", "U"}, {"i", "I"}, {"o", "O"}, {"p", "P"}}};
    for (const Letter& l : kRow) r.cap(l.id, l.label, letter(l.label[0]));
    r.cap("lbracket", "[", Key::Unknown);
    r.cap("rbracket", "]", Key::Unknown);
    r.cap("backslash", "\\", Key::Unknown, 1.5f);
  }
  {
    RowBuilder r{caps, 3.5f};
    r.cap("caps", "Caps", Key::Unknown, 1.75f);
    static constexpr std::array<Letter, 9> kRow{{{"a", "A"}, {"s", "S"}, {"d", "D"}, {"f", "F"}, {"g", "G"}, {"h", "H"}, {"j", "J"}, {"k", "K"}, {"l", "L"}}};
    for (const Letter& l : kRow) r.cap(l.id, l.label, letter(l.label[0]));
    r.cap("semicolon", ";", Key::Unknown);
    r.cap("quote", "'", Key::Unknown);
    r.cap("enter", "Enter", Key::Enter, 2.25f);
  }
  {
    RowBuilder r{caps, 4.5f};
    r.cap("lshift", "Shift", Key::Unknown, 2.25f, ModifierRole::Shift);
    static constexpr std::array<Letter, 7> kRow{{{"z", "Z"}, {"x", "X"}, {"c", "C"}, {"v", "V"}, {"b", "B"}, {"n", "N"}, {"m", "M"}}};
    for (const Letter& l : kRow) r.cap(l.id, l.label, letter(l.label[0]));
    r.cap("comma", ",", Key::Unknown);
    r.cap("period", ".", Key::Unknown);
    r.cap("slash", "/", Key::Unknown);
    r.cap("rshift", "Shift", Key::Unknown, 2.75f, ModifierRole::Shift);
  }
  {
    RowBuilder r{caps, 5.5f};
    r.cap("lctrl", "Ctrl", Key::Unknown, 1.25f, ModifierRole::Ctrl);
    r.cap("lmeta", "Meta", Key::Unknown, 1.25f, ModifierRole::Meta);
    r.cap("lalt", "Alt", Key::Unknown, 1.25f, ModifierRole::Alt);
    r.cap("space", "Space", Key::Space, 6.25f);
    r.cap("ralt", "Alt", Key::Unknown, 1.25f, ModifierRole::Alt);
    r.cap("rmeta", "Meta", Key::Unknown, 1.25f, ModifierRole::Meta);
    r.cap("menu", "Menu", Key::Unknown, 1.25f);
    r.cap("rctrl", "Ctrl", Key::Unknown, 1.25f, ModifierRole::Ctrl);
  }

  // Navigation block (x = 15.5) and arrows.
  {
    RowBuilder r{caps, 1.5f, 15.5f};
    r.cap("insert", "Ins", Key::Insert).cap("home", "Home", Key::Home).cap("pageup", "PgUp", Key::PageUp);
    RowBuilder r2{caps, 2.5f, 15.5f};
    r2.cap("delete", "Del", Key::Delete).cap("end", "End", Key::End).cap("pagedown", "PgDn", Key::PageDown);
    RowBuilder r3{caps, 4.5f, 16.5f};
    r3.cap("up", "Up", Key::Up);
    RowBuilder r4{caps, 5.5f, 15.5f};
    r4.cap("left", "Left", Key::Left).cap("down", "Down", Key::Down).cap("right", "Right", Key::Right);
  }
  return caps;
}

}  // namespace

KeyboardLayout::KeyboardLayout(std::vector<KeyCap> caps) : caps_(std::move(caps)) {
  for (const KeyCap& c : caps_) {
    width_ = std::max(width_, c.x + c.w);
    height_ = std::max(height_, c.y + c.h);
  }
}

const KeyCap* KeyboardLayout::findByKey(Key key) const {
  if (!isRealKey(key)) return nullptr;
  for (const KeyCap& c : caps_) {
    if (c.key == key) return &c;
  }
  return nullptr;
}

const KeyCap* KeyboardLayout::findById(std::string_view id) const {
  for (const KeyCap& c : caps_) {
    if (c.id == id) return &c;
  }
  return nullptr;
}

int KeyboardLayout::capAt(float x, float y) const {
  for (size_t i = 0; i < caps_.size(); ++i) {
    const KeyCap& c = caps_[i];
    if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) return static_cast<int>(i);
  }
  return -1;
}

const KeyboardLayout& ansiKeyboardLayout() {
  static const KeyboardLayout layout(buildAnsi());
  return layout;
}

uint8_t modifierBit(ModifierRole role) {
  switch (role) {
    case ModifierRole::Shift: return Mod::kShift;
    case ModifierRole::Ctrl: return Mod::kCtrl;
    case ModifierRole::Alt: return Mod::kAlt;
    case ModifierRole::Meta: return Mod::kMeta;
    case ModifierRole::None: break;
  }
  return 0;
}

}  // namespace r1ui::commands
