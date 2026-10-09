// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Overrides.h.
// Invariants: count_ equals the number of engaged slots in table_; stamps are unique and grow; the
//   version and the registry notification move exactly when the effective table changed.
// Callers: the keybinding editor, the host, OverrideIo, Keymap.
#include "r1ui/commands/Overrides.h"

#include <algorithm>

namespace r1ui::commands {

SetError KeybindingOverrides::set(std::string_view commandId, int slot, std::optional<ChordSequence> chord) {
  if (slot < 0 || slot >= kSlotCount) return SetError::BadSlot;
  if (registry_.find(commandId) == nullptr) return SetError::UnknownCommand;
  if (chord && !chord->valid()) return SetError::InvalidChord;
  const std::string key(commandId);
  auto it = table_.find(key);
  const bool existing = it != table_.end() && it->second[static_cast<size_t>(slot)].has_value();
  if (!existing && count_ >= kMaxOverrides) return SetError::TableFull;
  if (it == table_.end()) it = table_.emplace(key, Slots{}).first;
  std::optional<OverrideEntry>& entry = it->second[static_cast<size_t>(slot)];
  if (entry && entry->chord == chord) return SetError::None;
  if (!entry) ++count_;
  entry = OverrideEntry{std::string(commandId), slot, chord, nextStamp_++};
  changed();
  return SetError::None;
}

bool KeybindingOverrides::clear(std::string_view commandId, int slot) {
  if (slot < 0 || slot >= kSlotCount) return false;
  const auto it = table_.find(std::string(commandId));
  if (it == table_.end() || !it->second[static_cast<size_t>(slot)]) return false;
  it->second[static_cast<size_t>(slot)].reset();
  --count_;
  if (!it->second[0] && !it->second[1]) table_.erase(it);
  changed();
  return true;
}

bool KeybindingOverrides::resetCommand(std::string_view commandId) {
  const auto it = table_.find(std::string(commandId));
  if (it == table_.end()) return false;
  for (const auto& slot : it->second) {
    if (slot) --count_;
  }
  table_.erase(it);
  changed();
  return true;
}

void KeybindingOverrides::resetAll() {
  if (table_.empty()) return;
  table_.clear();
  count_ = 0;
  changed();
}

const OverrideEntry* KeybindingOverrides::find(std::string_view commandId, int slot) const {
  if (slot < 0 || slot >= kSlotCount) return nullptr;
  const auto it = table_.find(std::string(commandId));
  if (it == table_.end()) return nullptr;
  const std::optional<OverrideEntry>& entry = it->second[static_cast<size_t>(slot)];
  return entry ? &*entry : nullptr;
}

std::vector<OverrideEntry> KeybindingOverrides::entries() const {
  std::vector<OverrideEntry> out;
  out.reserve(count_);
  for (const auto& [id, slots] : table_) {
    for (const auto& slot : slots) {
      if (slot) out.push_back(*slot);
    }
  }
  std::sort(out.begin(), out.end(), [](const OverrideEntry& a, const OverrideEntry& b) { return a.stamp < b.stamp; });
  return out;
}

size_t KeybindingOverrides::replaceAll(const std::vector<OverrideEntry>& entries) {
  std::unordered_map<std::string, Slots> next;
  size_t kept = 0;
  uint64_t stamp = nextStamp_;
  for (const OverrideEntry& e : entries) {
    if (e.slot < 0 || e.slot >= kSlotCount || e.commandId.empty() || (e.chord && !e.chord->valid()) || kept >= kMaxOverrides) continue;
    std::optional<OverrideEntry>& target = next[e.commandId][static_cast<size_t>(e.slot)];
    if (!target) ++kept;  // a repeated (command, slot) replaces the earlier one: the later wins
    target = OverrideEntry{e.commandId, e.slot, e.chord, stamp++};
  }
  nextStamp_ = stamp;
  table_ = std::move(next);
  count_ = kept;
  changed();
  return kept;
}

void KeybindingOverrides::changed() {
  ++version_;
  if (batchDepth_ > 0) {
    batchDirty_ = true;
    return;
  }
  registry_.touch();
}

void KeybindingOverrides::endBatch() {
  if (--batchDepth_ == 0 && batchDirty_) {
    batchDirty_ = false;
    registry_.touch();
  }
}

}  // namespace r1ui::commands
