// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for Checkbox: toggling by click and by Space/Enter on key-up, mixed becoming
//   checked, the change callback only for user changes, label in the hit area and in measure, the
//   13 px box, disabled behaviour, destroy from the callback, rapid toggling and hostile labels
//   (invalid UTF-8, empty, huge) and sizes.
// Callers: CTest (checkbox fast, no GPU).
#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/checkbox/Checkbox.h"

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

void testToggle() {
  r1test::TestUi t;
  auto& ui = t.ui;
  ui.rootStyle().alignItems = layout::Align::Start;
  Checkbox& c = ui.create<Checkbox>(ui.root());
  int changes = 0;
  bool last = false;
  c.setOnChange([&](bool v) {
    ++changes;
    last = v;
  });
  t.layout();
  R1_EXPECT(ui.absRect(c.id()).w == 13 && ui.absRect(c.id()).h == 13);
  const layout::Rect r = ui.absRect(c.id());
  const double cx = r.x + 6, cy = r.y + 6;
  ui.pointerDown(cx, cy);
  ui.pointerUp(cx, cy);
  R1_EXPECT(c.checked() && changes == 1 && last);
  ui.pointerDown(cx, cy);
  ui.pointerUp(cx, cy);
  R1_EXPECT(!c.checked() && changes == 2 && !last);

  // Programmatic changes are silent; mixed becomes checked on activation.
  c.setChecked(true);
  c.setChecked(false);
  R1_EXPECT(changes == 2);
  c.setMixed(true);
  R1_EXPECT(c.mixed() && !c.checked());
  ui.router().focus(c.id(), FocusReason::Keyboard);
  ui.keyDown(Key::Space);
  R1_EXPECT(changes == 2);  // toggles on key-up
  ui.keyUp(Key::Space);
  R1_EXPECT(c.checked() && !c.mixed() && changes == 3);
  ui.keyDown(Key::Enter);
  ui.keyUp(Key::Enter);
  R1_EXPECT(!c.checked() && changes == 4);
  c.setChecked(true);
  R1_EXPECT(!c.mixed());
  paintOnce(t);

  // Rapid toggling keeps the parity.
  const int before = changes;
  for (int i = 0; i < 1001; ++i) {
    ui.pointerDown(cx, cy);
    ui.pointerUp(cx, cy);
  }
  R1_EXPECT(changes == before + 1001 && !c.checked());

  // Disabled: no toggle, 50% opacity.
  c.setEnabled(false);
  ui.pointerDown(cx, cy);
  ui.pointerUp(cx, cy);
  R1_EXPECT(changes == before + 1001 && c.paintOpacity() == 0.5f);
}

void testLabel() {
  r1test::TestUi t;
  auto& ui = t.ui;
  ui.rootStyle().alignItems = layout::Align::Start;
  Checkbox& c = ui.create<Checkbox>(ui.root(), "Clip content");
  t.layout();
  const layout::Rect r = ui.absRect(c.id());
  R1_EXPECT(r.h == 16 && r.w > 13 + 8 + 40);
  R1_EXPECT(c.accessibleName() == "Clip content");
  // A click on the label toggles (the label is part of the control).
  ui.pointerDown(r.x + r.w - 3, r.y + 8);
  ui.pointerUp(r.x + r.w - 3, r.y + 8);
  R1_EXPECT(c.checked());
  c.setChecked(false);

  for (const std::string label : {std::string("\xC3\x28\xA0\xA1"), std::string(), std::string(100000, 'm')}) {
    c.setLabel(label);
    c.style().maxWidth = layout::Length::px(150);
    c.requestLayout();
    t.layout();
    R1_EXPECT(ui.absRect(c.id()).w <= 150);
    paintOnce(t);
  }
  c.style().width = layout::Length::px(0);
  c.requestLayout();
  t.layout();
  paintOnce(t);

  // Destroy from the callback.
  const auto id = c.id();
  c.setOnChange([&](bool) { ui.destroy(id); });
  c.style().width = layout::Length::autoValue();
  c.requestLayout();
  t.layout();
  const layout::Rect r2 = ui.absRect(id);
  ui.pointerDown(r2.x + 2, r2.y + 2);
  ui.pointerUp(r2.x + 2, r2.y + 2);
  R1_EXPECT(!ui.alive(id));
  paintOnce(t);

  for (const float scale : {1.25f, 2.0f}) {
    r1test::TestUi s(200, 100, scale);
    Checkbox& sc = s.ui.create<Checkbox>(s.ui.root(), "x");
    sc.setChecked(true);
    s.layout();
    paintOnce(s);
  }
}

}  // namespace

int main() {
  testToggle();
  testLabel();
  return r1test::finish();
}
