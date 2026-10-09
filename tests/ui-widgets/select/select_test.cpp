// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for Select and SelectList against spec 10 rules 52-55 and spec 01: opening under the
//   trigger (2 px gap, at least its width), toggling by clicking the trigger, choosing with the pointer
//   or Enter (value changes, popup closes, callback once, choosing the selected row is silent), leaving by
//   Escape or an outside press (no change, focus restored), keyboard navigation without wrapping over
//   disabled items, group labels and separators, Home / End / paging, type-ahead, Space, the filter row
//   of the combobox variant (filter, Enter, Escape order, no results), scrolling beyond 224 px, trigger
//   keys, and hostile cases: 100000 entries, more than the limit, destroying the select while open or
//   inside its callback, disabling it while open, hostile labels, empty lists, zero-width triggers,
//   two selects, programmatic changes while open.
// Callers: CTest (select fast, no GPU; paint is checked on a recording Painter).
#include <cmath>
#include <string>
#include <vector>

#include "../textinput/FieldRig.h"
#include "r1ui/widgets/select/Select.h"
#include "r1ui/widgets/select/SelectList.h"

namespace {

using namespace r1ui::widgets;
namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;
using events::Key;
namespace Mod = events::Mod;

constexpr double kX = 10.0;
constexpr double kY = 10.0;

std::vector<SelectEntry> blend() {
  return {{SelectEntryKind::Item, "Pass through", "pass", false}, {SelectEntryKind::Item, "Normal", "normal", false},
          {SelectEntryKind::Item, "Locked", "locked", true},      {SelectEntryKind::Separator, "", "", false},
          {SelectEntryKind::Group, "Darken", "", false},          {SelectEntryKind::Item, "Darken", "darken", false},
          {SelectEntryKind::Item, "Multiply", "multiply", false}, {SelectEntryKind::Item, "Color burn", "burn", false},
          {SelectEntryKind::Group, "Lighten", "", false},         {SelectEntryKind::Item, "Lighten", "lighten", false},
          {SelectEntryKind::Item, "Screen", "screen", false},     {SelectEntryKind::Item, "Color dodge", "dodge", false}};
}

struct Setup {
  explicit Setup(double width = 114.0) {
    select = &rig.ui.create<Select>(rig.ui.root());
    select->style().width = layout::Length::px(width);
    select->setEntries(blend());
    select->setSelectedIndex(0);
    select->setOnChanged([this](size_t index, std::string_view value) {
      ++changes;
      lastIndex = index;
      lastValue = std::string(value);
    });
    rig.layout();
  }
  SelectList* list() { return rig.ui.objectAs<SelectList>(select->popup()); }
  layout::Rect triggerRect() { return rig.ui.absRect(select->id()); }
  void clickTrigger() { rig.click(kX + 40.0, kY + 13.0); }
  // Opens by pointer and lets the overlay be placed and focused.
  void open() {
    clickTrigger();
    rig.layout();
  }
  void clickRow(size_t entry) {
    const layout::Rect r = list()->rowRect(entry);
    rig.click(r.x + 30.0, r.y + r.h / 2.0);
  }

  r1test::FieldRig rig;
  Select* select = nullptr;
  int changes = 0;
  size_t lastIndex = 0;
  std::string lastValue;
};

void testOpenPlacementAndToggle() {
  Setup s;
  R1_EXPECT(!s.select->isOpen() && !s.rig.ui.overlays().any());
  s.open();
  R1_EXPECT(s.select->isOpen() && s.rig.ui.overlays().count() == 1 && s.list() != nullptr);
  const layout::Rect trigger = s.triggerRect();
  const layout::Rect list = s.rig.ui.absRect(s.select->popup());
  R1_EXPECT(list.x == trigger.x + 5);                    // host padding 4 + border 1
  R1_EXPECT(list.y == trigger.y + trigger.h + 2 + 5);    // 2 px gap, then padding and border
  R1_EXPECT(list.w + 10 >= trigger.w);                   // at least as wide as the trigger (rule 52)
  R1_EXPECT(s.rig.ui.router().focused() == s.select->popup());  // focus moved into the list
  R1_EXPECT(s.list()->highlighted() == 0u);              // starts on the selected row
  // Clicking the trigger again closes (rule 52) and does not reopen.
  s.clickTrigger();
  R1_EXPECT(!s.select->isOpen() && !s.rig.ui.overlays().any() && s.changes == 0);
  R1_EXPECT(s.rig.ui.router().focused() == s.select->id());
  // An outside press closes without a change and the press passes through (rule 55).
  s.open();
  R1_EXPECT(s.select->isOpen());
  s.rig.click(350.0, 250.0);
  R1_EXPECT(!s.select->isOpen() && s.changes == 0 && s.select->selectedIndex() == 0u);
}

void testEscapeRestoresFocus() {
  Setup s;
  s.rig.ui.router().focus(s.select->id(), events::FocusReason::Keyboard);
  s.rig.key(Key::Enter);
  s.rig.layout();
  R1_EXPECT(s.select->isOpen());
  s.rig.key(Key::Down);
  R1_EXPECT(s.list()->highlighted() == 1u);
  R1_EXPECT(s.rig.key(Key::Escape));
  R1_EXPECT(!s.select->isOpen() && s.changes == 0 && s.select->selectedIndex() == 0u);  // keyboard moves never change the value (rule 54)
  R1_EXPECT(s.rig.ui.router().focused() == s.select->id());
  // Escape pressed before the list has taken focus (same frame as the opening) also closes.
  s.rig.key(Key::Down);
  R1_EXPECT(s.select->isOpen());
  s.rig.key(Key::Escape);
  R1_EXPECT(!s.select->isOpen());
}

void testKeyboardNavigationAndChoice() {
  Setup s;
  s.rig.ui.router().focus(s.select->id(), events::FocusReason::Keyboard);
  s.rig.key(Key::Space);
  s.rig.layout();
  R1_EXPECT(s.select->isOpen() && s.list()->highlighted() == 0u);
  s.rig.key(Key::Down);
  R1_EXPECT(s.list()->highlighted() == 1u);
  s.rig.key(Key::Down);  // "Locked" is disabled, the separator and group label are not rows to stop on
  R1_EXPECT(s.list()->highlighted() == 5u);
  s.rig.key(Key::Down);
  s.rig.key(Key::Down);
  R1_EXPECT(s.list()->highlighted() == 7u);
  s.rig.key(Key::Down);  // the next group label is skipped
  R1_EXPECT(s.list()->highlighted() == 9u);
  s.rig.key(Key::End);
  R1_EXPECT(s.list()->highlighted() == 11u);
  s.rig.key(Key::Down);  // no wrap
  R1_EXPECT(s.list()->highlighted() == 11u);
  s.rig.key(Key::Home);
  R1_EXPECT(s.list()->highlighted() == 0u);
  s.rig.key(Key::Up);
  R1_EXPECT(s.list()->highlighted() == 0u);
  s.rig.key(Key::PageDown);
  R1_EXPECT(s.list()->highlighted() == 7u);  // a page is the 7 whole rows of the 214 px viewport
  s.rig.key(Key::PageUp);
  R1_EXPECT(s.list()->highlighted() == 0u);
  s.rig.key(Key::Down);
  s.rig.key(Key::Enter);
  R1_EXPECT(!s.select->isOpen() && s.changes == 1 && s.lastIndex == 1u && s.lastValue == "normal");
  R1_EXPECT(s.select->selectedIndex() == 1u && s.select->selectedValue() == "normal" && s.select->selectedLabel() == "Normal");
  R1_EXPECT(s.rig.ui.router().focused() == s.select->id());
  // Choosing the row that is already selected closes silently (rule 53).
  s.rig.key(Key::Down);
  s.rig.layout();
  R1_EXPECT(s.select->isOpen() && s.list()->highlighted() == 1u);
  s.rig.key(Key::Enter);
  R1_EXPECT(!s.select->isOpen() && s.changes == 1);
}

void testPointerChoice() {
  Setup s;
  s.open();
  // Hover highlights enabled rows only.
  const layout::Rect normal = s.list()->rowRect(1);
  s.rig.ui.pointerMove(normal.x + 20.0, normal.y + 10.0);
  R1_EXPECT(s.list()->highlighted() == 1u);
  const layout::Rect locked = s.list()->rowRect(2);
  s.rig.ui.pointerMove(locked.x + 20.0, locked.y + 10.0);
  R1_EXPECT(s.list()->highlighted() == 1u);
  // Clicking a disabled row, a group label or a separator does nothing and keeps the popup open.
  s.rig.click(locked.x + 20.0, locked.y + 10.0);
  s.rig.click(s.list()->rowRect(4).x + 20.0, s.list()->rowRect(4).y + 10.0);
  s.rig.click(s.list()->rowRect(3).x + 20.0, s.list()->rowRect(3).y + 4.0);
  R1_EXPECT(s.select->isOpen() && s.changes == 0);
  s.clickRow(6);
  R1_EXPECT(!s.select->isOpen() && s.changes == 1 && s.lastIndex == 6u && s.lastValue == "multiply");
  R1_EXPECT(s.select->selectedIndex() == 6u);
  R1_EXPECT(s.rig.paint() > 0);
}

void testTypeAheadAndSpace() {
  Setup s;
  s.rig.ui.router().focus(s.select->id(), events::FocusReason::Keyboard);
  s.rig.type("c");  // typing on the closed trigger opens the list and starts type-ahead
  R1_EXPECT(s.select->isOpen());
  s.rig.layout();
  R1_EXPECT(s.list()->highlighted() == 7u);  // "Color burn"
  s.rig.type("o");
  s.rig.type("l");
  s.rig.type("o");
  s.rig.type("r");
  s.rig.type(" ");
  s.rig.type("d");
  R1_EXPECT(s.list()->highlighted() == 11u);  // "Color dodge"
  // After a pause the buffer starts over; a lone letter cycles through its matches.
  s.rig.ui.setTime(s.rig.ui.now() + 2000);
  s.rig.type("c");
  R1_EXPECT(s.list()->highlighted() == 7u);
  s.rig.ui.setTime(s.rig.ui.now() + 2000);
  s.rig.key(Key::Space);  // no recent typing: Space chooses
  R1_EXPECT(!s.select->isOpen() && s.changes == 1 && s.lastIndex == 7u);
  // Control chords are not text.
  s.rig.ui.router().focus(s.select->id(), events::FocusReason::Keyboard);
  R1_EXPECT(!s.rig.ctrl('S'));
}

void testSearchableCombobox() {
  Setup s;
  s.select->setSearchable(true);
  s.open();
  R1_EXPECT(s.select->isOpen());
  const int fullHeight = s.rig.ui.absRect(s.select->popup()).h;
  s.rig.type("ar");  // matches "Darken" only ("Pass through"? no: contains no "ar"); "Darken" has "ar"
  R1_EXPECT(s.list()->filterText() == "ar");
  R1_EXPECT(s.select->model().visible().size() == 2);  // the group label and its item
  s.rig.layout();
  R1_EXPECT(s.rig.ui.absRect(s.select->popup()).h < fullHeight);
  R1_EXPECT(s.list()->highlighted() == 5u);
  s.rig.key(Key::Backspace);  // editing keys go to the filter
  s.rig.key(Key::Backspace);
  R1_EXPECT(s.list()->filterText().empty() && s.select->model().visible().size() == blend().size());
  s.rig.type("color");
  R1_EXPECT(s.list()->highlighted() == 7u);
  s.rig.key(Key::Down);
  R1_EXPECT(s.list()->highlighted() == 11u);
  // Escape clears the filter first (rule 29), then closes.
  R1_EXPECT(s.rig.key(Key::Escape));
  R1_EXPECT(s.select->isOpen() && s.list()->filterText().empty());
  s.rig.type("zzz");  // nothing matches
  R1_EXPECT(!s.select->model().anyVisibleItem() && !s.list()->highlighted());
  R1_EXPECT(s.rig.paint() > 0);  // the "No results" hint
  s.rig.key(Key::Enter);         // nothing highlighted: closes without a change
  R1_EXPECT(!s.select->isOpen() && s.changes == 0);
  R1_EXPECT(s.select->model().filter().empty());  // closing resets the filter
  // Enter on the filtered list chooses the highlighted row.
  s.open();
  s.rig.type("burn");
  s.rig.key(Key::Enter);
  R1_EXPECT(!s.select->isOpen() && s.changes == 1 && s.lastValue == "burn");
  // Space is text in a searchable list.
  s.open();
  s.rig.type("color d");
  R1_EXPECT(s.list()->filterText() == "color d" && s.select->isOpen());
  R1_EXPECT(s.list()->highlighted() == 11u);
  s.rig.ctrl('A');
  s.rig.type("x");
  R1_EXPECT(s.list()->filterText() == "x");
  s.rig.key(Key::Escape);
  s.rig.key(Key::Escape);
  R1_EXPECT(!s.select->isOpen());
}

void testLongListScrolls() {
  Setup s;
  std::vector<SelectEntry> items;
  for (int i = 0; i < 100; ++i) items.push_back({SelectEntryKind::Item, "Item " + std::to_string(i), "", false});
  s.select->setEntries(std::move(items));
  s.select->setSelectedIndex(0);
  s.open();
  const layout::Rect host = s.rig.ui.absRect(s.select->popup());
  R1_EXPECT(host.h <= 214 && host.h > 150);  // capped at 224 minus padding and border
  R1_EXPECT(s.list()->scrollOffset() == 0.0);
  R1_EXPECT(s.rig.ui.wheel(host.x + 20.0, host.y + 40.0, 0, -2.0));  // wheel down
  R1_EXPECT(s.list()->scrollOffset() > 0.0);
  s.rig.ui.wheel(host.x + 20.0, host.y + 40.0, 0, 5.0);  // wheel up past the top clamps at 0
  R1_EXPECT(s.list()->scrollOffset() == 0.0);
  s.rig.key(Key::End);
  R1_EXPECT(s.list()->highlighted() == 99u && s.list()->scrollOffset() > 0.0);
  const layout::Rect row = s.list()->rowRect(99);
  R1_EXPECT(row.y >= host.y && row.y + row.h <= host.y + host.h + 1);  // the highlighted row is inside the viewport
  s.rig.key(Key::Home);
  R1_EXPECT(s.list()->scrollOffset() == 0.0);
  s.rig.key(Key::PageDown);
  R1_EXPECT(s.list()->highlighted() > 5u && s.list()->highlighted() < 99u);
  // The scrollbar thumb appears on hover and drags.
  s.rig.ui.pointerMove(host.x + 30.0, host.y + 30.0);
  R1_EXPECT(s.rig.paint() > 0);
  const double right = host.x + host.w - 4.0;
  s.rig.ui.pointerMove(right, host.y + 6.0);
  s.rig.ui.pointerDown(right, host.y + 6.0);
  s.rig.ui.pointerMove(right, host.y + 150.0);
  s.rig.ui.pointerUp(right, host.y + 150.0);
  R1_EXPECT(s.list()->scrollOffset() > 0.0 && s.select->isOpen());
  // Choosing a row that was scrolled into view.
  s.rig.key(Key::End);
  s.rig.key(Key::Enter);
  R1_EXPECT(s.changes == 1 && s.lastIndex == 99u);
}

void testTriggerKeys() {
  for (const Key key : {Key::Enter, Key::Space, Key::Down, Key::Up}) {
    Setup s;
    s.rig.ui.router().focus(s.select->id(), events::FocusReason::Keyboard);
    R1_EXPECT(s.rig.key(key));
    R1_EXPECT(s.select->isOpen());
  }
  Setup s;
  s.rig.ui.router().focus(s.select->id(), events::FocusReason::Keyboard);
  R1_EXPECT(s.rig.key(static_cast<Key>('K')));   // a letter is swallowed (the character follows as text)
  R1_EXPECT(!s.rig.key(Key::Left));              // other keys travel on
  R1_EXPECT(!s.rig.ctrl('S'));
}

void testProgrammaticChanges() {
  Setup s;
  R1_EXPECT(!s.select->setSelectedIndex(3));    // a separator
  R1_EXPECT(!s.select->setSelectedIndex(4));    // a group label
  R1_EXPECT(!s.select->setSelectedIndex(99));
  R1_EXPECT(s.select->selectedIndex() == 0u);
  R1_EXPECT(s.select->setSelectedValue("burn") && s.select->selectedIndex() == 7u);
  R1_EXPECT(!s.select->setSelectedValue("nope") && s.select->selectedIndex() == 7u);
  R1_EXPECT(s.select->setSelectedIndex(std::nullopt) && !s.select->selectedIndex() && s.select->selectedValue().empty());
  R1_EXPECT(s.changes == 0);  // programmatic changes never call back
  s.select->setPlaceholder("Choose");
  R1_EXPECT(s.select->accessibleName() == "Choose");
  s.select->setSelectedValue("screen");
  R1_EXPECT(s.select->accessibleName() == "Screen");
  // Replacing the entries closes an open list and keeps the selection by value.
  s.open();
  R1_EXPECT(s.select->isOpen());
  auto next = blend();
  next.erase(next.begin() + 1);  // remove "Normal": indices shift
  R1_EXPECT(s.select->setEntries(next));
  R1_EXPECT(!s.select->isOpen() && s.select->selectedValue() == "screen" && s.select->selectedIndex() == 9u);
  next.pop_back();
  next.pop_back();
  s.select->setEntries(next);  // "screen" is gone
  R1_EXPECT(!s.select->selectedIndex());
  R1_EXPECT(s.select->addItem("Added", "added") && s.select->addGroup("G") && s.select->addSeparator());
  R1_EXPECT(s.select->entries().size() == next.size() + 3);
  s.select->clearEntries();
  R1_EXPECT(s.select->entries().empty());
  s.open();  // an empty list still opens and shows its hint
  R1_EXPECT(s.select->isOpen() && s.rig.paint() > 0);
  s.rig.key(Key::Down);
  s.rig.key(Key::Enter);
  R1_EXPECT(!s.select->isOpen() && s.changes == 0);
}

void testCompactAndPaint() {
  for (const float scale : {1.0f, 1.5f, 2.0f}) {
    r1test::FieldRig rig(400, 300, scale);
    Select& select = rig.ui.create<Select>(rig.ui.root());
    select.style().width = layout::Length::px(114);
    select.setEntries(blend());
    select.setCompact(true);
    select.setPlaceholder("Choose");
    rig.layout();
    R1_EXPECT(rig.paint() > 0);  // placeholder
    select.setSelectedIndex(1);
    R1_EXPECT(rig.paint() > 0);
    select.open();
    rig.layout();
    R1_EXPECT(select.isOpen() && rig.paint() > 0);
    select.close();
    R1_EXPECT(!select.isOpen());
    select.setEnabled(false);
    R1_EXPECT(rig.paint() > 0 && !select.open());
  }
}

void testHostileInput() {
  {
    // A very large list opens, filters, scrolls and paints.
    Setup s;
    std::vector<SelectEntry> big;
    for (size_t i = 0; i < SelectModel::kMaxEntries; ++i) big.push_back({SelectEntryKind::Item, "Entry " + std::to_string(i), "", false});
    R1_EXPECT(s.select->setEntries(std::move(big)));
    std::vector<SelectEntry> tooMany(SelectModel::kMaxEntries + 1, SelectEntry{SelectEntryKind::Item, "x", "", false});
    R1_EXPECT(!s.select->setEntries(std::move(tooMany)) && s.select->entries().size() == SelectModel::kMaxEntries);
    s.open();
    R1_EXPECT(s.select->isOpen());
    s.rig.key(Key::End);
    R1_EXPECT(s.list()->highlighted() == SelectModel::kMaxEntries - 1);
    R1_EXPECT(s.rig.paint() < 400);  // only the visible rows are drawn
    s.rig.key(Key::Escape);
    s.select->setSearchable(true);
    s.open();
    s.rig.type("99999");
    R1_EXPECT(s.select->model().visible().size() == 1);
    s.rig.key(Key::Enter);
    R1_EXPECT(s.changes == 1 && s.lastIndex == SelectModel::kMaxEntries - 1);
  }
  {
    // Hostile labels: controls, invalid UTF-8, a very long label.
    Setup s;
    s.select->setEntries({{SelectEntryKind::Item, std::string("a\x01\x02\n\xFF\xFE" "b"), "", false}, {SelectEntryKind::Item, std::string(100000, 'L'), "", false}});
    s.open();
    R1_EXPECT(s.select->entries()[0].label == "a\xEF\xBF\xBD\xEF\xBF\xBD" "b");
    R1_EXPECT(s.rig.paint() > 0);
    s.rig.key(Key::End);
    s.rig.key(Key::Enter);
    R1_EXPECT(s.select->selectedIndex() == 1u);
    R1_EXPECT(s.rig.paint() > 0);
  }
  {
    // Destroy the select while its list is open, and from inside the change callback.
    Setup s;
    s.open();
    const auto id = s.select->id();
    s.rig.ui.destroy(id);
    s.rig.layout();
    R1_EXPECT(!s.rig.ui.alive(id) && !s.rig.ui.overlays().any());
    (void)s.rig.paint();
    Setup t;
    const auto tid = t.select->id();
    t.select->setOnChanged([&](size_t, std::string_view) { t.rig.ui.destroy(tid); });
    t.open();
    t.clickRow(1);
    R1_EXPECT(!t.rig.ui.alive(tid) && !t.rig.ui.overlays().any());
    t.rig.layout();
    (void)t.rig.paint();
  }
  {
    // Disabling a select with an open list closes it.
    Setup s;
    s.open();
    s.select->setEnabled(false);
    R1_EXPECT(!s.select->isOpen() && !s.rig.ui.overlays().any());
    s.rig.key(Key::Enter);
    R1_EXPECT(!s.select->isOpen());
  }
  {
    // A zero-width and a one-pixel trigger still work; the popup is placed inside the window.
    for (const double width : {0.0, 1.0, 20.0}) {
      Setup s(width);
      s.select->open();
      s.rig.layout();
      R1_EXPECT(s.select->isOpen());
      s.rig.key(Key::Down);
      s.rig.key(Key::Enter);
      (void)s.rig.paint();
      R1_EXPECT(!s.select->isOpen());
    }
  }
  {
    // Two selects: pressing the second while the first is open closes the first and opens the second.
    Setup s;
    Select& other = s.rig.ui.create<Select>(s.rig.ui.root());
    other.style().width = layout::Length::px(114);
    other.setEntries(blend());
    other.style().position = layout::Position::Absolute;
    other.style().inset[layout::kLeft] = layout::Length::px(250);
    other.style().inset[layout::kTop] = layout::Length::px(10);
    s.rig.layout();
    s.open();
    R1_EXPECT(s.select->isOpen());
    const layout::Rect o = s.rig.ui.absRect(other.id());
    s.rig.click(o.x + 40.0, o.y + 13.0);
    s.rig.layout();
    R1_EXPECT(!s.select->isOpen() && other.isOpen());
    other.close();
  }
  {
    // Rapid open / close / choose cycles leave no overlay behind.
    Setup s;
    for (int i = 0; i < 200; ++i) {
      s.select->open();
      s.rig.layout();
      s.rig.key(i % 3 == 0 ? Key::Down : Key::Up);
      s.rig.key(i % 4 == 0 ? Key::Enter : Key::Escape);
    }
    R1_EXPECT(!s.select->isOpen() && !s.rig.ui.overlays().any());
  }
}

}  // namespace

int main() {
  testOpenPlacementAndToggle();
  testEscapeRestoresFocus();
  testKeyboardNavigationAndChoice();
  testPointerChoice();
  testTypeAheadAndSpace();
  testSearchableCombobox();
  testLongListScrolls();
  testTriggerKeys();
  testProgrammaticChanges();
  testCompactAndPaint();
  testHostileInput();
  return r1test::finish();
}
