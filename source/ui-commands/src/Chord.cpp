// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Chord.h: key names, parse, display and sequence relations.
// Invariants: format(parse(t)) is canonical and parse(format(c)) == c for every valid chord; parsing
//   never reads beyond kMaxChordTextBytes and never throws.
// Callers: everything in ui-commands that touches chords; the widgets for display text.
#include "r1ui/commands/Chord.h"

#include <cctype>

namespace r1ui::commands {

namespace {

struct NamedKey {
  std::string_view name;
  Key key;
};

// Canonical display names first; aliases (parse only) follow their canonical entry.
constexpr NamedKey kNamedKeys[] = {
    {"Backspace", Key::Backspace}, {"Tab", Key::Tab},       {"Enter", Key::Enter},   {"Return", Key::Enter},   {"Escape", Key::Escape},
    {"Esc", Key::Escape},          {"Space", Key::Space},   {"Page Up", Key::PageUp}, {"PageUp", Key::PageUp}, {"Page Down", Key::PageDown},
    {"PageDown", Key::PageDown},   {"End", Key::End},       {"Home", Key::Home},     {"Left", Key::Left},     {"Up", Key::Up},
    {"Right", Key::Right},         {"Down", Key::Down},     {"Insert", Key::Insert}, {"Ins", Key::Insert},    {"Delete", Key::Delete},
    {"Del", Key::Delete}};

bool equalFolded(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

std::string keyName(Key key) {
  const auto code = static_cast<unsigned>(key);
  if ((code >= 65 && code <= 90) || (code >= 48 && code <= 57)) return std::string(1, static_cast<char>(code));
  if (code >= 112 && code <= 123) return "F" + std::to_string(code - 111);
  for (const NamedKey& entry : kNamedKeys) {
    if (entry.key == key) return std::string(entry.name);  // the first entry per key is canonical
  }
  return {};
}

std::optional<Key> parseKeyName(std::string_view name) {
  if (name.size() == 1) {
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return static_cast<Key>(c);
  }
  if ((name.size() == 2 || name.size() == 3) && (name[0] == 'F' || name[0] == 'f')) {
    unsigned number = 0;
    for (size_t i = 1; i < name.size(); ++i) {
      if (name[i] < '0' || name[i] > '9') return std::nullopt;
      number = number * 10 + static_cast<unsigned>(name[i] - '0');
    }
    if (number >= 1 && number <= 12 && name[1] != '0') return static_cast<Key>(111 + number);
    return std::nullopt;
  }
  for (const NamedKey& entry : kNamedKeys) {
    if (equalFolded(entry.name, name)) return entry.key;
  }
  return std::nullopt;
}

// Modifier bit of a token, or 0 when the token is not a modifier name.
uint8_t modifierOf(std::string_view token) {
  if (equalFolded(token, "ctrl") || equalFolded(token, "control")) return Mod::kCtrl;
  if (equalFolded(token, "shift")) return Mod::kShift;
  if (equalFolded(token, "alt") || equalFolded(token, "option")) return Mod::kAlt;
  if (equalFolded(token, "cmd") || equalFolded(token, "command") || equalFolded(token, "meta") || equalFolded(token, "win")) return Mod::kMeta;
  return 0;
}

bool printableAscii(std::string_view text) {
  for (const char c : text) {
    if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) return false;
  }
  return true;
}

}  // namespace

bool isRealKey(Key key) {
  const auto code = static_cast<unsigned>(key);
  if ((code >= 48 && code <= 57) || (code >= 65 && code <= 90) || (code >= 112 && code <= 123)) return true;
  return !keyName(key).empty();
}

uint32_t KeyChord::code() const {
  return static_cast<uint32_t>(key) | (static_cast<uint32_t>(modifiers & kAllModifiers) << 16) | (onKeyUp ? (1u << 24) : 0u);
}

KeyChord KeyChord::fromCode(uint32_t code) {
  KeyChord chord;
  chord.key = static_cast<Key>(code & 0xFFFFu);
  chord.modifiers = static_cast<uint8_t>((code >> 16) & 0xFFu);
  chord.onKeyUp = (code & (1u << 24)) != 0;
  return chord;
}

ChordSequence ChordSequence::fromCode(uint64_t code) {
  ChordSequence sequence;
  const uint32_t first = static_cast<uint32_t>(code & 0xFFFFFFFFu);
  const uint32_t second = static_cast<uint32_t>(code >> 32);
  if (first == 0) return sequence;
  sequence.chords[0] = KeyChord::fromCode(first);
  sequence.count = 1;
  if (second != 0) {
    sequence.chords[1] = KeyChord::fromCode(second);
    sequence.count = 2;
  }
  return sequence;
}

bool ChordSequence::valid() const {
  if (count < 1 || count > 2) return false;
  for (uint8_t i = 0; i < count; ++i) {
    if (!chords[i].valid()) return false;
  }
  return count == 1 || (!chords[0].onKeyUp && !chords[1].onKeyUp);
}

uint64_t ChordSequence::code() const {
  const uint64_t second = count >= 2 ? chords[1].code() : 0u;
  return static_cast<uint64_t>(chords[0].code()) | (second << 32);
}

std::optional<KeyChord> parseChord(std::string_view text) {
  if (text.size() > kMaxChordTextBytes) return std::nullopt;  // bounded before any scan
  text = trim(text);
  if (text.empty() || !printableAscii(text)) return std::nullopt;
  KeyChord chord;
  const size_t colon = text.rfind(':');
  if (colon != std::string_view::npos) {
    if (!equalFolded(trim(text.substr(colon + 1)), "up")) return std::nullopt;
    chord.onKeyUp = true;
    text = trim(text.substr(0, colon));
  }
  // Tokens are separated by '+'; every token but the last must be a modifier, each at most once.
  while (true) {
    const size_t plus = text.find('+');
    if (plus == std::string_view::npos) break;
    const uint8_t bit = modifierOf(trim(text.substr(0, plus)));
    if (bit == 0 || (chord.modifiers & bit) != 0) return std::nullopt;
    chord.modifiers = static_cast<uint8_t>(chord.modifiers | bit);
    text = text.substr(plus + 1);
  }
  const std::optional<Key> key = parseKeyName(trim(text));
  if (!key) return std::nullopt;
  chord.key = *key;
  return chord;
}

std::optional<ChordSequence> parseSequence(std::string_view text) {
  if (text.size() > kMaxChordTextBytes) return std::nullopt;
  text = trim(text);
  if (text.empty()) return std::nullopt;
  const size_t comma = text.find(',');
  if (comma == std::string_view::npos) {
    const std::optional<KeyChord> chord = parseChord(text);
    if (!chord) return std::nullopt;
    return ChordSequence::single(*chord);
  }
  if (text.find(',', comma + 1) != std::string_view::npos) return std::nullopt;  // at most two steps
  const std::optional<KeyChord> first = parseChord(text.substr(0, comma));
  const std::optional<KeyChord> second = parseChord(text.substr(comma + 1));
  if (!first || !second) return std::nullopt;
  const ChordSequence sequence = ChordSequence::pair(*first, *second);
  if (!sequence.valid()) return std::nullopt;  // key-release triggers are not allowed in sequences
  return sequence;
}

std::string formatChord(const KeyChord& chord, bool upperCase) {
  if (!chord.valid()) return {};
  std::string text;
  if ((chord.modifiers & Mod::kCtrl) != 0) text += "Ctrl+";
  if ((chord.modifiers & Mod::kMeta) != 0) text += "Cmd+";
  if ((chord.modifiers & Mod::kAlt) != 0) text += "Alt+";
  if ((chord.modifiers & Mod::kShift) != 0) text += "Shift+";
  text += keyName(chord.key);
  if (chord.onKeyUp) text += ":up";
  if (upperCase) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return text;
}

std::string formatSequence(const ChordSequence& sequence, bool upperCase) {
  if (!sequence.valid()) return {};
  std::string text = formatChord(sequence.chords[0], upperCase);
  if (sequence.count == 2) text += ", " + formatChord(sequence.chords[1], upperCase);
  return text;
}

SequenceRelation relate(const ChordSequence& a, const ChordSequence& b) {
  if (a.empty() || b.empty()) return SequenceRelation::Unrelated;
  if (a == b) return SequenceRelation::Equal;
  if (a.chords[0] != b.chords[0]) return SequenceRelation::Unrelated;
  if (a.count == 1 && b.count == 2) return SequenceRelation::APrefixOfB;
  if (a.count == 2 && b.count == 1) return SequenceRelation::BPrefixOfA;
  return SequenceRelation::Unrelated;
}

}  // namespace r1ui::commands
