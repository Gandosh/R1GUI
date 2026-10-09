// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for the property-panel widgets: PropertySection geometry (26 px header, 12 px
//   side padding, 1 px separator), header actions (click, press-and-leave, keyboard, disabled,
//   destroy in the callback), collapse by click and by Enter / Space, PanelHeader (43 px, truncated
//   title), FieldGroup (label, 4 px gap) and FieldGrid (114 / 114 and rail columns of a 234 px
//   column), plus hostile text (empty, invalid UTF-8, very long) and zero-width layouts.
// Callers: CTest (section fast, no GPU).
#include <cmath>
#include <string>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/section/Section.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Key;
namespace layout = r1ui::core::layout;
using r1ui::core::tree::WidgetId;

class Fixed : public WidgetObject {
 public:
  explicit Fixed(double h = 26.0) : h_(h) {}
  const char* typeName() const override { return "Fixed"; }
  void onAttached() override { style().height = layout::Length::px(h_); }

 private:
  double h_;
};

void paintOnce(r1test::TestUi& t) {
  r1ui::render::Painter painter;
  painter.begin(static_cast<uint32_t>(t.ui.viewportWidth()), static_cast<uint32_t>(t.ui.viewportHeight()));
  t.ui.paint(painter);
  t.ui.finishPaint();
  painter.end();
}

void testSectionGeometry() {
  r1test::TestUi t(400, 400);
  auto& ui = t.ui;
  ui.rootStyle().direction = layout::FlexDirection::Column;
  ui.rootStyle().alignItems = layout::Align::Start;
  PropertySection& s = ui.create<PropertySection>(ui.root(), "Layout");
  s.style().width = layout::Length::px(258);
  ActionButton& add = s.addAction("plus", "Add", [](ActionButton&) {});
  ui.create<Fixed>(s.content());
  t.layout();
  const layout::Rect r = ui.absRect(s.id());
  R1_EXPECT(r.w == 258 && r.h == 1 + 26 + 26 + 6);  // measured: separator + header + one field row + bottom padding
  const layout::Rect h = ui.absRect(s.header());
  R1_EXPECT(h.h == 26 && h.y == r.y + 1);
  const layout::Rect b = ui.absRect(add.id());
  R1_EXPECT(b.w == 26 && b.h == 26 && b.x + b.w == r.x + r.w - 12);  // right edge sits on the 12 px padding
  const layout::Rect c = ui.absRect(s.content());
  R1_EXPECT(c.w == 258 && c.y == h.y + 26);
  R1_EXPECT(!s.collapsible() && !s.setCollapsed(true) && !s.collapsed());
  R1_EXPECT(add.tooltipText() == "Add" && add.accessibleName() == "Add");
  R1_EXPECT(s.accessibleName() == "Layout");
  s.setTitle("Position");
  R1_EXPECT(s.title() == "Position" && s.accessibleName() == "Position");
  paintOnce(t);

  PropertySection& flat = ui.create<PropertySection>(ui.root(), "No border", SectionOptions{.topBorder = false});
  flat.style().width = layout::Length::px(258);
  t.layout();
  R1_EXPECT(ui.absRect(flat.id()).h == 26 + 6);
}

void testCollapse() {
  r1test::TestUi t(400, 400);
  auto& ui = t.ui;
  ui.rootStyle().direction = layout::FlexDirection::Column;
  ui.rootStyle().alignItems = layout::Align::Start;
  PropertySection& s = ui.create<PropertySection>(ui.root(), "Appearance", SectionOptions{.collapsible = true});
  s.style().width = layout::Length::px(258);
  ui.create<Fixed>(s.content());
  t.layout();
  const int open = ui.absRect(s.id()).h;
  int toggles = 0;
  s.setOnToggle([&](PropertySection&) { ++toggles; });
  const layout::Rect h = ui.absRect(s.header());
  R1_EXPECT(ui.cursor() == Cursor::Default);
  ui.pointerMove(h.x + 30, h.y + 10);
  R1_EXPECT(ui.cursor() == Cursor::Pointer);
  ui.pointerDown(h.x + 30, h.y + 10);
  ui.pointerUp(h.x + 30, h.y + 10);
  t.layout();
  R1_EXPECT(s.collapsed() && toggles == 1 && ui.absRect(s.id()).h == 1 + 26 && open == 59);
  ui.pointerDown(h.x + 30, h.y + 10);
  ui.pointerUp(h.x + 30, h.y + 10);
  t.layout();
  R1_EXPECT(!s.collapsed() && ui.absRect(s.id()).h == open && toggles == 2);
  // Press on the header, release elsewhere: no toggle.
  ui.pointerDown(h.x + 30, h.y + 10);
  ui.pointerUp(h.x + 30, h.y + 200);
  R1_EXPECT(!s.collapsed());
  // Keyboard: the header is focusable; Enter toggles on key-up.
  R1_EXPECT(ui.router().focus(s.header(), r1ui::core::events::FocusReason::Keyboard));
  ui.keyDown(Key::Enter);
  R1_EXPECT(!s.collapsed());
  ui.keyUp(Key::Enter);
  R1_EXPECT(s.collapsed() && toggles == 3);
  ui.keyDown(Key::Space);
  ui.keyUp(Key::Space);
  R1_EXPECT(!s.collapsed());
  R1_EXPECT(s.setCollapsed(true) && !s.setCollapsed(true));
  // A callback that destroys the section while collapsing.
  const WidgetId id = s.id();
  s.setOnToggle([&](PropertySection&) { ui.destroy(id); });
  s.toggle();
  R1_EXPECT(!ui.alive(id));
  t.layout();
  paintOnce(t);
}

void testActionButton() {
  r1test::TestUi t(400, 400);
  auto& ui = t.ui;
  ui.rootStyle().direction = layout::FlexDirection::Column;
  ui.rootStyle().alignItems = layout::Align::Start;
  PropertySection& s = ui.create<PropertySection>(ui.root(), "Fill");
  s.style().width = layout::Length::px(258);
  int clicks = 0;
  ActionButton& b = s.addAction("plus", "Add fill", [&](ActionButton&) { ++clicks; });
  t.layout();
  const layout::Rect r = ui.absRect(b.id());
  const double cx = r.x + r.w / 2.0, cy = r.y + r.h / 2.0;
  ui.pointerMove(cx, cy);
  R1_EXPECT(b.hovered() && ui.cursor() == Cursor::Pointer);
  ui.pointerDown(cx, cy);
  R1_EXPECT(b.pressed());
  ui.pointerUp(cx, cy);
  R1_EXPECT(clicks == 1 && !b.pressed());
  // Press, leave, release: no click.
  ui.pointerDown(cx, cy);
  ui.pointerMove(cx + 100, cy + 100);
  ui.pointerUp(cx + 100, cy + 100);
  R1_EXPECT(clicks == 1);
  // Right button never activates.
  ui.pointerMove(cx, cy);
  ui.pointerDown(cx, cy, r1ui::core::events::Button::Right);
  ui.pointerUp(cx, cy, r1ui::core::events::Button::Right);
  R1_EXPECT(clicks == 1);
  // Keyboard: pressed look on key-down, fires on key-up, only when the press began here.
  ui.router().clearFocus();  // the earlier presses focused it without the indication
  ui.router().focus(b.id(), r1ui::core::events::FocusReason::Keyboard);
  R1_EXPECT(b.focusVisible());
  ui.keyUp(Key::Space);
  R1_EXPECT(clicks == 1);                       // an up without a down is ignored
  ui.keyDown(Key::Space);
  R1_EXPECT(b.styleState() & r1ui::theme::State::kActive);
  ui.keyDown(Key::Space, 0, true);              // auto-repeat does not fire or restart
  ui.keyUp(Key::Space);
  R1_EXPECT(clicks == 2);
  ui.keyDown(Key::Enter);
  ui.router().clearFocus();                     // focus lost between down and up cancels it
  ui.keyUp(Key::Enter);
  R1_EXPECT(clicks == 2);
  // Disabled: no hover look, no click, no focus; disabled while pressed cancels the click.
  ui.pointerMove(cx, cy);
  ui.pointerDown(cx, cy);
  b.setEnabled(false);
  ui.pointerUp(cx, cy);
  R1_EXPECT(clicks == 2 && b.paintOpacity() == 0.5f && ui.cursor() == Cursor::Default);
  R1_EXPECT(!b.activate());
  b.setEnabled(true);
  R1_EXPECT(b.activate() && clicks == 3);
  // A callback that destroys the button, and one that replaces its own callback.
  const WidgetId id = b.id();
  b.setOnActivate([&](ActionButton& self) {
    ui.destroy(self.id());
    ++clicks;
  });
  ui.pointerMove(cx, cy);
  ui.pointerDown(cx, cy);
  ui.pointerUp(cx, cy);
  R1_EXPECT(!ui.alive(id) && clicks == 4);
  ActionButton& again = s.addAction("x", "Again", [&](ActionButton& self) { self.setOnActivate({}); ++clicks; });
  t.layout();
  R1_EXPECT(again.activate() && clicks == 5 && !again.activate());
  again.setSize(-1, 5);
  again.setSize(std::nan(""), 5);
  again.setIcon("no-such-icon-name");
  t.layout();
  bool threw = false;
  try {
    paintOnce(t);
  } catch (const std::runtime_error&) {
    threw = true;  // an unknown icon is reported by the icon cache (the shell shows it), never a crash
  }
  R1_EXPECT(threw);
  again.setIcon("x");
  paintOnce(t);
}

void testPanelHeader() {
  r1test::TestUi t(400, 200);
  auto& ui = t.ui;
  ui.rootStyle().direction = layout::FlexDirection::Column;
  ui.rootStyle().alignItems = layout::Align::Start;
  PanelHeader& h = ui.create<PanelHeader>(ui.root(), "Rectangle", "square");
  h.style().width = layout::Length::px(258);
  ActionButton& more = h.addAction("shapes", "Component", [](ActionButton&) {});
  t.layout();
  const layout::Rect r = ui.absRect(h.id());
  R1_EXPECT(r.w == 258 && r.h == 43);
  const layout::Rect b = ui.absRect(more.id());
  R1_EXPECT(b.w == 26 && b.x + b.w == r.x + r.w - 12 && b.y == r.y + 8);
  R1_EXPECT(h.accessibleName() == "Rectangle");
  // Long title: the action stays at the right edge, the title truncates.
  h.setTitle(std::string(500, 'W'));
  t.layout();
  R1_EXPECT(ui.absRect(more.id()).x + 26 == r.x + r.w - 12);
  paintOnce(t);
  for (const std::string text : {std::string(), std::string("\xFF\xFE bad"), std::string("a\0b", 3), std::string("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9")}) {
    h.setTitle(text);
    t.layout();
    paintOnce(t);
  }
  h.style().width = layout::Length::px(0);
  h.requestLayout();
  t.layout();
  paintOnce(t);
}

void testFieldGroupAndGrid() {
  r1test::TestUi t(400, 400);
  auto& ui = t.ui;
  ui.rootStyle().direction = layout::FlexDirection::Column;
  ui.rootStyle().alignItems = layout::Align::Start;
  FieldGrid& grid = ui.create<FieldGrid>(ui.root());
  grid.style().width = layout::Length::px(234);
  const FieldGrid::Row two = grid.addRow(2, false);
  const FieldGrid::Row rail = grid.addRow(2, true);
  const FieldGrid::Row one = grid.addRow(1, false);
  const FieldGrid::Row clamped = grid.addRow(9, false);
  R1_EXPECT(!one.second.valid() && !two.rail.valid() && clamped.second.valid());
  FieldGroup& g = ui.create<FieldGroup>(two.first, "Blend mode");
  ui.create<Fixed>(g.control());
  ui.create<Fixed>(two.second);
  ui.create<Fixed>(rail.first);
  ui.create<Fixed>(rail.second);
  ui.create<Fixed>(rail.rail);
  ui.create<Fixed>(one.first);
  t.layout();
  R1_EXPECT(ui.absRect(two.first).w == 114 && ui.absRect(two.second).w == 114);
  R1_EXPECT(ui.absRect(two.second).x - ui.absRect(two.first).x == 114 + 6);
  R1_EXPECT(ui.absRect(rail.rail).w == 26 && ui.absRect(rail.first).w == (234 - 26 - 12) / 2);
  R1_EXPECT(ui.absRect(one.first).w == 234);
  // Label above the control with a 4 px gap: 11 + 4 + 26 = 41.
  R1_EXPECT(ui.absRect(g.id()).h == 41);
  R1_EXPECT(ui.absRect(g.control()).y - ui.absRect(g.id()).y == 15);
  R1_EXPECT(g.label() == "Blend mode" && g.accessibleName() == "Blend mode");
  g.setLabel("Opacity");
  R1_EXPECT(g.label() == "Opacity");
  // A grid wider or narrower than the column keeps the cells equal.
  grid.style().width = layout::Length::px(100);
  grid.requestLayout();
  t.layout();
  R1_EXPECT(std::abs(ui.absRect(two.first).w - ui.absRect(two.second).w) <= 1);
  grid.style().width = layout::Length::px(0);
  grid.requestLayout();
  t.layout();
  paintOnce(t);
}

}  // namespace

int main() {
  testSectionGeometry();
  testCollapse();
  testActionButton();
  testPanelHeader();
  testFieldGroupAndGrid();
  return r1test::finish();
}
