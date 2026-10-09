// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for IconButton: measured boxes (sm 20, md 26, custom), active state, hover fill
//   rule per size, click / keyboard activation through the Pressable base, disabled opacity and
//   input, tooltip and accessible name, destroy from the callback, and hostile input (bad icon
//   names, NaN or huge sizes, a missing icon file reported by paint as the IconCache contract says).
// Callers: CTest (iconbutton fast, no GPU).
#include <cmath>
#include <limits>
#include <stdexcept>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/iconbutton/IconButton.h"

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

void testSizesAndState() {
  r1test::TestUi t;
  auto& ui = t.ui;
  ui.rootStyle().alignItems = layout::Align::Start;
  IconButton& sm = ui.create<IconButton>(ui.root(), "plus", IconButtonSize::Sm);
  IconButton& md = ui.create<IconButton>(ui.root(), "plus", IconButtonSize::Md);
  IconButton& custom = ui.create<IconButton>(ui.root(), "x", IconButtonSize::Md);
  R1_EXPECT(custom.setBoxSize(24) && custom.setIconSize(16));
  t.layout();
  R1_EXPECT(ui.absRect(sm.id()).w == 20 && ui.absRect(sm.id()).h == 20);
  R1_EXPECT(ui.absRect(md.id()).w == 26 && ui.absRect(md.id()).h == 26);
  R1_EXPECT(ui.absRect(custom.id()).w == 24);
  md.setSizeClass(IconButtonSize::Sm);
  t.layout();
  R1_EXPECT(ui.absRect(md.id()).w == 20);

  // Active maps to the selected style bit (accent border and glyph rows).
  R1_EXPECT(!sm.active());
  sm.setActive(true);
  R1_EXPECT(sm.active() && (sm.styleState() & r1ui::theme::State::kSelected) != 0);
  const auto& active = t.services.resolve("iconbtn", r1ui::theme::State::kSelected);
  const auto& idle = t.services.resolve("iconbtn", 0);
  R1_EXPECT(active.text.color.r != idle.text.color.r || active.text.color.b != idle.text.color.b);
  R1_EXPECT(active.border.color.a > 0 && idle.border.color.a == 0);
  paintOnce(t);

  sm.setTooltip("Apply variable");
  R1_EXPECT(sm.tooltipText() == "Apply variable");
  R1_EXPECT(sm.accessibleName() == "plus");
  sm.setAccessibleName("Add");
  R1_EXPECT(sm.accessibleName() == "Add");
}

void testActivation() {
  r1test::TestUi t;
  auto& ui = t.ui;
  ui.rootStyle().alignItems = layout::Align::Start;
  IconButton& b = ui.create<IconButton>(ui.root(), "plus");
  int clicks = 0;
  b.setOnClick([&] { ++clicks; });
  t.layout();
  const layout::Rect r = ui.absRect(b.id());
  const double cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  ui.pointerMove(cx, cy);
  ui.pointerDown(cx, cy);
  ui.pointerUp(cx, cy);
  R1_EXPECT(clicks == 1);
  ui.pointerDown(cx, cy);
  ui.pointerMove(cx + 50, cy);
  ui.pointerUp(cx + 50, cy);
  R1_EXPECT(clicks == 1);
  ui.router().clearFocus();  // the click focused it without indication
  ui.router().focus(b.id(), FocusReason::Keyboard);
  ui.keyDown(Key::Space);
  ui.keyUp(Key::Space);
  R1_EXPECT(clicks == 2 && b.focusVisible());
  b.setEnabled(false);
  ui.keyDown(Key::Space);
  ui.keyUp(Key::Space);
  ui.pointerDown(cx, cy);
  ui.pointerUp(cx, cy);
  R1_EXPECT(clicks == 2 && !b.click());
  R1_EXPECT(b.paintOpacity() == 0.5f);
  b.setEnabled(true);
  R1_EXPECT(b.paintOpacity() == 1.0f);

  // Destroying from the callback is safe.
  const auto id = b.id();
  b.setOnClick([&] { ui.destroy(id); });
  ui.pointerDown(cx, cy);
  ui.pointerUp(cx, cy);
  R1_EXPECT(!ui.alive(id));
  paintOnce(t);
}

void testHostile() {
  r1test::TestUi t;
  auto& ui = t.ui;
  ui.rootStyle().alignItems = layout::Align::Start;
  IconButton& bad = ui.create<IconButton>(ui.root(), "../secret");  // rejected: empty icon
  R1_EXPECT(bad.icon().empty());
  IconButton& b = ui.create<IconButton>(ui.root(), "plus");
  for (const char* name : {"a/b", "x.svg", "\xFF", "", "bad name"}) {
    const bool ok = b.setIcon(name);
    R1_EXPECT(ok == (std::string(name).empty()));
    b.setIcon("plus");
  }
  R1_EXPECT(!b.setBoxSize(std::numeric_limits<double>::quiet_NaN()) && !b.setBoxSize(0) && !b.setBoxSize(1e12));
  R1_EXPECT(!b.setIconSize(std::numeric_limits<double>::infinity()) && !b.setIconSize(-1));
  R1_EXPECT(!b.setRadius(std::numeric_limits<double>::quiet_NaN()));
  t.layout();
  R1_EXPECT(ui.absRect(b.id()).w == 26);
  paintOnce(t);

  // A well formed name without a file: paint reports it (IconCache contract) and keeps working after.
  b.setIcon("no-such-icon-in-assets");
  bool threw = false;
  try {
    paintOnce(t);  // an unknown icon is drawn as a placeholder square; painting never throws
  } catch (const std::exception&) {
    threw = true;
  }
  R1_EXPECT(!threw);
  R1_EXPECT(t.ui.icons().failedLoads() >= 1);
  b.setIcon("plus");
  paintOnce(t);

  // Scaled displays and a zero-size window.
  r1test::TestUi s(0, 0, 2.0f);
  s.ui.create<IconButton>(s.ui.root(), "plus");
  s.layout();
  paintOnce(s);
}

}  // namespace

int main() {
  testSizesAndState();
  testActivation();
  testHostile();
  return r1test::finish();
}
