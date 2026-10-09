// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Conflicts.h.
// Invariants: findConflicts is read-only; assignChord either changes nothing (refused or invalid) or
//   applies unbinds, the new chord and the slot pin as one batch.
// Callers: KeybindingEditor, tests.
#include "r1ui/commands/Conflicts.h"

#include <algorithm>

namespace r1ui::commands {

namespace {

struct Ranked {
  Conflict conflict;
  size_t rank = 0;
  uint64_t serial = 0;
};

}  // namespace

std::vector<Conflict> findConflicts(const CommandRegistry& registry, const Keymap& keymap, std::string_view commandId, const ChordSequence& proposed) {
  const CommandDef* command = registry.find(commandId);
  if (command == nullptr || !proposed.valid()) return {};
  const std::vector<std::string> ancestors = registry.contextChain(command->context);  // own context first
  const std::vector<std::string> descendants = registry.descendantsOf(command->context);

  std::vector<Ranked> found;
  keymap.forEachBinding([&](const std::string& context, const ChordSequence& existing, const Binding& binding) {
    if (binding.commandId == commandId) return;
    size_t rank = 0;
    ConflictScope scope = ConflictScope::SameContext;
    const auto up = std::find(ancestors.begin(), ancestors.end(), context);
    if (up != ancestors.end()) {
      rank = static_cast<size_t>(up - ancestors.begin());
      scope = rank == 0 ? ConflictScope::SameContext : ConflictScope::Ancestor;
    } else {
      const auto down = std::find(descendants.begin(), descendants.end(), context);
      if (down == descendants.end()) return;  // a sibling or unrelated context: never a conflict
      rank = ancestors.size() + static_cast<size_t>(down - descendants.begin());
      scope = ConflictScope::Descendant;
    }
    const SequenceRelation relation = relate(existing, proposed);
    if (relation == SequenceRelation::Unrelated) return;
    const ConflictKind kind = relation == SequenceRelation::Equal ? ConflictKind::Equal
                              : relation == SequenceRelation::APrefixOfB ? ConflictKind::ExistingIsPrefix
                                                                         : ConflictKind::NewIsPrefix;
    for (int slot = 0; slot < kSlotCount; ++slot) {
      if ((binding.slotMask & (1u << slot)) == 0) continue;
      found.push_back({{binding.commandId, context, slot, kind, scope, existing}, rank, registry.serialOf(binding.commandId)});
    }
  });
  std::sort(found.begin(), found.end(), [](const Ranked& a, const Ranked& b) {
    if (a.rank != b.rank) return a.rank < b.rank;
    if (a.serial != b.serial) return a.serial < b.serial;
    return a.conflict.slot < b.conflict.slot;
  });
  std::vector<Conflict> out;
  out.reserve(found.size());
  for (Ranked& r : found) out.push_back(std::move(r.conflict));
  return out;
}

AssignResult assignChord(KeybindingOverrides& overrides, const Keymap& keymap, const CommandRegistry& registry, std::string_view commandId, int slot,
                         std::optional<ChordSequence> chord, bool overrideConflicts) {
  AssignResult result;
  if (registry.find(commandId) == nullptr) {
    result.error = "unknown command";
    return result;
  }
  if (slot < 0 || slot >= kSlotCount) {
    result.error = "slot must be 0 or 1";
    return result;
  }
  if (chord && !chord->valid()) {
    result.error = "invalid chord";
    return result;
  }
  const int otherSlot = 1 - slot;
  std::optional<ChordSequence> otherValue = keymap.effective(commandId, otherSlot);
  if (chord) result.conflicts = findConflicts(registry, keymap, commandId, *chord);
  if (!result.conflicts.empty() && !overrideConflicts) return result;
  if (otherValue == chord) otherValue.reset();  // the chord moves to this slot

  KeybindingOverrides::Batch batch(overrides);
  for (const Conflict& c : result.conflicts) overrides.set(c.commandId, c.slot, std::nullopt);
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
