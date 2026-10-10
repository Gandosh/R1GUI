// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: KeyboardLayout, the headless description of an ANSI keyboard: every key cap with a stable id,
//   a display label, the toolkit Key it produces (or Unknown for caps the Key enum cannot name), its
//   modifier role and its rectangle in key units (1.0 = one standard key).
// Why: the hotkey editor draws a keyboard and must tell the user which keys carry a binding; making
//   the geometry data keeps the drawing, the hit test and the tests independent of any widget, and
//   gives one place that maps a Key code to its cap.
// Callers: the hotkey editor's keyboard widget (ui-widgets/hotkeys), tests. Calls: Chord.h (Key).
// Invariants (checked by tests): ids are unique; every Key the toolkit can bind (isRealKey) appears on
//   exactly one cap; caps never overlap; left and right modifier caps share the role but have their own
//   ids. Caps whose key is Key::Unknown (punctuation, Caps Lock, Menu) are drawn but cannot hold a
//   binding because the Key enum has no code for them.
// Units: x grows right, y down; the main block is 15 units wide, the navigation cluster starts at 15.5.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "r1ui/commands/Chord.h"

namespace r1ui::commands {

enum class ModifierRole : uint8_t { None, Shift, Ctrl, Alt, Meta };

struct KeyCap {
  std::string_view id;     // stable, lower-case ("esc", "f1", "a", "lshift")
  std::string_view label;  // what is printed on the cap
  Key key = Key::Unknown;  // the key it produces; Unknown for modifiers and unnameable caps
  ModifierRole modifier = ModifierRole::None;
  float x = 0.0f;
  float y = 0.0f;
  float w = 1.0f;
  float h = 1.0f;

  // A cap can carry a chord only when it has a real key.
  bool bindable() const { return isRealKey(key); }
};

class KeyboardLayout {
 public:
  const std::vector<KeyCap>& caps() const { return caps_; }
  // Overall size in key units (max right and bottom edge).
  float width() const { return width_; }
  float height() const { return height_; }

  // The cap producing `key`, or nullptr (Unknown and unmapped values have none).
  const KeyCap* findByKey(Key key) const;
  const KeyCap* findById(std::string_view id) const;
  // Index into caps() of the cap containing the point (key units), or -1.
  int capAt(float x, float y) const;

  explicit KeyboardLayout(std::vector<KeyCap> caps);

 private:
  std::vector<KeyCap> caps_;
  float width_ = 0.0f;
  float height_ = 0.0f;
};

// The built-in ANSI layout: Escape and F1-F12, number row, three letter rows with Tab, Caps Lock, Enter,
// both Shift, Ctrl, Meta and Alt keys, Space, Menu, the six-key navigation block and the arrow cluster.
// The numeric keypad is not drawn (the Key enum has no keypad codes).
const KeyboardLayout& ansiKeyboardLayout();

// Modifier bit (Mod::kShift ...) of a role; 0 for None.
uint8_t modifierBit(ModifierRole role);

}  // namespace r1ui::commands
