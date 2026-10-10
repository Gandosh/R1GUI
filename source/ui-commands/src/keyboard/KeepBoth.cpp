// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of KeepBoth.h.
// Invariants: either nothing changes or the chord and the other-slot pin are applied as one batch.
// Callers: HotkeyEditor, tests.
#include "r1ui/commands/keyboard/KeepBoth.h"

#include <algorithm>

namespace r1ui::commands {

bool canKeepBoth(const std::vector<Conflict>& conflicts) {
  return std::none_of(conflicts.begin(), conflicts.end(), [](const Conflict& c) { return c.scope == ConflictScope::SameContext; });
}

AssignResult assignKeepingBoth(KeybindingOverrides& overrides, const Keymap& keymap, const CommandRegistry& registry, std::string_view commandId, int slot,
                               const ChordSequence& chord) {
  AssignResult result;
  if (registry.find(commandId) == nullptr) {
    result.error = "unknown command";
    return result;
  }
  if (slot < 0 || slot >= kSlotCount) {
    result.error = "slot must be 0 or 1";
    return result;
  }
  if (!chord.valid()) {
    result.error = "invalid chord";
    return result;
  }
  result.conflicts = findConflicts(registry, keymap, commandId, chord);
  if (!canKeepBoth(result.conflicts)) {
    result.error = "the shortcut is used in the same context";
    return result;
  }
  const int otherSlot = 1 - slot;
  std::optional<ChordSequence> otherValue = keymap.effective(commandId, otherSlot);
  if (otherValue == chord) otherValue.reset();  // the chord moves to this slot
  KeybindingOverrides::Batch batch(overrides);
  const SetError first = overrides.set(commandId, slot, chord);
  const SetError second = overrides.set(commandId, otherSlot, otherValue);
  if (first != SetError::None || second != SetError::None) {
    result.error = "the override table is full";
    return result;
  }
  result.ok = true;
  return result;
}

}  // namespace r1ui::commands
