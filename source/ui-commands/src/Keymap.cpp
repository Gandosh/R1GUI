// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Keymap.h: candidate ordering, acceptance with prefix conflicts, queries.
// Invariants: within one table no two bindings are equal and none is a prefix of another; every
//   accepted slot appears in effective_ exactly as stored in its table.
// Callers: CommandRouter, conflict detection, widgets.
#include "r1ui/commands/Keymap.h"

#include <algorithm>

namespace r1ui::commands {

namespace {

struct Candidate {
  const CommandDef* command = nullptr;
  int slot = 0;
  ChordSequence sequence;
  uint64_t stamp = 0;  // overrides only
};

}  // namespace

void Keymap::ensureFresh() const {
  if (builtRegistry_ == registry_.version() && builtOverrides_ == overrides_.version()) return;
  rebuild();
}

// Overrides outrank defaults; among overrides the newest wins; defaults keep registration order.
void Keymap::rebuild() const {
  tables_.clear();
  effective_.clear();
  std::vector<Candidate> overridden;
  std::vector<Candidate> defaults;
  for (const CommandDef* command : registry_.commands()) {
    for (int slot = 0; slot < kSlotCount; ++slot) {
      const OverrideEntry* entry = overrides_.find(command->id, slot);
      if (entry != nullptr) {
        if (entry->chord) overridden.push_back({command, slot, *entry->chord, entry->stamp});
      } else if (!command->defaultChords[static_cast<size_t>(slot)].empty()) {
        defaults.push_back({command, slot, command->defaultChords[static_cast<size_t>(slot)], 0});
      }
    }
  }
  std::stable_sort(overridden.begin(), overridden.end(), [](const Candidate& a, const Candidate& b) { return a.stamp > b.stamp; });
  for (const std::vector<Candidate>* list : {&overridden, &defaults}) {
    for (const Candidate& c : *list) {
      if (accept(tables_[c.command->context], c.sequence, c.command->id, c.slot)) {
        effective_[c.command->id][static_cast<size_t>(c.slot)] = c.sequence;
      }
    }
  }
  builtRegistry_ = registry_.version();
  builtOverrides_ = overrides_.version();
}

bool Keymap::accept(Table& table, const ChordSequence& sequence, const std::string& commandId, int slot) const {
  const auto same = table.exact.find(sequence.code());
  if (same != table.exact.end()) {
    if (same->second.commandId != commandId) return false;
    same->second.slotMask = static_cast<uint8_t>(same->second.slotMask | (1u << slot));  // both slots hold it: one binding
    return true;
  }
  if (sequence.count == 1) {
    if (table.firsts.contains(sequence.chords[0].code())) return false;  // it starts a sequence
  } else {
    if (table.exact.contains(ChordSequence::single(sequence.chords[0]).code())) return false;  // its first step is a binding
  }
  table.exact.emplace(sequence.code(), Binding{commandId, static_cast<uint8_t>(1u << slot)});
  if (sequence.count == 2) ++table.firsts[sequence.chords[0].code()];
  return true;
}

LookupResult Keymap::lookup(std::string_view context, const ChordSequence& typed) const {
  ensureFresh();
  if (!typed.valid()) return {};
  const auto table = tables_.find(std::string(context));
  if (table == tables_.end()) return {};
  const auto exact = table->second.exact.find(typed.code());
  if (exact != table->second.exact.end()) return {LookupKind::Exact, exact->second.commandId};
  if (typed.count == 1 && table->second.firsts.contains(typed.chords[0].code())) return {LookupKind::Prefix, {}};
  return {};
}

std::optional<ChordSequence> Keymap::effective(std::string_view commandId, int slot) const {
  ensureFresh();
  if (slot < 0 || slot >= kSlotCount) return std::nullopt;
  const auto it = effective_.find(std::string(commandId));
  return it == effective_.end() ? std::nullopt : it->second[static_cast<size_t>(slot)];
}

std::string Keymap::displayText(std::string_view commandId, bool upperCase) const {
  for (int slot = 0; slot < kSlotCount; ++slot) {
    const std::optional<ChordSequence> chord = effective(commandId, slot);
    if (chord) return formatSequence(*chord, upperCase);
  }
  return {};
}

void Keymap::forEachBinding(const std::function<void(const std::string&, const ChordSequence&, const Binding&)>& visit) const {
  ensureFresh();
  for (const auto& [context, table] : tables_) {
    for (const auto& [code, binding] : table.exact) visit(context, ChordSequence::fromCode(code), binding);
  }
}

std::vector<Continuation> Keymap::continuations(std::string_view context, const KeyChord& first) const {
  ensureFresh();
  std::vector<Continuation> out;
  const auto table = tables_.find(std::string(context));
  if (table == tables_.end() || !table->second.firsts.contains(first.code())) return out;
  for (const auto& [code, binding] : table->second.exact) {
    const ChordSequence sequence = ChordSequence::fromCode(code);
    if (sequence.count == 2 && sequence.chords[0] == first) out.push_back({sequence.chords[1], binding.commandId});
  }
  return out;
}

size_t Keymap::bindingCount() const {
  ensureFresh();
  size_t total = 0;
  for (const auto& entry : tables_) total += entry.second.exact.size();
  return total;
}

}  // namespace r1ui::commands
