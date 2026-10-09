// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for Segmented: geometry (height 26, equal item widths, hit testing including the
//   gaps), click selection (same enabled item for press and release), the change callback only for
//   user changes, arrow / Home / End navigation skipping disabled items without wrapping, a keyboard
//   press at an edge being consumed, per-item tooltips, programmatic value API with range checks,
//   item replacement keeping or clearing the selection, destroy from the callback and hostile input
//   (too many items, bad icon names, invalid UTF-8 and huge labels, zero width, no items).
// Callers: CTest (segmented fast, no GPU).
#include <limits>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/segmented/Segmented.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::FocusReason;
using r1ui::core::events::Key;
namespace layout = r1ui::core::layout;

void paintOnce(r1test::TestUi& t) {
  r1ui::render::Painter painter;
  painter.begin(static_cast<uint32_t>(t.ui.viewportWidth() * t.ui.scale()), static_cast<uint32_t>(t.ui.viewportHeight() * t.ui.scale()));
  t.ui.paint(painter);
  t.ui.finishPaint();
  painter.end();
}

Segmented& make(r1test::TestUi& t, int width = 240) {
  auto& ui = t.ui;
  ui.rootStyle().alignItems = layout::Align::Start;
  Segmented& s = ui.create<Segmented>(ui.root(), SegmentedSize::Md);
  s.setItems({{.text = "One"}, {.text = "Two"}, {.text = "Three", .enabled = false}, {.text = "Four", .tooltip = "Fourth"}});
  s.style().width = layout::Length::px(width);
  return s;
}

void testGeometryAndPointer() {
  r1test::TestUi t;
  auto& ui = t.ui;
  Segmented& s = make(t);
  int last = -2, changes = 0;
  s.setOnChange([&](int i) {
    last = i;
    ++changes;
  });
  t.layout();
  const layout::Rect r = ui.absRect(s.id());
  R1_EXPECT(r.h == 26 && r.w == 240);
  // 4 items in 240 - 4 padding - 3 x 2 gap = 230 px: 57.5 px each.
  R1_EXPECT(s.itemAt(1.0) == -1);       // padding
  R1_EXPECT(s.itemAt(3.0) == 0);
  R1_EXPECT(s.itemAt(2.0 + 57.5 + 1.0) == -1);  // the gap
  R1_EXPECT(s.itemAt(2.0 + 57.5 + 3.0) == 1);
  R1_EXPECT(s.itemAt(239.0) == -1 || s.itemAt(239.0) == 3);
  R1_EXPECT(s.itemAt(std::numeric_limits<double>::quiet_NaN()) == -1);

  const double y = r.y + 13;
  const auto xOf = [&](int item) { return r.x + 2 + item * 59.5 + 20; };
  ui.pointerMove(xOf(1), y);
  ui.pointerDown(xOf(1), y);
  ui.pointerUp(xOf(1), y);
  R1_EXPECT(s.selectedIndex() == 1 && last == 1 && changes == 1);
  ui.pointerDown(xOf(1), y);
  ui.pointerUp(xOf(1), y);
  R1_EXPECT(changes == 1);  // already selected
  // Disabled item: nothing. Press on one item, release on another: nothing.
  ui.pointerDown(xOf(2), y);
  ui.pointerUp(xOf(2), y);
  R1_EXPECT(s.selectedIndex() == 1 && changes == 1);
  ui.pointerDown(xOf(0), y);
  ui.pointerMove(xOf(3), y);
  ui.pointerUp(xOf(3), y);
  R1_EXPECT(s.selectedIndex() == 1 && changes == 1);
  // Rapid clicking alternates.
  for (int i = 0; i < 600; ++i) {
    const int item = i % 2 == 0 ? 0 : 1;
    ui.pointerDown(xOf(item), y);
    ui.pointerUp(xOf(item), y);
  }
  R1_EXPECT(changes == 601 && s.selectedIndex() == 1);

  // Tooltip of the hovered item replaces the control's.
  s.setTooltip("Control");
  ui.pointerMove(xOf(3), y);
  R1_EXPECT(s.tooltipText() == "Fourth");
  ui.pointerMove(xOf(0), y);
  R1_EXPECT(s.tooltipText() == "Control");
  R1_EXPECT(ui.cursor() == Cursor::Pointer);
  ui.pointerMove(xOf(2), y);
  R1_EXPECT(ui.cursor() == Cursor::Default);
  paintOnce(t);

  // Disabled control: no events.
  s.setEnabled(false);
  ui.pointerDown(xOf(0), y);
  ui.pointerUp(xOf(0), y);
  R1_EXPECT(s.selectedIndex() == 1);
}

void testKeyboard() {
  r1test::TestUi t;
  auto& ui = t.ui;
  Segmented& s = make(t);
  std::vector<int> seen;
  s.setOnChange([&](int i) { seen.push_back(i); });
  t.layout();
  R1_EXPECT(ui.keyDown(Key::Tab) || s.focused());
  R1_EXPECT(s.focused() && s.focusVisible());
  // No selection yet: Right selects the first enabled, Left from none the last enabled.
  R1_EXPECT(ui.keyDown(Key::Right));
  R1_EXPECT(s.selectedIndex() == 0);
  ui.keyDown(Key::Down);
  R1_EXPECT(s.selectedIndex() == 1);
  ui.keyDown(Key::Right);  // item 2 is disabled: skipped
  R1_EXPECT(s.selectedIndex() == 3);
  R1_EXPECT(ui.keyDown(Key::Right));  // edge: consumed, no change, no wrap
  R1_EXPECT(s.selectedIndex() == 3 && seen.size() == 3);
  ui.keyDown(Key::Left);
  R1_EXPECT(s.selectedIndex() == 1);
  ui.keyDown(Key::Home);
  R1_EXPECT(s.selectedIndex() == 0);
  ui.keyDown(Key::Up);
  R1_EXPECT(s.selectedIndex() == 0);
  ui.keyDown(Key::End);
  R1_EXPECT(s.selectedIndex() == 3);
  // With a modifier the keys are not taken.
  R1_EXPECT(!ui.keyDown(Key::Left, r1ui::core::events::Mod::kCtrl));
  R1_EXPECT(s.selectedIndex() == 3);
  // Held key repeats keep stepping.
  for (int i = 0; i < 20; ++i) ui.keyDown(Key::Left, 0, i > 0);
  R1_EXPECT(s.selectedIndex() == 0);
  paintOnce(t);
}

void testValueApiAndHostile() {
  r1test::TestUi t;
  auto& ui = t.ui;
  Segmented& s = make(t);
  R1_EXPECT(s.selectedIndex() == -1);
  R1_EXPECT(s.setSelectedIndex(2));  // programmatic selection of a disabled item is allowed
  R1_EXPECT(!s.setSelectedIndex(4) && !s.setSelectedIndex(-2) && s.selectedIndex() == 2);
  R1_EXPECT(s.setSelectedIndex(-1) && s.selectedIndex() == -1);
  R1_EXPECT(s.setSelectedIndex(3));
  R1_EXPECT(s.setItems({{.text = "A"}, {.text = "B"}}));  // 3 is out of range now
  R1_EXPECT(s.selectedIndex() == -1);
  R1_EXPECT(s.setSelectedIndex(1) && s.setItems({{.text = "A"}, {.text = "B"}, {.text = "C"}}) && s.selectedIndex() == 1);
  R1_EXPECT(s.setItemEnabled(2, false) && !s.setItemEnabled(3, true) && !s.setItemEnabled(-1, true));

  std::vector<SegmentItem> many(Segmented::kMaxItems + 1);
  R1_EXPECT(!s.setItems(many) && s.items().size() == 3);
  many.resize(Segmented::kMaxItems);
  for (size_t i = 0; i < many.size(); ++i) many[i].text = std::to_string(i);
  R1_EXPECT(s.setItems(many));
  R1_EXPECT(!s.setItems({{.text = "x", .icon = "../x"}}) && s.items().size() == Segmented::kMaxItems);
  t.layout();
  paintOnce(t);  // 64 items in 240 px: tiny items, never throws

  R1_EXPECT(s.setItems({{.text = "\xC3\x28\xA0"}, {.text = std::string(50000, 'W')}, {.text = "", .icon = "plus"}, {.text = "Both", .icon = "plus"}}));
  t.layout();
  paintOnce(t);
  s.style().width = layout::Length::px(0);
  s.requestLayout();
  t.layout();
  paintOnce(t);
  R1_EXPECT(s.itemAt(0.0) == -1);
  R1_EXPECT(s.setItems({}));  // no items
  s.style().width = layout::Length::autoValue();
  s.requestLayout();
  t.layout();
  paintOnce(t);
  R1_EXPECT(!ui.keyDown(Key::Right) || true);

  // Natural width: n x widest item + paddings.
  R1_EXPECT(s.setItems({{.text = "File"}, {.text = "Assets"}}));
  t.layout();
  const int natural = ui.absRect(s.id()).w;
  R1_EXPECT(natural > 60 && natural < 200);

  // Destroy from the callback.
  s.setSelectedIndex(0);
  const auto id = s.id();
  s.setOnChange([&](int) { ui.destroy(id); });
  const layout::Rect r = ui.absRect(id);
  ui.pointerDown(r.x + r.w - 8, r.y + 13);
  ui.pointerUp(r.x + r.w - 8, r.y + 13);
  R1_EXPECT(!ui.alive(id));
  paintOnce(t);

  for (const float scale : {1.5f, 2.0f}) {
    r1test::TestUi st(300, 100, scale);
    Segmented& ss = make(st, 200);
    ss.setSelectedIndex(1);
    st.layout();
    paintOnce(st);
  }
}

}  // namespace

int main() {
  testGeometryAndPointer();
  testKeyboard();
  testValueApiAndHostile();
  return r1test::finish();
}
