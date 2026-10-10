// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of KeyUsage.h.
// Invariants: reads the Keymap once per call (O(bindings)); never mutates anything.
// Callers: the hotkey editor, tests.
#include "r1ui/commands/keyboard/KeyUsage.h"

#include <algorithm>

namespace r1ui::commands {

std::string categoryOf(const CommandDef& def) { return def.category.empty() ? std::string("General") : def.category; }

KeyUsageMap collectKeyUsage(const CommandRegistry& registry, const Keymap& keymap, const KeyUsageFilter& filter, uint8_t modifiers) {
  KeyUsageMap result;
  std::vector<std::string> contextChain;
  if (!filter.context.empty()) contextChain = registry.contextChain(filter.context);
  keymap.forEachBinding([&](const std::string& context, const ChordSequence& sequence, const Binding& binding) {
    if (sequence.empty() || !sequence.valid()) return;
    const KeyChord& first = sequence.first();
    if (first.modifiers != modifiers) return;
    const CommandDef* def = registry.find(binding.commandId);
    if (def == nullptr) return;
    if (!filter.category.empty() && categoryOf(*def) != filter.category) return;
    if (!filter.context.empty() && std::find(contextChain.begin(), contextChain.end(), context) == contextChain.end()) return;
    KeyUse use;
    use.commandId = binding.commandId;
    use.context = context;
    use.sequence = sequence;
    use.startsSequence = sequence.count == 2;
    result[static_cast<uint16_t>(first.key)].push_back(std::move(use));
  });
  for (auto& entry : result) {
    std::stable_sort(entry.second.begin(), entry.second.end(), [&](const KeyUse& a, const KeyUse& b) {
      const uint64_t sa = registry.serialOf(a.commandId);
      const uint64_t sb = registry.serialOf(b.commandId);
      if (sa != sb) return sa < sb;
      return a.sequence.code() < b.sequence.code();
    });
  }
  return result;
}

}  // namespace r1ui::commands
