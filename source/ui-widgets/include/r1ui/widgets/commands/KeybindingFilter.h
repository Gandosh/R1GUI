// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the pure matching rule of the keybinding editor's search box.
// Why: spec 07 rule 41: typing filters rows by the command label and by the assigned chord, each slot
//   matched on its own text; the brief adds category. The rule is data in, bool out, so it is tested
//   without any widget.
// Callers: KeybindingEditor, tests. Calls: nothing.
// Rule: the query is split at whitespace into at most kMaxQueryTerms terms (longer input is cut at
//   kMaxQueryBytes); a row matches when every term occurs, ASCII case-insensitively, in the label,
//   the category, the command id or the text of either chord slot. "ctrl z" therefore finds "Ctrl+Z"
//   and "undo" finds Undo; an empty query matches everything.
#pragma once

#include <cstddef>
#include <string_view>

namespace r1ui::widgets {

inline constexpr size_t kMaxQueryBytes = 256;
inline constexpr size_t kMaxQueryTerms = 16;

struct KeybindingRowText {
  std::string_view label;
  std::string_view category;
  std::string_view id;
  std::string_view chord0;  // text of the primary slot, empty when unbound
  std::string_view chord1;  // text of the alternate slot
};

bool matchesKeybindingQuery(std::string_view query, const KeybindingRowText& row);

}  // namespace r1ui::widgets
