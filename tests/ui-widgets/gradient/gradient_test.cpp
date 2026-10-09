// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: behaviour oracle for the gradient editor: measured geometry, adding a stop by clicking the
//   bar (look unchanged, one gesture with the drag that follows), dragging handles, drag-off removal,
//   Escape / capture loss restoring the start gradient, keyboard editing of the bar, the stop rows
//   (position, hex and opacity fields, selection by press, Delete), the type control, angle and
//   centre fields, colour editing through the embedded picker, begin / end bracketing of every
//   gesture, the stop bounds, overlapping handles, and hostile input (NaN text, 64 stops, widgets
//   destroyed inside callbacks, scales 1 / 1.5 / 2).
// Callers: CTest (gradient, fast tier, no GPU; paint is exercised on the recording Painter).
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/gradient/GradientEditor.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Key;
namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;

struct Recorder {
  int begins = 0;
  int ends = 0;
  std::vector<GradientChange> changes;
  std::vector<uint32_t> selections;
};

struct Fixture {
  r1test::TestUi t{400, 900};
  GradientEditor* editor = nullptr;
  Recorder rec;
  explicit Fixture(float scale = 1.0f) : t(scale == 1.0f ? 400 : 700, scale == 1.0f ? 900 : 1500, scale) {
    t.ui.rootStyle().alignItems = layout::Align::Start;
    editor = &t.ui.create<GradientEditor>(t.ui.root());
    editor->onBeginInteraction = [this] { ++rec.begins; };
    editor->onEndInteraction = [this] { ++rec.ends; };
    editor->onChanged = [this](const GradientChange& c) { rec.changes.push_back(c); };
    editor->onSelectionChanged = [this](uint32_t id) { rec.selections.push_back(id); };
    t.layout();
  }
  layout::Rect rect(r1ui::core::tree::WidgetId id) { return t.ui.absRect(id); }
  layout::Rect barRect() { return rect(editor->bar().id()); }
  // The gradient itself is the bar widget minus the 7 px overhang of the end handles on each side.
  double trackW() { return barRect().w - 2.0 * GradientBar::kOverhang; }
  bool balanced() const { return rec.begins == rec.ends && !editor->gestureOpen(); }
  void clear() { rec = {}; }
  // Pointer helpers in bar coordinates (logical px from the bar's top-left).
  void move(double x, double y) {
    const layout::Rect b = barRect();
    t.ui.pointerMove(b.x + GradientBar::kOverhang + x, b.y + y);
  }
  void down(double x, double y) {
    const layout::Rect b = barRect();
    t.ui.pointerMove(b.x + GradientBar::kOverhang + x, b.y + y);
    t.ui.pointerDown(b.x + GradientBar::kOverhang + x, b.y + y);
  }
  void up(double x, double y) {
    const layout::Rect b = barRect();
    t.ui.pointerUp(b.x + GradientBar::kOverhang + x, b.y + y);
  }
};

void paintOnce(r1test::TestUi& t) {
  r1ui::render::Painter painter;
  painter.begin(static_cast<uint32_t>(t.ui.viewportWidth() * t.ui.scale()), static_cast<uint32_t>(t.ui.viewportHeight() * t.ui.scale()));
  t.ui.paint(painter);
  t.ui.finishPaint();
  painter.end();
}

void typeInto(Fixture& f, PickerEntry& e, std::string_view text) {
  f.t.ui.router().focus(e.id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(static_cast<Key>('A'), events::Mod::kCtrl);
  for (char c : text) f.t.ui.textInput(static_cast<char32_t>(c));
  f.t.ui.keyDown(Key::Enter);
}

bool nearD(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

void testGeometry() {
  Fixture f;
  f.editor->setShowGeometryFields(false);
  f.t.layout();
  const layout::Rect root = f.rect(f.editor->id());
  const layout::Rect bar = f.barRect();
  R1_EXPECT(root.w == 240 && bar.w == 222 + 14 && bar.h == 24 && bar.x == root.x + 9 - 7);
  const layout::Rect select = f.rect(f.editor->typeSelect().id());
  R1_EXPECT(select.w == 112 && select.h == 26 && select.y + 26 + 8 == bar.y);
  R1_EXPECT(f.editor->rowCount() == 2);
  const layout::Rect r0 = f.rect(f.editor->row(0).id());
  const layout::Rect r1 = f.rect(f.editor->row(1).id());
  R1_EXPECT(r0.w == 222 && r0.h == 30 && r1.y == r0.y + 30);
  // Stops header (16.5 px) + 4 gap between the bar's 8 px gap and the first row.
  R1_EXPECT(r0.y - bar.y - 24 >= 8 + 16 + 4 && r0.y - bar.y - 24 <= 8 + 17 + 4);
  const layout::Rect hex = f.rect(f.editor->row(0).hexEntry().id());
  R1_EXPECT(hex.w == 70 && hex.h == 22);
  const layout::Rect picker = f.rect(f.editor->picker().id());
  R1_EXPECT(picker.w == 222 && picker.y > r1.y + 30);
  paintOnce(f.t);
  // The geometry row appears when asked for.
  f.editor->setShowGeometryFields(true);
  f.t.layout();
  R1_EXPECT(f.barRect().y > bar.y);
}

void testSetGradientIsSilent() {
  Fixture f;
  gradient::Gradient g = gradient::Gradient::fromStops({{0, {{1, 0, 0}, 1}}, {0.4, {{0, 1, 0}, 1}}, {1, {{0, 0, 1}, 0.5}}});
  f.editor->setGradient(g);
  R1_EXPECT(f.rec.begins == 0 && f.rec.changes.empty() && f.rec.selections.empty());
  R1_EXPECT(f.editor->rowCount() == 3 && f.editor->gradient() == g);
  R1_EXPECT(f.editor->selectedStopId() == g.stops()[0].id);  // an unknown selection falls back to the first stop
  f.editor->setSelectedStop(g.stops()[2].id);
  R1_EXPECT(f.editor->selectedStopId() == g.stops()[2].id && f.rec.selections.empty());
  f.t.layout();
  R1_EXPECT(f.editor->row(2).hasState(StateFlag::kSelected) && !f.editor->row(0).hasState(StateFlag::kSelected));
  // The picker shows the selected stop's colour (blue at 50 %).
  R1_EXPECT(f.editor->picker().color().rgb.b == 1.0 && f.editor->picker().color().a == 0.5);
  f.editor->setSelectedStop(99999);  // unknown: ignored
  R1_EXPECT(f.editor->selectedStopId() == g.stops()[2].id);
  f.editor->setGradient(gradient::Gradient());  // the selected stop id no longer exists
  R1_EXPECT(f.editor->selectedStopId() == f.editor->gradient().stops()[0].id);
  paintOnce(f.t);
}

void testAddByClickAndDrag() {
  Fixture f;
  const gradient::Gradient before = f.editor->gradient();
  const double w = f.trackW();
  f.down(w * 0.5, 12);
  R1_EXPECT(f.rec.begins == 1 && f.rec.ends == 0 && f.editor->gradient().size() == 3);
  const uint32_t added = f.editor->selectedStopId();
  R1_EXPECT(added != before.stops()[0].id && added != before.stops()[1].id);
  R1_EXPECT(nearD(f.editor->gradient().find(added)->position, 0.5, 0.01));
  // The look is unchanged by adding a stop.
  R1_EXPECT(nearD(f.editor->gradient().evaluate(0.25).rgb.r, before.evaluate(0.25).rgb.r, 1e-6));
  R1_EXPECT(f.rec.changes.back().interactive && !f.rec.selections.empty() && f.rec.selections.back() == added);
  f.move(w * 0.7, 12);  // continues as a drag of the new stop
  R1_EXPECT(nearD(f.editor->gradient().find(added)->position, 0.7, 0.01));
  f.up(w * 0.7, 12);
  R1_EXPECT(f.balanced() && f.rec.begins == 1 && f.editor->gradient().size() == 3);
  // The new stop is in the list and the picker edits it.
  R1_EXPECT(f.editor->rowCount() == 3 && f.editor->row(1).stopId() == added);
  // The last change of the gesture is still flagged interactive: the end comes separately.
  R1_EXPECT(f.rec.changes.back().interactive);
}

void testHandleDragSelectAndCancel() {
  Fixture f;
  const uint32_t first = f.editor->gradient().stops()[0].id;
  const uint32_t second = f.editor->gradient().stops()[1].id;
  const double w = f.trackW();
  R1_EXPECT(f.editor->bar().selectedId() == first);
  f.down(w, 12);  // the second handle sits on the right edge
  R1_EXPECT(f.editor->selectedStopId() == second && f.rec.selections.size() == 1);
  f.move(w * 0.6, 12);
  R1_EXPECT(nearD(f.editor->gradient().find(second)->position, 0.6, 0.01));
  const gradient::Gradient start = gradient::Gradient();
  R1_EXPECT(f.t.ui.keyDown(Key::Escape));  // restores the start gradient and selection
  R1_EXPECT(f.editor->gradient().find(second)->position == 1.0 && f.balanced());
  f.up(w * 0.6, 12);  // late release: ignored
  R1_EXPECT(f.balanced() && f.editor->gradient().find(second)->position == 1.0);
  (void)start;

  // Capture loss also restores.
  f.clear();
  f.down(0, 12);
  f.move(w * 0.3, 12);
  R1_EXPECT(f.editor->gradient().find(first)->position > 0.2);
  f.t.ui.router().cancelPointerInteraction();
  R1_EXPECT(f.editor->gradient().find(first)->position == 0.0 && f.rec.begins == f.rec.ends && f.rec.begins == 1);

  // Dragging past a neighbour re-sorts the stops and keeps the identity.
  f.clear();
  f.down(0, 12);
  f.move(w, 12);
  R1_EXPECT(f.editor->gradient().indexOf(first) == 1);
  f.up(w, 12);
  R1_EXPECT(f.editor->selectedStopId() == first && f.balanced() && f.editor->row(1).stopId() == first);
  // Dragging beyond the bar clamps.
  f.down(w, 12);
  f.move(w + 500, 12);
  R1_EXPECT(f.editor->gradient().find(first)->position == 1.0);
  f.up(w + 500, 12);
}

void testDragOffRemoves() {
  Fixture f;
  const double w = f.trackW();
  f.down(w * 0.5, 12);  // add a middle stop (3 stops)
  const uint32_t mid = f.editor->selectedStopId();
  f.move(w * 0.5, 12 + 40);  // far below the bar
  R1_EXPECT(f.editor->gradient().size() == 3);  // removal happens on release
  f.up(w * 0.5, 12 + 40);
  R1_EXPECT(f.editor->gradient().size() == 2 && f.editor->gradient().indexOf(mid) == gradient::Gradient::npos && f.balanced());
  R1_EXPECT(f.editor->gradient().indexOf(f.editor->selectedStopId()) != gradient::Gradient::npos);
  // With two stops left a drag-off never removes.
  f.down(0, 12);
  f.move(0, -60);
  f.up(0, -60);
  R1_EXPECT(f.editor->gradient().size() == 2);
  // Coming back before releasing keeps the stop.
  f.down(w * 0.5, 12);
  f.move(w * 0.5, 80);
  f.move(w * 0.4, 12);
  f.up(w * 0.4, 12);
  R1_EXPECT(f.editor->gradient().size() == 3);
}

void testBarKeyboard() {
  Fixture f;
  GradientBar& bar = f.editor->bar();
  f.t.ui.router().focus(bar.id(), events::FocusReason::Keyboard);
  const uint32_t first = f.editor->gradient().stops()[0].id;
  f.t.ui.keyDown(Key::Right);
  R1_EXPECT(nearD(f.editor->gradient().find(first)->position, 0.01) && f.rec.begins == 1 && f.rec.ends == 1 && f.rec.changes.size() == 1);
  f.t.ui.keyDown(Key::Right, events::Mod::kShift);
  R1_EXPECT(nearD(f.editor->gradient().find(first)->position, 0.11));
  f.t.ui.keyDown(Key::End);
  R1_EXPECT(f.editor->gradient().find(first)->position == 1.0);
  f.t.ui.keyDown(Key::Home);
  R1_EXPECT(f.editor->gradient().find(first)->position == 0.0);
  const size_t before = f.rec.changes.size();
  f.t.ui.keyDown(Key::Left);  // already at the start: no change
  R1_EXPECT(f.rec.changes.size() == before && f.balanced());
  // PageDown selects the next stop; Delete removes down to two stops only.
  f.t.ui.keyDown(Key::PageDown);
  R1_EXPECT(f.editor->selectedStopId() != first);
  f.t.ui.keyDown(Key::Delete);
  R1_EXPECT(f.editor->gradient().size() == 2);
  f.editor->setGradient(gradient::Gradient::fromStops({{0, {{1, 0, 0}, 1}}, {0.5, {{0, 1, 0}, 1}}, {1, {{0, 0, 1}, 1}}}));
  f.t.layout();
  f.editor->setSelectedStop(f.editor->gradient().stops()[1].id);
  f.t.ui.router().focus(bar.id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(Key::Backspace);
  R1_EXPECT(f.editor->gradient().size() == 2 && f.editor->rowCount() == 2);
  f.t.ui.keyDown(Key::Delete);  // at the minimum: nothing
  R1_EXPECT(f.editor->gradient().size() == 2 && f.balanced());
}

void testRows() {
  Fixture f;
  const uint32_t first = f.editor->gradient().stops()[0].id;
  const uint32_t second = f.editor->gradient().stops()[1].id;
  GradientStopRow& row0 = f.editor->row(0);
  R1_EXPECT(row0.positionEntry().text() == "0" && row0.hexEntry().text() == "D4D4D4" && row0.opacityEntry().text() == "100");
  f.clear();
  typeInto(f, f.editor->row(0).positionEntry(), "40");
  R1_EXPECT(nearD(f.editor->gradient().find(first)->position, 0.4) && f.rec.begins == 1 && f.rec.ends == 1);
  typeInto(f, f.editor->row(0).positionEntry(), "250");  // clamps to 100 %: moves past nothing, then re-sorts
  R1_EXPECT(f.editor->gradient().find(first)->position == 1.0 && f.editor->gradient().indexOf(first) == 1);
  R1_EXPECT(f.editor->row(1).stopId() == first);  // rows follow the order
  typeInto(f, f.editor->row(1).positionEntry(), "xx");  // rejected
  R1_EXPECT(f.editor->row(1).positionEntry().text() == "100" && f.editor->gradient().find(first)->position == 1.0);
  // Hex and opacity.
  typeInto(f, f.editor->row(0).hexEntry(), "#ff0000");
  R1_EXPECT(f.editor->gradient().find(second)->color.rgb.r == 1.0 && f.editor->gradient().find(second)->color.rgb.g == 0.0);
  typeInto(f, f.editor->row(0).opacityEntry(), "50");
  R1_EXPECT(nearD(f.editor->gradient().find(second)->color.a, 0.5));
  typeInto(f, f.editor->row(0).hexEntry(), "00ff0080");  // eight digits also set the opacity
  R1_EXPECT(f.editor->gradient().find(second)->color.rgb.g == 1.0 && nearD(f.editor->gradient().find(second)->color.a, 128.0 / 255));
  typeInto(f, f.editor->row(0).hexEntry(), "12345");
  R1_EXPECT(f.editor->row(0).hexEntry().text() == "00FF00");
  R1_EXPECT(f.balanced());
  // Editing a field selects its stop and the picker follows.
  R1_EXPECT(f.editor->selectedStopId() == second);
  typeInto(f, f.editor->row(1).positionEntry(), "100");
  R1_EXPECT(f.editor->selectedStopId() == first);
  R1_EXPECT(f.editor->picker().color().rgb.r == 212.0 / 255);

  // A press on a row selects it without being consumed; Delete on the focused row removes the stop.
  f.editor->setGradient(gradient::Gradient::fromStops({{0, {{1, 0, 0}, 1}}, {0.5, {{0, 1, 0}, 1}}, {1, {{0, 0, 1}, 1}}}));
  f.t.layout();
  const layout::Rect r = f.rect(f.editor->row(2).id());
  f.t.ui.pointerMove(r.x + 77, r.y + 15);  // on the swatch (not an entry)
  f.t.ui.pointerDown(r.x + 77, r.y + 15);
  f.t.ui.pointerUp(r.x + 77, r.y + 15);
  R1_EXPECT(f.editor->selectedStopId() == f.editor->gradient().stops()[2].id);
  f.t.ui.router().focus(f.editor->row(1).id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(Key::Delete);
  R1_EXPECT(f.editor->gradient().size() == 2 && f.editor->rowCount() == 2);
}

void testPickerEditsSelectedStop() {
  Fixture f;
  const uint32_t first = f.editor->gradient().stops()[0].id;
  const uint32_t second = f.editor->gradient().stops()[1].id;
  f.clear();
  // Drag in the picker's square: one gesture, interactive changes, the stop's colour follows.
  const layout::Rect sv = f.rect(f.editor->picker().square().id());
  f.t.ui.pointerMove(sv.x + sv.w - 1, sv.y + 1);
  f.t.ui.pointerDown(sv.x + sv.w - 1, sv.y + 1);
  R1_EXPECT(f.rec.begins == 1 && f.editor->gradient().find(first)->color.rgb.r > 0.99);
  f.t.ui.pointerMove(sv.x + sv.w * 0.5, sv.y + sv.h * 0.5);
  f.t.ui.pointerUp(sv.x + sv.w * 0.5, sv.y + sv.h * 0.5);
  R1_EXPECT(f.balanced() && f.rec.changes.size() >= 2);
  R1_EXPECT(f.editor->gradient().find(second)->color.rgb.r == 1.0 && f.editor->gradient().find(second)->color.rgb.b == 1.0);  // untouched
  R1_EXPECT(f.editor->row(0).hexEntry().text() == color::formatHex(f.editor->gradient().find(first)->color));
  // Selecting the other stop shows its colour in the picker without changing anything.
  f.clear();
  f.editor->bar();
  f.down(f.trackW(), 12);
  f.up(f.trackW(), 12);
  R1_EXPECT(f.editor->selectedStopId() == second && f.editor->picker().color().rgb.r == 1.0);
  R1_EXPECT(f.editor->gradient().find(first)->color.rgb.r < 1.0);
  // Alpha through the picker.
  typeInto(f, f.editor->picker().alphaRow().entry(), "30");
  R1_EXPECT(nearD(f.editor->gradient().find(second)->color.a, 0.3));
}

void testTypeAndGeometry() {
  Fixture f;
  std::vector<gradient::GradientType> types;
  f.editor->onChanged = [&](const GradientChange& c) { types.push_back(c.gradient.type()); };
  PickerDropdown& select = f.editor->typeSelect();
  f.t.ui.router().focus(select.id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(Key::Enter);
  f.t.layout();
  f.t.ui.keyDown(Key::Down);
  f.t.ui.keyDown(Key::Enter);
  R1_EXPECT(f.editor->gradient().type() == gradient::GradientType::Radial && types.size() == 1 && select.selected() == 1);
  f.t.layout();
  // Radial: centre fields, no angle field (display none leaves a zero rectangle).
  R1_EXPECT(f.rect(f.editor->angleEntry().id()).w == 0 && f.rect(f.editor->centerXEntry().id()).w > 0);
  typeInto(f, f.editor->centerXEntry(), "25");
  R1_EXPECT(nearD(f.editor->gradient().centerX(), 0.25) && f.editor->centerXEntry().text() == "25");
  typeInto(f, f.editor->centerYEntry(), "300");
  R1_EXPECT(f.editor->gradient().centerY() == 1.0 && f.editor->centerYEntry().text() == "100");
  typeInto(f, f.editor->centerYEntry(), "nan");
  R1_EXPECT(f.editor->gradient().centerY() == 1.0 && f.editor->centerYEntry().text() == "100");
  // Angular shows both; the angle wraps.
  f.editor->setTypeControl(GradientEditor::TypeControl::Buttons);
  f.t.layout();
  const layout::Rect tb = f.rect(f.editor->typeButton(gradient::GradientType::Angular).id());
  R1_EXPECT(tb.w == 24 && f.rect(f.editor->typeSelect().id()).w == 0);
  f.t.ui.pointerMove(tb.x + 4, tb.y + 4);
  f.t.ui.pointerDown(tb.x + 4, tb.y + 4);
  f.t.ui.pointerUp(tb.x + 4, tb.y + 4);
  R1_EXPECT(f.editor->gradient().type() == gradient::GradientType::Angular && f.editor->typeButton(gradient::GradientType::Angular).active());
  f.t.layout();
  typeInto(f, f.editor->angleEntry(), "-90");
  R1_EXPECT(f.editor->gradient().angleDegrees() == 270 && f.editor->angleEntry().text() == "270");
  typeInto(f, f.editor->angleEntry(), "1e999");  // not finite: rejected
  R1_EXPECT(f.editor->gradient().angleDegrees() == 270);
  R1_EXPECT(f.balanced());
}

void testAddButtonAndBounds() {
  Fixture f;
  f.clear();
  f.t.ui.router().focus(f.editor->addStopButton().id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(Key::Enter);
  R1_EXPECT(f.editor->gradient().size() == 3 && f.rec.begins == 1 && f.rec.ends == 1);
  R1_EXPECT(nearD(f.editor->gradient().find(f.editor->selectedStopId())->position, 0.5));
  f.t.ui.keyDown(Key::Enter);  // the next gap: 0 .. 0.5 or 0.5 .. 1 (the first widest)
  R1_EXPECT(f.editor->gradient().size() == 4 && nearD(f.editor->gradient().find(f.editor->selectedStopId())->position, 0.25));
  for (int i = 0; i < 100; ++i) f.t.ui.keyDown(Key::Enter);
  R1_EXPECT(f.editor->gradient().size() == gradient::kMaxStops && f.editor->rowCount() == gradient::kMaxStops && f.balanced());
  f.t.layout();
  paintOnce(f.t);
  // Clicking the bar when full adds nothing but still brackets the gesture.
  f.clear();
  f.down(f.trackW() * 0.123, 12);
  f.up(f.trackW() * 0.123, 12);
  R1_EXPECT(f.editor->gradient().size() == gradient::kMaxStops && f.balanced());
}

void testOverlappingHandles() {
  Fixture f;
  f.editor->setGradient(gradient::Gradient::fromStops({{0.5, {{1, 0, 0}, 1}}, {0.5, {{0, 1, 0}, 1}}, {0.5, {{0, 0, 1}, 1}}, {1, {{1, 1, 1}, 1}}}));
  f.t.layout();
  const double w = f.trackW();
  const uint32_t top = f.editor->gradient().stops()[2].id;  // drawn above the earlier ones
  f.editor->setSelectedStop(f.editor->gradient().stops()[3].id);
  f.down(w * 0.5, 12);
  R1_EXPECT(f.editor->selectedStopId() == top);
  f.up(w * 0.5, 12);
  // The selected stop wins a tie.
  const uint32_t lower = f.editor->gradient().stops()[0].id;
  f.editor->setSelectedStop(lower);
  f.down(w * 0.5, 12);
  R1_EXPECT(f.editor->selectedStopId() == lower);
  f.up(w * 0.5, 12);
  paintOnce(f.t);
}

void testHostileAndDestroy() {
  {
    Fixture f;
    f.editor->onBeginInteraction = [&] { f.t.ui.destroy(f.editor->id()); };
    // The editor is gone after the first press, so the pointer is driven by coordinates taken before.
    const layout::Rect b = f.barRect();
    const double x0 = b.x + GradientBar::kOverhang + f.trackW() * 0.5;
    const double y0 = b.y + 12;
    f.t.ui.pointerMove(x0, y0);
    f.t.ui.pointerDown(x0, y0);
    f.t.ui.pointerMove(x0 + 20, y0);
    f.t.ui.pointerUp(x0 + 20, y0);
    f.t.layout();
    paintOnce(f.t);
  }
  {
    Fixture f;
    f.editor->onChanged = [&](const GradientChange&) { f.t.ui.destroy(f.editor->id()); };
    typeInto(f, f.editor->row(0).positionEntry(), "30");
    f.t.layout();
    paintOnce(f.t);
  }
  {
    Fixture f;
    f.editor->onEndInteraction = [&] { f.t.ui.destroy(f.editor->id()); };
    f.t.ui.router().focus(f.editor->bar().id(), events::FocusReason::Keyboard);
    f.t.ui.keyDown(Key::Right);
    f.t.layout();
    paintOnce(f.t);
  }
  {
    Fixture f;
    f.editor->onSelectionChanged = [&](uint32_t) { f.t.ui.destroy(f.editor->id()); };
    const layout::Rect b = f.barRect();
    const double x0 = b.x + GradientBar::kOverhang + f.trackW();
    const double y0 = b.y + 12;
    f.t.ui.pointerMove(x0, y0);
    f.t.ui.pointerDown(x0, y0);
    f.t.ui.pointerUp(x0, y0);
    f.t.layout();
    paintOnce(f.t);
  }
  {
    // Hostile gradient values: NaN stops are cleaned by the model, the editor never sees them.
    Fixture f;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    f.editor->setGradient(gradient::Gradient::fromStops({{nan, {{nan, nan, nan}, nan}}, {2.0, {{9, -9, 9}, 9}}}));
    f.t.layout();
    paintOnce(f.t);
    R1_EXPECT(f.editor->rowCount() == 2);
  }
  for (const float scale : {1.5f, 2.0f}) {
    Fixture f(scale);
    const double w = f.trackW();
    f.down(w * 0.5, 12);
    f.up(w * 0.5, 12);
    R1_EXPECT(f.editor->gradient().size() == 3 && f.balanced());
    paintOnce(f.t);
  }
  {
    Fixture f;
    f.editor->setEnabled(false);
    f.t.layout();
    f.down(f.trackW() * 0.5, 12);
    f.up(f.trackW() * 0.5, 12);
    R1_EXPECT(f.editor->gradient().size() == 2 && f.rec.begins == 0);
  }
  {
    // Mode tabs: programmatic changes are silent, a click calls back once.
    Fixture f;
    std::vector<PickerMode> modes;
    f.editor->onModeChanged = [&](PickerMode m) { modes.push_back(m); };
    f.editor->setMode(PickerMode::Image);
    R1_EXPECT(modes.empty() && f.editor->modeTab(PickerMode::Image).active());
    const layout::Rect tab = f.rect(f.editor->modeTab(PickerMode::Solid).id());
    f.t.ui.pointerMove(tab.x + 5, tab.y + 5);
    f.t.ui.pointerDown(tab.x + 5, tab.y + 5);
    f.t.ui.pointerUp(tab.x + 5, tab.y + 5);
    R1_EXPECT(modes.size() == 1 && modes[0] == PickerMode::Solid);
  }
}

}  // namespace

int main() {
  testGeometry();
  testSetGradientIsSilent();
  testAddByClickAndDrag();
  testHandleDragSelectAndCancel();
  testDragOffRemoves();
  testBarKeyboard();
  testRows();
  testPickerEditsSelectedStop();
  testTypeAndGeometry();
  testAddButtonAndBounds();
  testOverlappingHandles();
  testHostileAndDestroy();
  return r1test::finish();
}
