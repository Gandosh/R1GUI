// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for SelectModel: entry sanitising, the filter (substring, group labels shown only
//   with a visible item, separators hidden), stepping without wrapping over disabled items, group
//   labels and separators, page jumps, type-ahead (prefix, growing prefix, repeated character cycling,
//   no match), value lookup, and hostile input: empty lists, all items disabled, control characters and
//   invalid UTF-8 in labels, more than the entry limit (refused, old list kept), a 100000 entry list
//   filtered and navigated, filters with odd bytes.
// Callers: CTest (select fast, no GPU).
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/widgets/select/SelectModel.h"

namespace {

using namespace r1ui::widgets;

std::vector<SelectEntry> sample() {
  return {{SelectEntryKind::Item, "Pass through", "", false}, {SelectEntryKind::Item, "Normal", "", false},     {SelectEntryKind::Separator, "", "", false},
          {SelectEntryKind::Group, "Darken", "", false},      {SelectEntryKind::Item, "Darken", "darken", false}, {SelectEntryKind::Item, "Multiply", "", true},
          {SelectEntryKind::Item, "Color burn", "", false},   {SelectEntryKind::Group, "Lighten", "", false},     {SelectEntryKind::Item, "Lighten", "", false},
          {SelectEntryKind::Item, "Screen", "", false},       {SelectEntryKind::Item, "Color dodge", "", false}};
}

void testEntriesAreSanitised() {
  SelectModel m;
  R1_EXPECT(m.setEntries({{SelectEntryKind::Item, std::string("a\x01" "b\n\xFF" "c\xE2\x80\xA8"), "", false}, {SelectEntryKind::Item, "x", "", false}}));
  R1_EXPECT(m.entries()[0].label == "ab\xEF\xBF\xBD" "c");  // controls and U+2028 dropped, the bad byte repaired
  R1_EXPECT(m.entries()[0].value == m.entries()[0].label);  // value defaults to the label
  R1_EXPECT(m.entries()[1].value == "x");
  R1_EXPECT(m.visible().size() == 2);
}

void testFilter() {
  SelectModel m;
  m.setEntries(sample());
  R1_EXPECT(m.visible().size() == 11);
  m.setFilter("dark");
  // Group label "Darken" + item "Darken": the group (index 3) and the item (4) only.
  R1_EXPECT(m.visible() == (std::vector<size_t>{3, 4}));
  m.setFilter("COLOR");
  R1_EXPECT(m.visible() == (std::vector<size_t>{3, 6, 7, 10}));  // each match brings the group label above it
}

void testFilterGroupsAndSeparators() {
  SelectModel m;
  m.setEntries(sample());
  m.setFilter("light");
  R1_EXPECT(m.visible() == (std::vector<size_t>{7, 8}));  // group "Lighten" and its item; the separator is hidden
  m.setFilter("zzz");
  R1_EXPECT(m.visible().empty() && !m.anyVisibleItem());
  R1_EXPECT(!m.first() && !m.last() && !m.step(std::nullopt, 1));
  m.setFilter("");
  R1_EXPECT(m.visible().size() == 11);
  m.setFilter("  ");  // two blanks match no label
  R1_EXPECT(m.visible().empty());
  m.setFilter(std::string("\xFF\x01" "no\n"));  // hostile filter text is sanitised, never crashes
  R1_EXPECT(m.visible().empty());
  m.setFilter(std::string(100000, 'q'));
  R1_EXPECT(m.visible().empty());
}

void testStepping() {
  SelectModel m;
  m.setEntries(sample());
  R1_EXPECT(m.first() == 0u && m.last() == 10u);
  R1_EXPECT(m.step(std::nullopt, 1) == 0u && m.step(std::nullopt, -1) == 10u);
  R1_EXPECT(m.step(0u, 1) == 1u);
  R1_EXPECT(m.step(1u, 1) == 4u);   // skips the separator and the group label
  R1_EXPECT(m.step(4u, 1) == 6u);   // skips the disabled item
  R1_EXPECT(m.step(6u, -1) == 4u);
  R1_EXPECT(m.step(0u, -1) == 0u);  // no wrap at the top
  R1_EXPECT(m.step(10u, 1) == 10u); // no wrap at the bottom
  R1_EXPECT(m.step(3u, 1) == 4u);   // starting on a group label (not selectable) still moves on
  R1_EXPECT(m.step(0u, 0) == 0u);
  R1_EXPECT(m.step(9999u, 1) == 0u);  // a stale index restarts from the first item
}

void testPaging() {
  SelectModel m;
  m.setEntries(sample());
  R1_EXPECT(m.page(0u, 3) == 4u);    // row 3 is the group label: the next enabled item
  R1_EXPECT(m.page(0u, 100) == 10u); // clamped to the last
  R1_EXPECT(m.page(10u, -100) == 0u);
  R1_EXPECT(m.page(std::nullopt, 2) == 1u);
  R1_EXPECT(m.page(5u, 0) == 5u);
  SelectModel empty;
  R1_EXPECT(!empty.page(std::nullopt, 5));
}

void testTypeAhead() {
  SelectModel m;
  m.setEntries(sample());
  R1_EXPECT(m.typeAhead("n", std::nullopt) == 1u);
  R1_EXPECT(m.typeAhead("PASS", std::nullopt) == 0u);
  R1_EXPECT(m.typeAhead("pass t", std::nullopt) == 0u);  // a space is part of the prefix
  R1_EXPECT(m.typeAhead("c", std::nullopt) == 6u);       // "Color burn" (item 5 is disabled and skipped anyway)
  R1_EXPECT(m.typeAhead("c", 6u) == 10u);                // a single character cycles to the next match
  R1_EXPECT(m.typeAhead("cc", 6u) == 10u);               // repeated characters cycle too
  R1_EXPECT(m.typeAhead("c", 10u) == 6u);                // and wrap around
  R1_EXPECT(m.typeAhead("co", 6u) == 6u);                // a longer prefix searches from the current item
  R1_EXPECT(m.typeAhead("color d", 6u) == 10u);
  R1_EXPECT(!m.typeAhead("zz", std::nullopt));
  R1_EXPECT(!m.typeAhead("", std::nullopt));
  R1_EXPECT(!m.typeAhead("mu", std::nullopt));           // "Multiply" is disabled
  m.setFilter("light");
  R1_EXPECT(!m.typeAhead("n", std::nullopt));            // only visible items are searched
  R1_EXPECT(m.typeAhead("l", std::nullopt) == 8u);
}

void testValueLookup() {
  SelectModel m;
  m.setEntries(sample());
  R1_EXPECT(m.indexOfValue("darken") == 4u);
  R1_EXPECT(m.indexOfValue("Normal") == 1u);
  R1_EXPECT(!m.indexOfValue("Darken x"));
  R1_EXPECT(!m.indexOfValue(""));
  R1_EXPECT(m.rowOf(4u) == 4u && !m.rowOf(99u));
}

void testLimits() {
  SelectModel m;
  R1_EXPECT(m.setEntries(sample()));
  std::vector<SelectEntry> tooMany(SelectModel::kMaxEntries + 1, SelectEntry{SelectEntryKind::Item, "x", "", false});
  R1_EXPECT(!m.setEntries(std::move(tooMany)));
  R1_EXPECT(m.entries().size() == 11);  // the old list stays
  std::vector<SelectEntry> big;
  big.reserve(SelectModel::kMaxEntries);
  for (size_t i = 0; i < SelectModel::kMaxEntries; ++i) big.push_back({SelectEntryKind::Item, "item " + std::to_string(i), "", i % 100 == 0});
  R1_EXPECT(m.setEntries(std::move(big)));
  R1_EXPECT(m.visible().size() == SelectModel::kMaxEntries);
  m.setFilter("item 9999");
  R1_EXPECT(m.visible().size() == 11);  // item 9999, 99990 .. 99999
  m.setFilter("");
  R1_EXPECT(m.typeAhead("item 5", std::nullopt).has_value());
  R1_EXPECT(m.step(m.first(), 1).has_value());
  R1_EXPECT(m.page(m.first(), 40).has_value());
  // Empty and all-disabled lists have no navigation targets.
  SelectModel none;
  R1_EXPECT(none.setEntries({}));
  R1_EXPECT(!none.first() && !none.step(std::nullopt, 1) && !none.typeAhead("a", std::nullopt));
  SelectModel disabled;
  disabled.setEntries({{SelectEntryKind::Item, "a", "", true}, {SelectEntryKind::Group, "g", "", false}, {SelectEntryKind::Item, "b", "", true}});
  R1_EXPECT(!disabled.first() && !disabled.last() && !disabled.step(std::nullopt, 1) && disabled.step(0u, 1) == 0u);
}

}  // namespace

int main() {
  testEntriesAreSanitised();
  testFilter();
  testFilterGroupsAndSeparators();
  testStepping();
  testPaging();
  testTypeAhead();
  testValueLookup();
  testLimits();
  return r1test::finish();
}
