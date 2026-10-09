// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the search rule of the keybinding editor: label, category, id and both chord slots,
//   case-insensitivity, several terms (all must match), the empty query, and hostile queries (very
//   long, many terms, whitespace only, non-ASCII bytes).
// Callers: CTest (label fast).
#include "TestSupport.h"
#include "r1ui/widgets/commands/KeybindingFilter.h"

namespace {

using namespace r1ui::widgets;

const KeybindingRowText kUndo{"Undo", "Edit", "edit.undo", "Ctrl+Z", ""};
const KeybindingRowText kDelete{"Delete", "Edit", "edit.delete", "Delete", "Backspace"};

void testMatching() {
  R1_EXPECT(matchesKeybindingQuery("", kUndo) && matchesKeybindingQuery("   ", kUndo));
  R1_EXPECT(matchesKeybindingQuery("undo", kUndo) && matchesKeybindingQuery("UNDO", kUndo) && matchesKeybindingQuery("ndo", kUndo));
  R1_EXPECT(matchesKeybindingQuery("edit", kUndo));            // category and id
  R1_EXPECT(matchesKeybindingQuery("edit.undo", kUndo));      // id
  R1_EXPECT(matchesKeybindingQuery("ctrl+z", kUndo));         // chord text
  R1_EXPECT(matchesKeybindingQuery("ctrl z", kUndo));         // terms, in any order
  R1_EXPECT(matchesKeybindingQuery("z ctrl", kUndo));
  R1_EXPECT(!matchesKeybindingQuery("ctrl y", kUndo));        // every term must occur
  R1_EXPECT(!matchesKeybindingQuery("redo", kUndo));
  R1_EXPECT(matchesKeybindingQuery("backspace", kDelete));    // the alternate slot is matched on its own text
  R1_EXPECT(matchesKeybindingQuery("  delete   backspace ", kDelete));
  R1_EXPECT(!matchesKeybindingQuery("shift", kDelete));
}

void testHostile() {
  R1_EXPECT(matchesKeybindingQuery(std::string(100000, 'x') + " undo", {"x", "", "", "", ""}) == false);
  // Only the first kMaxQueryBytes are read and the first kMaxQueryTerms terms count.
  std::string many;
  for (size_t i = 0; i < 1000; ++i) many += "e ";
  R1_EXPECT(matchesKeybindingQuery(many, kUndo));
  const std::string late = std::string(kMaxQueryBytes, ' ') + "zzzz";
  R1_EXPECT(matchesKeybindingQuery(late, kUndo));  // the part beyond the limit is not read
  R1_EXPECT(!matchesKeybindingQuery("\xC3\xA9", kUndo) && matchesKeybindingQuery("\xC3\xA9", {"caf\xC3\xA9", "", "", "", ""}));
  R1_EXPECT(!matchesKeybindingQuery("\xFF\xFE", kUndo));
  R1_EXPECT(!matchesKeybindingQuery("a", {"", "", "", "", ""}));
  R1_EXPECT(matchesKeybindingQuery("\t\n\r", kUndo));
}

}  // namespace

int main() {
  testMatching();
  testHostile();
  return r1test::finish();
}
