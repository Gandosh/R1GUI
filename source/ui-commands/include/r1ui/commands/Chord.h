// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the key vocabulary of the command module: KeyChord (one key plus modifiers, optionally
//   triggered on key release), ChordSequence (one chord or a two-step sequence), their text form
//   (parse and display) and the relation test the conflict rules use.
// Why: spec 07 rules 7 and 28 and decision D14: a chord is valid only for a real, non-modifier key;
//   modifiers print in the order Ctrl, Cmd, Alt, Shift joined by '+'; sequences of up to two chords
//   are a toolkit addition whose conflicts include prefixes (a single chord A conflicts with a
//   sequence A, B because A would fire before B can be typed).
// Callers: CommandRegistry (default chords), Keymap, CommandRouter, overrides and their JSON file,
//   widgets (menu shortcut text, tooltips, the keybinding editor). Calls: core events (Key, Mod).
// Text form: "Ctrl+Shift+Z"; a sequence is "Ctrl+K, Ctrl+C"; a key-release trigger appends ":up" to a
//   single chord ("Ctrl+K:up"). Parsing is case-insensitive, accepts the aliases Control, Option,
//   Command, Meta, Win, Esc, Return, Del, Ins, PageUp, PageDown, rejects duplicate modifiers, unknown
//   names, input over 128 bytes and non-ASCII bytes. Platform-neutral: the Cmd label is the meta key.
// Keys: the toolkit's Key enum: letters, digits, F1..F12, arrows, Home, End, Page Up/Down, Insert,
//   Delete, Backspace, Tab, Enter, Escape, Space. Key::Unknown (modifier-only presses and unmapped
//   keys) is never a valid chord.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "r1ui/core/events/Event.h"

namespace r1ui::commands {

using Key = core::events::Key;
namespace Mod = core::events::Mod;

inline constexpr size_t kMaxChordTextBytes = 128;
inline constexpr uint8_t kAllModifiers = Mod::kShift | Mod::kCtrl | Mod::kAlt | Mod::kMeta;

// True for the keys of the Key enum that can form a chord (everything but Unknown and unmapped values).
bool isRealKey(Key key);

struct KeyChord {
  Key key = Key::Unknown;
  uint8_t modifiers = Mod::kNone;
  bool onKeyUp = false;  // fires on release instead of press

  bool valid() const { return isRealKey(key) && (modifiers & ~kAllModifiers) == 0; }
  // Unique 32-bit identity of a valid chord (never 0).
  uint32_t code() const;
  static KeyChord fromCode(uint32_t code);
  friend bool operator==(const KeyChord&, const KeyChord&) = default;
};

struct ChordSequence {
  std::array<KeyChord, 2> chords{};
  uint8_t count = 0;  // 0 = unbound

  static ChordSequence single(KeyChord chord) { return {{chord, KeyChord{}}, 1}; }
  static ChordSequence pair(KeyChord first, KeyChord second) { return {{first, second}, 2}; }

  bool empty() const { return count == 0; }
  // One or two valid chords; a key-release trigger is allowed only on a single chord.
  bool valid() const;
  const KeyChord& first() const { return chords[0]; }
  // Unique 64-bit identity of a valid sequence (first code in the low half).
  uint64_t code() const;
  static ChordSequence fromCode(uint64_t code);
  friend bool operator==(const ChordSequence& a, const ChordSequence& b) {
    return a.count == b.count && a.chords[0] == b.chords[0] && (a.count < 2 || a.chords[1] == b.chords[1]);
  }
};

// Text -> value; nullopt for any malformed input (see the header comment). An empty text is not a chord.
std::optional<KeyChord> parseChord(std::string_view text);
std::optional<ChordSequence> parseSequence(std::string_view text);

// Value -> display text. Empty for an invalid chord or an unbound sequence. `upperCase` converts the
// whole text (menus of the reference show capitals).
std::string formatChord(const KeyChord& chord, bool upperCase = false);
std::string formatSequence(const ChordSequence& sequence, bool upperCase = false);

enum class SequenceRelation : uint8_t {
  Unrelated,
  Equal,
  APrefixOfB,  // a is a proper prefix of b (a single chord against a sequence that starts with it)
  BPrefixOfA
};
SequenceRelation relate(const ChordSequence& a, const ChordSequence& b);

}  // namespace r1ui::commands
