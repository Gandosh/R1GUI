// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the speed and hostile-input tests of ActionList: 20 000 actions (load, filter, scroll and paint
//   cost, and that the widget count does not grow with the action count), the action cap, duplicate ids,
//   10 000-character descriptions, invalid UTF-8 in every text field and in the query, many categories,
//   categories differing only by case, an empty list, a zero-sized list and non-finite scroll values.
//   Every scene is painted once so a paint failure is caught here.
// Callers: CTest (label fast). The measured times are printed so the evidence record can quote them.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>

#include "ActionFixture.h"
#include "r1ui/commands/Text.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;
using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

size_t descendantCount(ActionScene& s) {
  size_t n = 0;
  s.t.ui.tree().forEachDescendant(s.t.ui.root(), [&](WidgetId) { ++n; });
  return n;
}

void testTwentyThousand() {
  ActionScene small(sampleActions(50));
  const size_t smallWidgets = descendantCount(small);

  ActionScene s(std::vector<ActionInfo>{});
  auto t0 = Clock::now();
  s.list->setActions(sampleActions(20000));
  s.t.layout();
  const double load = msSince(t0);
  R1_EXPECT(s.view().actions().size() == 20000 && s.view().matchCount() == 20000);
  R1_EXPECT(descendantCount(s) == smallWidgets);  // virtualized: no widget per row

  t0 = Clock::now();
  s.list->setFilter("action 19");
  const double filterOne = msSince(t0);
  R1_EXPECT(s.view().matchCount() > 1100 && s.view().matchCount() < 4000);  // every number with "19" in it
  t0 = Clock::now();
  s.list->setFilter("thing 1 widget");
  const double filterWide = msSince(t0);
  R1_EXPECT(s.view().matchCount() > 10000);
  t0 = Clock::now();
  s.list->setFilter("");
  const double filterClear = msSince(t0);

  // Scroll to several places and paint: each frame touches only the visible rows.
  double paintMax = 0.0;
  for (const double fraction : {0.0, 0.37, 0.99, 1.0}) {
    s.view().setScrollOffset(fraction * (s.view().contentHeight() - s.view().viewportHeight()));
    t0 = Clock::now();
    s.paintOnce();
    paintMax = std::max(paintMax, msSince(t0));
  }
  // rowAt on the last screen finds a row in logarithmic time and agrees with rowRect.
  const int last = static_cast<int>(s.view().rows().size()) - 1;
  s.view().selectRow(last);
  const RectD r = s.rowRect(last);
  R1_EXPECT(r.h > 0.0 && s.view().rowAt(r.x + 10, r.y + 2) == last);
  // Collapsing every group shows only the headers; expanding restores the rows.
  for (const std::string& c : s.view().categories()) s.view().setCollapsed(c, true);
  R1_EXPECT(s.view().rows().size() == 3);
  for (const std::string& c : s.view().categories()) s.view().setCollapsed(c, false);
  R1_EXPECT(s.view().rows().size() == 20003);

  std::printf("action list, 20000 actions: load %.1f ms, filter(1 hit) %.1f ms, filter(wide) %.1f ms, clear %.1f ms, worst paint %.1f ms\n", load, filterOne, filterWide, filterClear,
              paintMax);
  R1_EXPECT(load < 3000.0 && filterOne < 1500.0 && filterWide < 1500.0 && filterClear < 1500.0 && paintMax < 250.0);
}

void testCapAndDuplicates() {
  ActionScene s(std::vector<ActionInfo>{});
  std::vector<ActionInfo> many(kMaxActions + 500);
  for (size_t i = 0; i < many.size(); ++i) {
    many[i].id = "same.id";  // all duplicates
    many[i].label = "L";
    many[i].category = "C";
  }
  s.list->setActions(std::move(many));
  R1_EXPECT(s.view().actions().size() == kMaxActions);
  R1_EXPECT(s.view().matchCount() == kMaxActions);
  R1_EXPECT(s.view().selectAction("same.id"));  // the first match wins
  s.view().setCategoryFilter("C");
  s.paintOnce();
}

void testTextHostility() {
  std::vector<ActionInfo> actions;
  ActionInfo bad;
  bad.id = "bad.text";
  bad.label = std::string("Label \xFF\xC0\xAF with \xED\xA0\x80 junk\x01\x02");
  bad.description = std::string(10000, 'd') + std::string("\xE2\x82");  // 10k chars, ends in half a character
  bad.category = std::string("Cat\xF8\x88\x80\x80\x80");
  bad.shortcut = std::string("Ctrl+\xFF");
  bad.icon = "no-such-icon";
  actions.push_back(bad);
  ActionInfo empty;
  actions.push_back(empty);  // everything empty
  ActionInfo huge;
  huge.id = std::string(100000, 'i');
  huge.label = std::string(100000, 'l');
  huge.category = std::string(100000, 'c');
  actions.push_back(huge);
  ActionInfo upper = bad, lower = bad;
  upper.id = "u";
  upper.category = "Edit";
  lower.id = "l";
  lower.category = "edit";  // differs only by case: two groups
  actions.push_back(upper);
  actions.push_back(lower);
  ActionScene s(std::move(actions));
  R1_EXPECT(s.view().actions().size() == 5);
  for (const ActionInfo& a : s.view().actions()) {
    R1_EXPECT(r1ui::commands::isValidUtf8(a.id) && r1ui::commands::isValidUtf8(a.label) && r1ui::commands::isValidUtf8(a.description));
    R1_EXPECT(r1ui::commands::isValidUtf8(a.category) && r1ui::commands::isValidUtf8(a.shortcut));
    R1_EXPECT(a.description.size() <= kMaxActionDescriptionBytes && a.label.size() <= kMaxActionLabelBytes && a.id.size() <= kMaxActionIdBytes);
    R1_EXPECT(!a.category.empty());
  }
  R1_EXPECT(s.view().categories().size() == 5);  // General (empty), the long one, the bad one, Edit, edit
  s.paintOnce();
  // Queries: invalid UTF-8, control characters, enormous input.
  for (const std::string& q : {std::string("\xFF\xFE"), std::string("a\x01\x02 b"), std::string(1000000, 'z'), std::string("\xC3"), std::string(" \t \n ")}) {
    s.list->setFilter(q);
    s.paintOnce();
    R1_EXPECT(r1ui::commands::isValidUtf8(s.view().query()) && s.view().query().size() <= kMaxActionQueryBytes);
  }
  s.list->setFilter("label");  // matches the sanitised text of the bad action
  R1_EXPECT(s.view().matchCount() >= 3);
  s.paintOnce();
  // The tooltip text of the 10k description is the (bounded) description, never read past its end.
  s.list->setFilter("");
  const RectD r = s.rowRect(s.rowOf("bad.text"));
  s.t.ui.pointerMove(r.x + 100, r.y + r.h / 2);
  R1_EXPECT(s.view().tooltipText().size() <= kMaxActionDescriptionBytes);
}

void testDegenerate() {
  // An empty list paints and handles every key.
  ActionScene empty(std::vector<ActionInfo>{});
  empty.focusView();
  for (const Key k : {Key::Down, Key::Up, Key::Home, Key::End, Key::PageDown, Key::PageUp, Key::Enter, Key::Left, Key::Right, Key::Space}) empty.key(k);
  empty.paintOnce();
  R1_EXPECT(empty.view().rows().empty() && empty.view().selectedRow() == -1 && empty.view().contentHeight() == 0.0);
  R1_EXPECT(empty.view().rowAt(10, 10) == -1);
  // A zero-sized and a one-pixel list.
  ActionScene tiny(sampleActions(40));
  for (const double size : {0.0, 1.0, 5.0}) {
    tiny.list->style().width = r1ui::core::layout::Length::px(size);
    tiny.list->style().height = r1ui::core::layout::Length::px(size);
    tiny.list->requestLayout();
    tiny.t.layout();
    tiny.paintOnce();
    tiny.focusView();
    tiny.key(Key::Down);
    tiny.key(Key::End);
    R1_EXPECT(std::isfinite(tiny.view().scrollOffset()) && tiny.view().scrollOffset() >= 0.0);
    R1_EXPECT(tiny.view().columns().labelW >= 0.0);
  }
  // Non-finite scroll values and selections outside the range are ignored.
  tiny.view().setScrollOffset(std::numeric_limits<double>::infinity());
  R1_EXPECT(std::isfinite(tiny.view().scrollOffset()));
  tiny.view().selectRow(-7);
  tiny.view().selectRow(100000);
  R1_EXPECT(tiny.view().selectedRow() < static_cast<int>(tiny.view().rows().size()));
  tiny.view().setCollapsed("No such category", true);
  R1_EXPECT(!tiny.view().isCollapsed("No such category"));
  // Replacing the actions from inside the selection callback does not crash.
  ActionScene reentrant(sampleActions(10));
  reentrant.view().setOnSelect([&](const ActionInfo&) { reentrant.list->setActions(sampleActions(3)); });
  reentrant.click(1);
  reentrant.paintOnce();
  R1_EXPECT(reentrant.view().actions().size() == 3);
}

}  // namespace

int main() {
  testTwentyThousand();
  testCapAndDuplicates();
  testTextHostility();
  testDegenerate();
  return r1test::finish();
}
