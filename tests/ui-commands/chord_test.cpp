// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of chords: validity (modifier-only and unmapped keys), canonical modifier order, parse
//   and format round trips for every key, aliases, sequences and key-release triggers, relations used
//   by the conflict rules, code round trips, and hostile text (empty, huge, non-ASCII, control
//   characters, duplicate modifiers, malformed separators).
// Callers: CTest (label fast).
#include "TestSupport.h"
#include "r1ui/commands/Text.h"

namespace {

using namespace r1test;

void testValidity() {
  R1_EXPECT(!chord(Key::Unknown).valid());
  R1_EXPECT(!chord(static_cast<Key>(200)).valid());       // not a key of the enum
  R1_EXPECT(!chord(Key::A, 0x80).valid());                 // unknown modifier bit
  R1_EXPECT(chord(Key::A, Mod::kCtrl | Mod::kShift).valid());
  R1_EXPECT(chord(Key::Escape).valid());
  R1_EXPECT(chord(static_cast<Key>(123)).valid());         // F12
  R1_EXPECT(!chord(static_cast<Key>(124)).valid());
  R1_EXPECT(!ChordSequence{}.valid());
  R1_EXPECT(seq2(kK, Mod::kCtrl, kC, Mod::kCtrl).valid());
  KeyChord up = chord(kK, Mod::kCtrl);
  up.onKeyUp = true;
  R1_EXPECT(ChordSequence::single(up).valid());
  R1_EXPECT(!ChordSequence::pair(up, chord(kC)).valid());  // release triggers only on single chords
  R1_EXPECT(!ChordSequence::pair(chord(kK), chord(Key::Unknown)).valid());
}

void testFormatOrder() {
  // Spec 07 rule 28: Ctrl, Cmd, Alt, Shift.
  R1_EXPECT(formatChord(chord(kS, Mod::kShift | Mod::kAlt | Mod::kMeta | Mod::kCtrl)) == "Ctrl+Cmd+Alt+Shift+S");
  R1_EXPECT(formatChord(chord(kZ, Mod::kCtrl)) == "Ctrl+Z");
  R1_EXPECT(formatChord(chord(Key::Delete)) == "Delete");
  R1_EXPECT(formatChord(chord(Key::PageUp, Mod::kAlt)) == "Alt+Page Up");
  R1_EXPECT(formatChord(chord(Key::F1)) == "F1");
  R1_EXPECT(formatChord(chord(static_cast<Key>(123))) == "F12");
  R1_EXPECT(formatChord(chord(kZ, Mod::kCtrl), true) == "CTRL+Z");
  R1_EXPECT(formatChord(chord(Key::Unknown)).empty());
  R1_EXPECT(formatSequence(ChordSequence{}).empty());
  R1_EXPECT(formatSequence(seq2(kK, Mod::kCtrl, kC, Mod::kCtrl)) == "Ctrl+K, Ctrl+C");
  KeyChord up = chord(kK, Mod::kCtrl);
  up.onKeyUp = true;
  R1_EXPECT(formatChord(up) == "Ctrl+K:up");
}

void testRoundTripAllKeys() {
  int checked = 0;
  for (unsigned code = 0; code < 300; ++code) {
    const Key key = static_cast<Key>(code);
    for (uint8_t mods = 0; mods < 16; ++mods) {
      const KeyChord c = chord(key, mods);
      if (!c.valid()) {
        R1_EXPECT(formatChord(c).empty());
        continue;
      }
      const std::string text = formatChord(c);
      const std::optional<KeyChord> back = parseChord(text);
      R1_EXPECT(back.has_value() && *back == c);
      R1_EXPECT(KeyChord::fromCode(c.code()) == c);
      ++checked;
    }
  }
  R1_EXPECT(checked == (26 + 10 + 12 + 15) * 16);  // letters, digits, F1..F12 and the 15 named keys
}

void testParse() {
  R1_EXPECT(parseChord("ctrl+shift+z") == chord(kZ, Mod::kCtrl | Mod::kShift));
  R1_EXPECT(parseChord("  Shift + Ctrl + Z ") == chord(kZ, Mod::kCtrl | Mod::kShift));
  R1_EXPECT(parseChord("Control+Option+Command+Del") == chord(Key::Delete, Mod::kCtrl | Mod::kAlt | Mod::kMeta));
  R1_EXPECT(parseChord("Esc") == chord(Key::Escape));
  R1_EXPECT(parseChord("pageup") == chord(Key::PageUp));
  R1_EXPECT(parseChord("Page Down") == chord(Key::PageDown));
  R1_EXPECT(parseChord("f5") == chord(static_cast<Key>(116)));
  R1_EXPECT(!parseChord("F0") && !parseChord("F13") && !parseChord("F01") && !parseChord("F100"));
  const std::optional<ChordSequence> pair = parseSequence("Ctrl+K,Ctrl+C");
  R1_EXPECT(pair && *pair == seq2(kK, Mod::kCtrl, kC, Mod::kCtrl));
  const std::optional<KeyChord> up = parseChord("Ctrl+K:UP");
  R1_EXPECT(up && up->onKeyUp);
  R1_EXPECT(!parseSequence("Ctrl+K:up, C"));  // release trigger inside a sequence
}

void testHostileText() {
  for (const char* bad : {"", " ", "+", "Ctrl+", "Ctrl+Ctrl+A", "Ctrl+Shift", "Ctrl", "Shift+", "A+B", "Ctrl++A", "Bogus", "Ctrl+Bogus", "Ctrl+A:down", "Ctrl+A:",
                          "Ctrl+A:up:up", "Ctrl+\x01", "\xC3\xA9", "Ctrl+\xFF", "Ctrl+A\n", "Ctrl+A,B,C", ",", "Ctrl+A,", ",A", "Ctrl+Ctrl"}) {
    R1_EXPECT(!parseSequence(bad));
  }
  R1_EXPECT(!parseChord(std::string(10000, 'a')));
  R1_EXPECT(!parseChord(std::string(kMaxChordTextBytes + 1, ' ') + "A"));
  R1_EXPECT(!parseSequence(std::string(5000, ',')));
  R1_EXPECT(parseChord(std::string(kMaxChordTextBytes - 1, ' ') + "A").has_value());  // exactly at the limit still parses
}

void testRelations() {
  const ChordSequence a = seq(kK, Mod::kCtrl);
  const ChordSequence ab = seq2(kK, Mod::kCtrl, kC, Mod::kCtrl);
  const ChordSequence other = seq(kK);
  R1_EXPECT(relate(a, a) == SequenceRelation::Equal);
  R1_EXPECT(relate(a, ab) == SequenceRelation::APrefixOfB);
  R1_EXPECT(relate(ab, a) == SequenceRelation::BPrefixOfA);
  R1_EXPECT(relate(a, other) == SequenceRelation::Unrelated);
  R1_EXPECT(relate(ab, seq2(kK, Mod::kCtrl, kD, Mod::kCtrl)) == SequenceRelation::Unrelated);
  R1_EXPECT(relate(ChordSequence{}, a) == SequenceRelation::Unrelated);
  KeyChord up = chord(kK, Mod::kCtrl);
  up.onKeyUp = true;
  R1_EXPECT(relate(ChordSequence::single(up), a) == SequenceRelation::Unrelated);  // press and release do not collide
  R1_EXPECT(ChordSequence::fromCode(ab.code()) == ab);
  R1_EXPECT(ChordSequence::fromCode(a.code()) == a);
  R1_EXPECT(ChordSequence::fromCode(0).empty());
}

void testText() {
  R1_EXPECT(isValidUtf8("plain") && isValidUtf8("\xC3\xA9\xF0\x9F\x98\x80"));
  R1_EXPECT(!isValidUtf8("\xC0\x80") && !isValidUtf8("\xED\xA0\x80") && !isValidUtf8("\xF4\x90\x80\x80") && !isValidUtf8("\xE2\x82"));
  R1_EXPECT(sanitizeText("a\xFF" "b", 100) == "a\xEF\xBF\xBD" "b");
  R1_EXPECT(sanitizeText("a\nb\tc\x7F", 100) == "a b c ");
  R1_EXPECT(sanitizeText("\xE2\x82\xAC\xE2\x82\xAC", 4) == "\xE2\x82\xAC");  // cut on a boundary
  R1_EXPECT(sanitizeText(std::string(100000, 'x'), 256).size() == 256);
  R1_EXPECT(isValidIdentifier("edit.undo", 128) && !isValidIdentifier("edit undo", 128) && !isValidIdentifier("", 128) && !isValidIdentifier(std::string(129, 'a'), 128));
  R1_EXPECT(isValidIconName("mouse-pointer") && !isValidIconName("a/b") && !isValidIconName("..") && !isValidIconName(""));
}

}  // namespace

int main() {
  testValidity();
  testFormatOrder();
  testRoundTripAllKeys();
  testParse();
  testHostileText();
  testRelations();
  testText();
  return r1test::finish();
}
