// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for Switch: the measured sizes (sm 28 x 16, md 36 x 20), toggle by click and by
//   Space/Enter on key-up, mixed becoming on, silent programmatic changes, disabled behaviour (also
//   while pressed), animation settling at the target when the frame loop runs, destroy from the
//   callback and rapid toggling.
// Callers: CTest (switch fast, no GPU).
#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/switch/Switch.h"

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

}  // namespace

int main() {
  {
    r1test::TestUi t;
    auto& ui = t.ui;
    ui.rootStyle().alignItems = layout::Align::Start;
    Switch& sm = ui.create<Switch>(ui.root(), SwitchSize::Sm);
    Switch& md = ui.create<Switch>(ui.root(), SwitchSize::Md);
    int changes = 0;
    bool last = false;
    sm.setOnChange([&](bool v) {
      ++changes;
      last = v;
    });
    t.layout();
    R1_EXPECT(ui.absRect(sm.id()).w == 28 && ui.absRect(sm.id()).h == 16);
    R1_EXPECT(ui.absRect(md.id()).w == 36 && ui.absRect(md.id()).h == 20);
    md.setSize(SwitchSize::Sm);
    t.layout();
    R1_EXPECT(ui.absRect(md.id()).w == 28);

    const layout::Rect r = ui.absRect(sm.id());
    const double cx = r.x + 14, cy = r.y + 8;
    ui.pointerMove(cx, cy);
    ui.pointerDown(cx, cy);
    ui.pointerUp(cx, cy);
    R1_EXPECT(sm.checked() && changes == 1 && last);
    R1_EXPECT(ui.cursor() == Cursor::Pointer);
    sm.setChecked(false);
    R1_EXPECT(changes == 1 && !sm.checked());

    sm.setMixed(true);
    R1_EXPECT(sm.mixed());
    ui.router().focus(sm.id(), FocusReason::Keyboard);
    ui.keyDown(Key::Space);
    R1_EXPECT(changes == 1);
    ui.keyUp(Key::Space);
    R1_EXPECT(sm.checked() && !sm.mixed() && changes == 2);
    paintOnce(t);

    // Disabled while pressed: no change.
    ui.pointerDown(cx, cy);
    sm.setEnabled(false);
    ui.pointerUp(cx, cy);
    R1_EXPECT(changes == 2 && sm.checked());
    sm.setEnabled(true);

    for (int i = 0; i < 1000; ++i) {
      ui.pointerDown(cx, cy);
      ui.pointerUp(cx, cy);
    }
    R1_EXPECT(changes == 1002 && sm.checked());

    const auto id = sm.id();
    sm.setOnChange([&](bool) { ui.destroy(id); });
    ui.pointerDown(cx, cy);
    ui.pointerUp(cx, cy);
    R1_EXPECT(!ui.alive(id));
    paintOnce(t);
  }
  {
    // With a running frame loop the thumb animates and the state settles at the target.
    r1test::TestUi t;
    auto& ui = t.ui;
    ui.setAnimationsEnabled(true);
    ui.setFrameLoopRunning(true);
    ui.rootStyle().alignItems = layout::Align::Start;
    Switch& s = ui.create<Switch>(ui.root());
    t.layout();
    s.setChecked(true);
    for (uint64_t ms = 0; ms <= 400; ms += 16) {
      ui.setTime(ms);
      ui.tick();
      ui.frame();
      paintOnce(t);
    }
    R1_EXPECT(s.checked());
  }
  for (const float scale : {1.0f, 1.5f, 3.0f}) {
    r1test::TestUi t(100, 50, scale);
    t.ui.create<Switch>(t.ui.root(), SwitchSize::Md).setMixed(true);
    t.layout();
    paintOnce(t);
  }
  return r1test::finish();
}
