// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: collectKeyUsage, the question the hotkey editor's keyboard asks: "which commands are bound to
//   which key when exactly this set of modifiers is held, in this category and context?"
// Why: the keyboard highlights keys that carry a binding for the shown filter; computing that from
//   the Keymap in the command module keeps the widget free of binding logic and lets tests cover the
//   rules (exact modifier match, sequences, filters, ordering) without drawing anything.
// Callers: the hotkey editor (ui-widgets/hotkeys), tests. Calls: CommandRegistry, Keymap.
// Rules: only bindings in force count (the Keymap's resolution, so a default another command displaced
//   is not listed); the modifier set must equal the chord's modifiers exactly; for a two-step sequence
//   the first chord decides the key and the use is flagged `startsSequence`; a category filter keeps
//   commands of that category ("General" matches commands without one); a context filter keeps commands
//   of that context and of its ancestors (what is live in a panel of that context).
//   Order inside a key is registration order, then the sequence code, so results are deterministic.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "r1ui/commands/Keymap.h"

namespace r1ui::commands {

struct KeyUse {
  std::string commandId;
  std::string context;
  ChordSequence sequence;      // the binding as a whole
  bool startsSequence = false; // true when this key is the first step of a two-step sequence
};

struct KeyUsageFilter {
  std::string category;  // empty = all categories
  std::string context;   // empty = all contexts
};

// Key (as an integer) -> its uses; keys without a use are absent.
using KeyUsageMap = std::unordered_map<uint16_t, std::vector<KeyUse>>;

KeyUsageMap collectKeyUsage(const CommandRegistry& registry, const Keymap& keymap, const KeyUsageFilter& filter, uint8_t modifiers);

// The category a command is listed under ("General" when empty).
std::string categoryOf(const CommandDef& def);

}  // namespace r1ui::commands
