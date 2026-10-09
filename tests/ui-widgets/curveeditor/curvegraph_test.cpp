// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: behaviour oracle for the curve editor widget (CurveEditor / CurveGraph / CurveKeyFields)
//   through synthetic input: the acceptance scenarios of spec 11 (wheel zoom about the pointer, F to
//   frame, middle-click insertion as one undo step with and without a drag, Shift axis lock,
//   frame snapping, Escape cancelling a drag without a trace, select all + 5, ctrl+Right nudging,
//   additive and subtractive marquees, copy and paste at the scrub time), selection rules, tangent
//   handles, the begin / change / end interaction protocol, read-only curves, hover and tooltip,
//   the key fields, culling with 100000 keys, and hostile input (NaN data, destroyed widgets).
// Callers: CTest (curveeditor, fast tier, no GPU; paint runs on the recording Painter).
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/curveeditor/CurveEditor.h"

namespace {

using namespace r1ui::widgets;
using curve::Curve;
using curve::Part;
namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;

constexpr events::Key kKeyA = static_cast<events::Key>('A');
constexpr events::Key kKeyC = static_cast<events::Key>('C');
constexpr events::Key kKeyD = static_cast<events::Key>('D');
constexpr events::Key kKeyF = static_cast<events::Key>('F');
constexpr events::Key kKeyI = static_cast<events::Key>('I');
constexpr events::Key kKeyV = static_cast<events::Key>('V');
constexpr events::Key kKeyW = static_cast<events::Key>('W');
constexpr events::Key kKeyX = static_cast<events::Key>('X');
constexpr events::Key kDigit(int d) { return static_cast<events::Key>(48 + d); }

bool nearD(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

curve::Key makeKey(uint32_t id, double t, double v, curve::Interp interp = curve::Interp::Linear) {
  curve::Key k;
  k.id = id;
  k.time = t;
  k.value = v;
  k.interp = interp;
  return k;
}

Curve makeCurve(uint32_t id, const char* name, std::initializer_list<std::pair<double, double>> pts, curve::Interp interp = curve::Interp::Linear) {
  Curve c;
  c.id = id;
  c.name = name;
  uint32_t kid = 1;
  for (const auto& [t, v] : pts) c.keys.push_back(makeKey(kid++, t, v, interp));
  return c;
}

struct Log {
  std::vector<std::string> begins;
  int ends = 0;
  int committed = 0;
  int cancelled = 0;
  int changes = 0;
  int selections = 0;
  std::vector<CurveContext> contexts;
  double scrub = -1.0;
};

struct Fixture {
  r1test::TestUi t{900, 700};
  CurveEditor* editor = nullptr;
  Log log;
  Fixture() {
    t.ui.rootStyle().alignItems = layout::Align::Start;
    editor = &t.ui.create<CurveEditor>(t.ui.root());
    editor->style().width = layout::Length::px(800);
    editor->style().height = layout::Length::px(600);
    editor->onBeginInteraction = [this](const std::string& l) { log.begins.push_back(l); };
    editor->onChanged = [this](const std::vector<uint32_t>&) { ++log.changes; };
    editor->onEndInteraction = [this](bool c) {
      ++log.ends;
      (c ? log.committed : log.cancelled)++;
    };
    editor->onSelectionChanged = [this] { ++log.selections; };
    editor->onScrubChanged = [this](double v) { log.scrub = v; };
    editor->onContextMenu = [this](const CurveContext& c) { log.contexts.push_back(c); };
    t.layout();
    // Two curves, a 0..10 x -1..5 view.
    CurveGraph& g = graph();
    g.setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}, {6, 4}, {8, 2}}), makeCurve(2, "Beta", {{1, -0.5}, {5, 0.5}, {9, 1.5}})});
    g.setView({0.0, 10.0, -1.0, 5.0});
    t.layout();
  }
  CurveGraph& graph() const { return editor->graph(); }
  layout::Rect rect() { return t.ui.absRect(graph().id()); }
  double X(double time) { return rect().x + graph().mapping().toX(time); }
  double Y(double value) { return rect().y + graph().mapping().toY(value); }
  void down(double x, double y, events::Button b = events::Button::Left, uint8_t mods = 0) {
    t.ui.pointerMove(x, y, mods);
    t.ui.pointerDown(x, y, b, mods);
  }
  void move(double x, double y, uint8_t mods = 0) { t.ui.pointerMove(x, y, mods); }
  void up(double x, double y, events::Button b = events::Button::Left, uint8_t mods = 0) { t.ui.pointerUp(x, y, b, mods); }
  void click(double x, double y, uint8_t mods = 0) {
    down(x, y, events::Button::Left, mods);
    up(x, y, events::Button::Left, mods);
  }
  void clickKey(uint32_t curveId, uint32_t keyId, uint8_t mods = 0) {
    const curve::Point p = graph().keyPosition(curveId, keyId);
    click(rect().x + p.x, rect().y + p.y, mods);
  }
  const Curve& curve(uint32_t id) const { return *curve::findCurve(graph().curves(), id); }
  void focus() { t.ui.router().focus(graph().id(), events::FocusReason::Keyboard); }
  void key(events::Key k, uint8_t mods = 0) { t.ui.keyDown(k, mods); }
  void paint() {
    r1ui::render::Painter painter;
    painter.begin(static_cast<uint32_t>(t.ui.viewportWidth() * t.ui.scale()), static_cast<uint32_t>(t.ui.viewportHeight() * t.ui.scale()));
    t.ui.paint(painter);
    t.ui.finishPaint();
    painter.end();
  }
  bool balanced() const { return static_cast<int>(log.begins.size()) == log.ends && !graph().interactionOpen(); }
  void clearLog() { log = Log{}; }
};

// ---- view -------------------------------------------------------------------------------------

void testGeometryAndWheel() {
  Fixture f;
  const layout::Rect r = f.rect();
  R1_EXPECT(r.w == 800 && r.h > 400);
  const curve::Mapping m = f.graph().mapping();
  R1_EXPECT(m.y == CurveGraph::kRulerHeight && nearD(m.w, r.w) && nearD(m.h, r.h - CurveGraph::kRulerHeight));
  // Acceptance 1: wheel up once at the horizontal centre of a 0..10 view: 0.5 .. 9.5; the value range shrinks to 90 %.
  f.graph().setView({0.0, 10.0, 0.0, 4.0});
  f.t.layout();
  const double cx = r.x + r.w / 2.0;
  const double cy = r.y + CurveGraph::kRulerHeight + m.h / 2.0;
  f.t.ui.pointerMove(cx, cy);
  f.t.ui.wheel(cx, cy, 0.0, 1.0);
  const curve::View v = f.graph().view();
  R1_EXPECT(nearD(v.tMin, 0.5) && nearD(v.tMax, 9.5));
  R1_EXPECT(nearD(v.valueSpan(), 3.6, 1e-6) && nearD((v.vMin + v.vMax) / 2.0, 2.0, 1e-6));
  f.t.ui.wheel(cx, cy, 0.0, -1.0);  // back out by the matching amount
  R1_EXPECT(nearD(f.graph().view().timeSpan(), 10.0, 1e-9));
  // The pointer position stays fixed: zoom at the left edge keeps t = 0 at the left.
  f.graph().setView({0.0, 10.0, 0.0, 4.0});
  f.t.ui.pointerMove(r.x + 1, cy);
  f.t.ui.wheel(r.x + 1, cy, 0.0, 3.0);
  R1_EXPECT(f.graph().view().tMin < 0.02 && nearD(f.graph().view().timeSpan(), 10.0 * 0.9 * 0.9 * 0.9, 1e-9));
  // Wheel multiplier and the scrub-time anchor.
  CurveSettings s = f.graph().settings();
  s.wheelMultiplier = 2.0;
  s.zoomAtScrubTime = true;
  f.graph().setSettings(s);
  f.graph().setView({0.0, 10.0, 0.0, 4.0});
  f.graph().setScrubTime(2.0);
  f.t.ui.wheel(cx, cy, 0.0, 1.0);
  R1_EXPECT(nearD(f.graph().view().timeSpan(), 8.0) && nearD(f.graph().view().tMin, 2.0 - 2.0 * 0.8));  // anchored at the scrub time
  f.graph().setScrubTime(99.0);  // outside the view: the pointer is the anchor again
  f.graph().setView({0.0, 10.0, 0.0, 4.0});
  f.t.ui.wheel(cx, cy, 0.0, 1.0);
  R1_EXPECT(nearD((f.graph().view().tMin + f.graph().view().tMax) / 2.0, 5.0));
  // Hostile wheel deltas never invalidate the view.
  f.t.ui.wheel(cx, cy, 0.0, std::numeric_limits<double>::quiet_NaN());
  f.t.ui.wheel(cx, cy, 0.0, 1e300);
  f.t.ui.wheel(cx, cy, 0.0, -1e300);
  R1_EXPECT(curve::validView(f.graph().view()));
}

void testPanZoomAndContext() {
  Fixture f;
  const layout::Rect r = f.rect();
  f.graph().setView({0.0, 10.0, 0.0, 4.0});
  const double px = r.x + 400;
  const double py = r.y + 300;
  // Right drag pans (after the threshold) and sends no context request.
  f.down(px, py, events::Button::Right);
  f.move(px + 3, py);  // still inside the dead zone
  R1_EXPECT(f.graph().view().tMin == 0.0);
  f.move(px + 80, py - 40);
  R1_EXPECT(f.graph().view().tMin < 0.0 && f.graph().view().vMin < 0.0);  // the content follows the pointer: right and up
  const double pxPerUnit = 800.0 / 10.0;
  R1_EXPECT(nearD(f.graph().view().tMin, -80.0 / pxPerUnit, 1e-6));
  f.up(px + 80, py - 40, events::Button::Right);
  R1_EXPECT(f.log.contexts.empty());
  // Right click without a drag asks for a context menu, classified by what is under the pointer.
  f.graph().setView({0.0, 10.0, -1.0, 5.0});
  f.t.layout();
  f.down(r.x + 700, r.y + 500, events::Button::Right);
  f.up(r.x + 700, r.y + 500, events::Button::Right);
  R1_EXPECT(f.log.contexts.size() == 1 && f.log.contexts[0].target == CurveContext::Target::Empty);
  const curve::Point kp = f.graph().keyPosition(1, 2);
  f.down(r.x + kp.x, r.y + kp.y, events::Button::Right);
  f.up(r.x + kp.x, r.y + kp.y, events::Button::Right);
  R1_EXPECT(f.log.contexts.size() == 2 && f.log.contexts[1].target == CurveContext::Target::Key && f.log.contexts[1].keyId == 2 && f.graph().selection().containsKey(1, 2));
  const double ymid = f.Y(curve::evaluate(f.curve(1), 3.0));
  f.down(f.X(3.0), ymid + 2, events::Button::Right);
  f.up(f.X(3.0), ymid + 2, events::Button::Right);
  R1_EXPECT(f.log.contexts.size() == 3 && f.log.contexts[2].target == CurveContext::Target::Curve && f.log.contexts[2].curveId == 1);
  // Alt + right drag zooms about the press point; right = zoom in on time, up = zoom in on value.
  f.graph().setView({0.0, 10.0, 0.0, 4.0});
  const double zx = r.x + 400;
  const double zy = r.y + CurveGraph::kRulerHeight + 100;
  const curve::Mapping m0 = f.graph().mapping();
  const double tAnchor = m0.toTime(zx - r.x);
  f.down(zx, zy, events::Button::Right, events::Mod::kAlt);
  f.move(zx + 40, zy - 30, events::Mod::kAlt);
  f.up(zx + 40, zy - 30, events::Button::Right, events::Mod::kAlt);
  const curve::View v = f.graph().view();
  R1_EXPECT(v.timeSpan() < 10.0 && v.valueSpan() < 4.0);
  R1_EXPECT(nearD((tAnchor - v.tMin) / v.timeSpan(), (tAnchor - 0.0) / 10.0, 1e-6));  // the anchor keeps its place
  // Alt + middle also pans.
  f.graph().setView({0.0, 10.0, 0.0, 4.0});
  f.down(zx, zy, events::Button::Middle, events::Mod::kAlt);
  f.move(zx + 40, zy, events::Mod::kAlt);
  f.up(zx + 40, zy, events::Button::Middle, events::Mod::kAlt);
  R1_EXPECT(f.graph().view().tMin < 0.0);
  R1_EXPECT(f.log.begins.empty());  // navigation never opens an undo interaction
}

void testFraming() {
  Fixture f;
  // Select three keys of Alpha and press F: the view contains all three with a margin.
  f.clickKey(1, 2);
  f.clickKey(1, 3, events::Mod::kShift);
  f.clickKey(1, 4, events::Mod::kShift);
  f.focus();
  f.key(kKeyF);
  const curve::View v = f.graph().view();
  R1_EXPECT(v.tMin < 2.0 && v.tMax > 6.0 && v.vMin < 1.0 && v.vMax > 4.0 && v.timeSpan() < 10.0);
  // One key: the zoom scale is kept and the key is centred.
  f.clickKey(1, 4);
  const double spanT = f.graph().view().timeSpan();
  const double spanV = f.graph().view().valueSpan();
  f.key(kKeyF);
  R1_EXPECT(nearD(f.graph().view().timeSpan(), spanT, 1e-9) && nearD(f.graph().view().valueSpan(), spanV, 1e-9));
  R1_EXPECT(nearD((f.graph().view().tMin + f.graph().view().tMax) / 2.0, 6.0, 1e-9) && nearD((f.graph().view().vMin + f.graph().view().vMax) / 2.0, 4.0, 1e-9));
  // Nothing selected: all visible curves. Hidden curves do not count.
  f.graph().clearSelection();
  f.key(kKeyF);
  R1_EXPECT(f.graph().view().tMin < 0.0 && f.graph().view().tMax > 9.0);
  Curve hidden = makeCurve(3, "Far", {{100, 100}, {200, 200}});
  hidden.visible = false;
  std::vector<Curve> all = f.graph().curves();
  all.push_back(hidden);
  f.graph().setCurves(all);
  f.graph().frameAll();
  R1_EXPECT(f.graph().view().tMax < 20.0);
  // Axis-only fits.
  f.graph().setView({50.0, 60.0, 50.0, 60.0});
  f.graph().frameHorizontal();
  R1_EXPECT(f.graph().view().tMax < 20.0 && f.graph().view().vMin == 50.0);
  f.graph().setView({50.0, 60.0, 50.0, 60.0});
  f.graph().frameVertical();
  R1_EXPECT(f.graph().view().tMin == 50.0 && f.graph().view().vMax < 20.0);
  // No curves: the view stays.
  f.graph().setCurves({});
  const curve::View keep = f.graph().view();
  f.graph().frameAll();
  f.graph().frameSelected();
  R1_EXPECT(f.graph().view() == keep);
}

// ---- selection --------------------------------------------------------------------------------

void testSelectionRules() {
  Fixture f;
  f.clickKey(1, 2);
  R1_EXPECT(f.graph().selection().size() == 1 && f.graph().selection().containsKey(1, 2) && f.log.selections == 1);
  f.clickKey(1, 3, events::Mod::kShift);
  R1_EXPECT(f.graph().selection().size() == 2);
  f.clickKey(1, 2, events::Mod::kAlt);  // alt removes
  R1_EXPECT(f.graph().selection().size() == 1 && !f.graph().selection().containsKey(1, 2));
  f.clickKey(1, 3, events::Mod::kCtrl);  // ctrl toggles off
  R1_EXPECT(f.graph().selection().empty());
  f.clickKey(2, 1, events::Mod::kCtrl);  // and on, on another curve
  R1_EXPECT(f.graph().selection().containsKey(2, 1));
  // Clicking one of several selected keys without a drag makes it the only selection.
  f.clickKey(1, 1, events::Mod::kShift);
  f.clickKey(1, 4, events::Mod::kShift);
  R1_EXPECT(f.graph().selection().size() == 3);
  f.clickKey(1, 4);
  R1_EXPECT(f.graph().selection().size() == 1 && f.graph().selection().containsKey(1, 4));
  // A click on empty space clears (a zero-length marquee), unless a modifier is down.
  f.click(f.rect().x + 700, f.rect().y + 500, events::Mod::kShift);
  R1_EXPECT(f.graph().selection().size() == 1);
  f.click(f.rect().x + 700, f.rect().y + 500);
  R1_EXPECT(f.graph().selection().empty());
  R1_EXPECT(f.log.begins.empty() && f.balanced());  // selection never opens an interaction

  // Keyboard: select all (ctrl+A), invert on curves that have a selection (ctrl+I), clear (ctrl+D, Escape).
  f.focus();
  f.key(kKeyA, events::Mod::kCtrl);
  R1_EXPECT(f.graph().selection().size() == 8);
  f.key(kKeyD, events::Mod::kCtrl);
  R1_EXPECT(f.graph().selection().empty());
  f.clickKey(1, 1);
  f.clickKey(1, 5, events::Mod::kShift);
  f.key(kKeyI, events::Mod::kCtrl);
  R1_EXPECT(f.graph().selection().size() == 3 && !f.graph().selection().containsKey(1, 1) && !f.graph().selection().containsKey(2, 1));  // Beta had no selection
  f.key(events::Key::Escape);
  R1_EXPECT(f.graph().selection().empty());
  // Scrub-relative selection.
  f.graph().setScrubTime(4.0);
  f.graph().selectAfterScrub();
  R1_EXPECT(f.graph().selection().containsKey(1, 3) && !f.graph().selection().containsKey(1, 2) && f.graph().selection().containsKey(2, 3));
  f.graph().selectBeforeScrub();
  R1_EXPECT(f.graph().selection().containsKey(1, 3) && f.graph().selection().containsKey(1, 2) && !f.graph().selection().containsKey(1, 4));
}

void testMarquee() {
  Fixture f;
  const layout::Rect r = f.rect();
  // Drag a rectangle around keys A2 (2,3) and A3 (4,1).
  f.down(f.X(1.6), f.Y(3.6));
  f.move(f.X(2.5), f.Y(2.0));
  R1_EXPECT(f.graph().selection().empty());  // nothing until release
  f.move(f.X(4.4), f.Y(0.6));
  f.up(f.X(4.4), f.Y(0.6));
  R1_EXPECT(f.graph().selection().size() == 2 && f.graph().selection().containsKey(1, 2) && f.graph().selection().containsKey(1, 3));
  // Shift adds (A5 at 8, 2), alt removes (A3), ctrl toggles.
  f.down(f.X(7.5), f.Y(2.6), events::Button::Left, events::Mod::kShift);
  f.move(f.X(8.5), f.Y(1.5), events::Mod::kShift);
  f.up(f.X(8.5), f.Y(1.5), events::Button::Left, events::Mod::kShift);
  R1_EXPECT(f.graph().selection().size() == 3 && f.graph().selection().containsKey(1, 5));
  f.down(f.X(3.5), f.Y(1.6), events::Button::Left, events::Mod::kAlt);
  f.move(f.X(4.5), f.Y(0.4), events::Mod::kAlt);
  f.up(f.X(4.5), f.Y(0.4), events::Button::Left, events::Mod::kAlt);
  R1_EXPECT(f.graph().selection().size() == 2 && !f.graph().selection().containsKey(1, 3));
  f.down(f.X(1.5), f.Y(3.5), events::Button::Left, events::Mod::kCtrl);
  f.move(f.X(2.5), f.Y(2.5), events::Mod::kCtrl);
  f.up(f.X(2.5), f.Y(2.5), events::Button::Left, events::Mod::kCtrl);
  R1_EXPECT(f.graph().selection().size() == 1 && !f.graph().selection().containsKey(1, 2));
  // A plain marquee replaces.
  f.down(f.X(0.5), f.Y(-0.8));
  f.move(f.X(1.5), f.Y(-0.2));
  f.up(f.X(1.5), f.Y(-0.2));
  R1_EXPECT(f.graph().selection().size() == 1 && f.graph().selection().containsKey(2, 1));
  f.paint();  // an active marquee paints
  R1_EXPECT(f.log.begins.empty() && f.balanced());
  // The ruler strip is not part of the plot: a press there scrubs instead.
  f.down(r.x + 300, r.y + 10);
  R1_EXPECT(nearD(f.graph().scrubTime(), f.graph().mapping().toTime(300), 1e-9) && nearD(f.log.scrub, f.graph().scrubTime()));
  f.move(r.x + 400, r.y + 10);
  R1_EXPECT(nearD(f.graph().scrubTime(), f.graph().mapping().toTime(400), 1e-9));
  f.up(r.x + 400, r.y + 10);
  R1_EXPECT(f.graph().selection().size() == 1);
}

// ---- moving, nudging ---------------------------------------------------------------------------

void testDragKeys() {
  Fixture f;
  const curve::Point p = f.graph().keyPosition(1, 3);
  const double sx = f.rect().x + p.x;
  const double sy = f.rect().y + p.y;
  f.down(sx, sy);
  f.move(sx + 3, sy);  // inside the dead zone: nothing yet
  R1_EXPECT(f.log.begins.empty() && f.curve(1).keys[2].time == 4.0);
  f.move(sx + 40, sy - 20);
  R1_EXPECT(f.log.begins.size() == 1 && f.log.begins[0] == "Move keys" && f.graph().interactionOpen());
  const curve::Mapping m = f.graph().mapping();
  R1_EXPECT(nearD(f.curve(1).keys[2].time, 4.0 + 40.0 * m.timePerPixel(), 1e-9) && nearD(f.curve(1).keys[2].value, 1.0 + 20.0 * m.valuePerPixel(), 1e-9));
  f.move(sx + 80, sy);  // total offset from the press, not accumulated
  R1_EXPECT(nearD(f.curve(1).keys[2].time, 4.0 + 80.0 * m.timePerPixel(), 1e-9) && nearD(f.curve(1).keys[2].value, 1.0, 1e-9));
  f.up(sx + 80, sy);
  R1_EXPECT(f.log.ends == 1 && f.log.committed == 1 && f.balanced() && !f.graph().interactionOpen() && f.log.changes >= 2);
  // A group moves together.
  f.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}, {6, 4}, {8, 2}})});
  f.clearLog();
  f.clickKey(1, 2);
  f.clickKey(1, 3, events::Mod::kShift);
  const curve::Point q = f.graph().keyPosition(1, 3);
  f.down(f.rect().x + q.x, f.rect().y + q.y);
  f.move(f.rect().x + q.x, f.rect().y + q.y + 50);
  f.up(f.rect().x + q.x, f.rect().y + q.y + 50);
  R1_EXPECT(nearD(f.curve(1).keys[1].value, 3.0 - 50.0 * m.valuePerPixel(), 1e-9) && nearD(f.curve(1).keys[2].value, 1.0 - 50.0 * m.valuePerPixel(), 1e-9));
  R1_EXPECT(f.log.begins.size() == 1 && f.balanced());
  // Moving onto another key removes the stacked one at the end of the drag (rule 49). Times are only
  // exactly equal on the frame grid, so the grid is on.
  f.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}})});
  f.graph().clearSelection();
  CurveSettings snap = f.graph().settings();
  snap.snapTime = true;
  f.graph().setSettings(snap);
  f.clearLog();
  const curve::Point a = f.graph().keyPosition(1, 2);
  const curve::Point b = f.graph().keyPosition(1, 3);
  f.down(f.rect().x + a.x, f.rect().y + a.y);
  f.move(f.rect().x + b.x, f.rect().y + a.y);
  f.up(f.rect().x + b.x, f.rect().y + a.y);
  R1_EXPECT(f.curve(1).keys.size() == 2 && curve::indexOfKey(f.curve(1), 3) == curve::npos && curve::valid(f.curve(1)));
  R1_EXPECT(f.graph().selection().containsKey(1, 2) && f.graph().selection().size() == 1);
  snap.snapTime = false;
  f.graph().setSettings(snap);
  // The key can be dragged back and forth across its neighbour while the button is down.
  f.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}})});
  const curve::Point c = f.graph().keyPosition(1, 1);
  f.down(f.rect().x + c.x, f.rect().y + c.y);
  f.move(f.rect().x + b.x + 40, f.rect().y + c.y);
  R1_EXPECT(curve::valid(f.curve(1)) && f.curve(1).keys.back().id == 1);
  f.move(f.rect().x + c.x + 5, f.rect().y + c.y);  // back: the stacked / crossed state is recomputed from the start
  R1_EXPECT(f.curve(1).keys.size() == 3 && f.curve(1).keys.front().id == 1);
  f.up(f.rect().x + c.x + 5, f.rect().y + c.y);
}

void testEscapeAndCaptureLoss() {
  Fixture f;
  const Curve original = f.curve(1);
  const curve::Point p = f.graph().keyPosition(1, 3);
  f.down(f.rect().x + p.x, f.rect().y + p.y);
  f.move(f.rect().x + p.x + 60, f.rect().y + p.y + 30);
  R1_EXPECT(!(f.curve(1) == original) && f.graph().interactionOpen());
  f.focus();
  f.t.ui.router().focus(f.graph().id(), events::FocusReason::Pointer);
  R1_EXPECT(f.t.ui.keyDown(events::Key::Escape));
  R1_EXPECT(f.curve(1) == original && f.log.cancelled == 1 && f.log.committed == 0 && f.balanced());
  R1_EXPECT(f.graph().selection().containsKey(1, 3));  // the selection of the press stays (it was made before the drag)
  f.up(f.rect().x + p.x + 60, f.rect().y + p.y + 30);  // the late release changes nothing
  R1_EXPECT(f.curve(1) == original && f.log.ends == 1);

  // Capture lost mid drag: the same restore.
  f.clearLog();
  f.down(f.rect().x + p.x, f.rect().y + p.y);
  f.move(f.rect().x + p.x + 60, f.rect().y + p.y);
  f.t.ui.router().cancelPointerInteraction();
  R1_EXPECT(f.curve(1) == original && f.log.cancelled == 1 && f.balanced());
  // A drag that ends where it began still commits one (empty) interaction; the host can drop it.
  f.clearLog();
  f.down(f.rect().x + p.x, f.rect().y + p.y);
  f.move(f.rect().x + p.x + 60, f.rect().y + p.y);
  f.move(f.rect().x + p.x, f.rect().y + p.y);
  f.up(f.rect().x + p.x, f.rect().y + p.y);
  R1_EXPECT(f.curve(1) == original && f.balanced());
}

void testAxisLockAndSnap() {
  Fixture f;
  const curve::Point p = f.graph().keyPosition(1, 3);
  const double sx = f.rect().x + p.x;
  const double sy = f.rect().y + p.y;
  const curve::Mapping m = f.graph().mapping();
  // Shift locks to the dominant axis after a tiny movement and keeps it until Shift is released.
  f.focus();
  f.down(sx, sy);
  f.move(sx + 6, sy + 1, events::Mod::kShift);
  f.move(sx + 40, sy + 30, events::Mod::kShift);  // vertical offset is ignored: horizontal won
  R1_EXPECT(nearD(f.curve(1).keys[2].value, 1.0, 1e-9) && f.curve(1).keys[2].time > 4.0);
  f.move(sx + 5, sy + 80, events::Mod::kShift);  // still locked to the horizontal axis
  R1_EXPECT(nearD(f.curve(1).keys[2].value, 1.0, 1e-9));
  f.move(sx + 5, sy + 80);  // Shift released: free again
  R1_EXPECT(f.curve(1).keys[2].value < 1.0);
  f.up(sx + 5, sy + 80);
  // A chosen axis lock applies without Shift and wins over the dominant-axis rule.
  f.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}})});
  f.graph().setAxisLock(AxisLock::Vertical);
  f.down(sx, sy);
  f.move(sx + 60, sy + 20);
  R1_EXPECT(nearD(f.curve(1).keys[2].time, 4.0, 1e-9) && f.curve(1).keys[2].value < 1.0);
  f.up(sx + 60, sy + 20);
  f.graph().setAxisLock(AxisLock::None);
  // Time snapping (acceptance 6): the key time always lands on a frame.
  f.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}})});
  CurveSettings s = f.graph().settings();
  s.snapTime = true;
  s.framesPerSecond = 30.0;
  f.graph().setSettings(s);
  f.down(sx, sy);
  for (int dx = 5; dx < 120; dx += 7) {
    f.move(sx + dx, sy);
    const double frames = f.curve(1).keys[2].time * 30.0;
    R1_EXPECT(nearD(frames, std::round(frames), 1e-6));
  }
  f.up(sx + 113, sy);
  // Value snapping to the minor grid step.
  s.snapTime = false;
  s.snapValue = true;
  s.valueSnapStep = 0.5;
  f.graph().setSettings(s);
  f.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}})});
  f.down(sx, sy);
  f.move(sx, sy - 23);
  const double v = f.curve(1).keys[2].value;
  R1_EXPECT(nearD(v / 0.5, std::round(v / 0.5), 1e-9));
  f.up(sx, sy - 23);
  (void)m;
}

void testNudge() {
  Fixture f;
  f.clickKey(1, 2);
  f.clickKey(1, 3, events::Mod::kShift);
  f.focus();
  const double frame = 1.0 / 30.0;
  f.key(events::Key::Right, events::Mod::kCtrl);  // acceptance 9: one display frame later, one undo step
  R1_EXPECT(nearD(f.curve(1).keys[1].time, 2.0 + frame) && nearD(f.curve(1).keys[2].time, 4.0 + frame));
  R1_EXPECT(f.log.begins.size() == 1 && f.log.begins[0] == "Translate keys right" && f.balanced());
  f.key(events::Key::Left, events::Mod::kCtrl);
  R1_EXPECT(nearD(f.curve(1).keys[1].time, 2.0) && f.log.begins.back() == "Translate keys left" && f.log.begins.size() == 2);
  // Plain arrows move by one pixel, Shift by ten.
  const curve::Mapping m = f.graph().mapping();
  f.key(events::Key::Up);
  R1_EXPECT(nearD(f.curve(1).keys[1].value, 3.0 + m.valuePerPixel(), 1e-9));
  f.key(events::Key::Left, events::Mod::kShift);
  R1_EXPECT(nearD(f.curve(1).keys[1].time, 2.0 - 10.0 * m.timePerPixel(), 1e-9));
  // Nothing selected: the arrows are not used.
  f.graph().clearSelection();
  R1_EXPECT(!f.t.ui.keyDown(events::Key::Left));
  // A nudge into a neighbour stacks and removes it.
  f.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {2.0 + 1.0 / 30.0, 1}})});
  f.clickKey(1, 2);
  f.key(events::Key::Right, events::Mod::kCtrl);
  R1_EXPECT(f.curve(1).keys.size() == 2 && curve::valid(f.curve(1)));
}

// ---- insertion --------------------------------------------------------------------------------

void testMiddleInsert() {
  Fixture f;
  const double tx = 3.0;
  const double v = curve::evaluate(f.curve(1), tx);
  const double sx = f.X(tx);
  const double sy = f.Y(v) + 1;  // 1 px off the curve: within the 5 px hover distance
  // Acceptance 3: a middle click with no key near inserts exactly one key, selected, one step.
  f.down(sx, sy, events::Button::Middle);
  f.up(sx, sy, events::Button::Middle);
  R1_EXPECT(f.curve(1).keys.size() == 6 && f.log.begins.size() == 1 && f.log.begins[0] == "Insert key" && f.balanced());
  R1_EXPECT(f.graph().selection().size() == 1 && f.graph().selection().items()[0].curve == 1);
  const curve::Key& inserted = f.curve(1).keys[2];
  R1_EXPECT(nearD(inserted.time, tx, 0.02) && inserted.interp == curve::Interp::Linear);
  // Acceptance 4: middle press and drag 50 px to the right: still one step, the key follows.
  f.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}, {6, 4}, {8, 2}})});
  f.clearLog();
  f.down(sx, sy, events::Button::Middle);
  f.move(sx + 20, sy);
  R1_EXPECT(f.log.begins.size() == 1 && f.log.begins[0] == "Insert and move key");
  f.move(sx + 50, sy);
  f.up(sx + 50, sy, events::Button::Middle);
  const curve::Mapping m = f.graph().mapping();
  R1_EXPECT(f.curve(1).keys.size() == 6 && f.balanced() && f.log.committed == 1);
  R1_EXPECT(nearD(f.curve(1).keys[2].time, f.graph().mapping().toTime(sx - f.rect().x + 50.0), 1e-6));
  // Escape during the insert-drag removes the key again: no trace.
  f.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}, {6, 4}, {8, 2}})});
  f.clearLog();
  f.down(sx, sy, events::Button::Middle);
  f.move(sx + 40, sy);
  f.focus();
  f.t.ui.keyDown(events::Key::Escape);
  R1_EXPECT(f.curve(1).keys.size() == 5 && f.log.cancelled == 1 && f.balanced());
  f.up(sx + 40, sy, events::Button::Middle);
  // Ctrl: user tangents measured on the curve; the interpolation is the curve's.
  f.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}}, curve::Interp::Cubic)});
  f.clearLog();
  const double cv = curve::evaluate(f.curve(1), 3.0);
  f.down(f.X(3.0), f.Y(cv), events::Button::Middle, events::Mod::kCtrl);
  f.up(f.X(3.0), f.Y(cv), events::Button::Middle, events::Mod::kCtrl);
  R1_EXPECT(f.curve(1).keys.size() == 4 && f.curve(1).keys[2].mode == curve::TangentMode::Independent && f.curve(1).keys[2].interp == curve::Interp::Cubic);
  // A middle click on empty space (no curve) or on a key does nothing to the data.
  f.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}})});
  f.clearLog();
  f.down(f.X(7.0), f.Y(-0.9), events::Button::Middle);
  f.up(f.X(7.0), f.Y(-0.9), events::Button::Middle);
  const curve::Point kp = f.graph().keyPosition(1, 2);
  f.down(f.rect().x + kp.x, f.rect().y + kp.y, events::Button::Middle);
  f.up(f.rect().x + kp.x, f.rect().y + kp.y, events::Button::Middle);
  R1_EXPECT(f.curve(1).keys.size() == 3 && f.log.begins.empty());
  // Snapping applies to the new key.
  CurveSettings s = f.graph().settings();
  s.snapTime = true;
  f.graph().setSettings(s);
  const double vv = curve::evaluate(f.curve(1), 3.0123);
  f.down(f.X(3.0123), f.Y(vv), events::Button::Middle);
  f.up(f.X(3.0123), f.Y(vv), events::Button::Middle);
  bool onFrame = false;
  for (const curve::Key& k : f.curve(1).keys) onFrame |= nearD(k.time * 30.0, std::round(k.time * 30.0), 1e-6) && k.time > 2.5 && k.time < 3.5;
  R1_EXPECT(onFrame);
  (void)m;
}

void testDoubleClickEnterAndLocked() {
  Fixture f;
  const double v = curve::evaluate(f.curve(1), 5.0);
  f.down(f.X(5.0), f.Y(v));
  f.up(f.X(5.0), f.Y(v));
  f.down(f.X(5.0), f.Y(v));
  f.up(f.X(5.0), f.Y(v));
  // The second click of a double click inserts (owner decision D17: yes).
  R1_EXPECT(f.curve(1).keys.size() == 6 && f.log.begins.size() == 1 && f.log.begins[0] == "Insert key" && f.balanced());
  // Enter adds a key to every editable curve at the scrub time, selected.
  f.graph().setScrubTime(3.0);
  f.focus();
  f.clearLog();
  f.key(events::Key::Enter);
  R1_EXPECT(f.curve(1).keys.size() == 7 && f.curve(2).keys.size() == 4 && f.graph().selection().size() == 2 && f.log.begins.size() == 1);
  f.key(events::Key::Enter);  // keys exist there now: nothing new
  R1_EXPECT(f.curve(1).keys.size() == 7 && f.log.begins.size() == 1);
  // A read-only curve is drawn but cannot be edited or selected (rule 23).
  std::vector<Curve> curves = f.graph().curves();
  curves[0].locked = true;
  f.graph().setCurves(curves);
  f.clearLog();
  f.clickKey(1, 2);
  R1_EXPECT(f.graph().selection().empty());
  const double lv = curve::evaluate(f.curve(1), 7.0);
  f.down(f.X(7.0), f.Y(lv), events::Button::Middle);
  f.up(f.X(7.0), f.Y(lv), events::Button::Middle);
  R1_EXPECT(f.curve(1).keys.size() == 7 && f.log.begins.empty());
  f.key(kKeyA, events::Mod::kCtrl);
  R1_EXPECT(!f.graph().selection().containsKey(1, 1) && f.graph().selection().containsKey(2, 1));
  // Solo: with one curve soloed the others are neither drawn nor editable.
  curves[1].solo = true;
  curves[0].locked = false;
  f.graph().setCurves(curves);
  f.key(kKeyA, events::Mod::kCtrl);
  R1_EXPECT(!f.graph().selection().containsKey(1, 1) && f.graph().selection().containsKey(2, 1));
  f.paint();
}

// ---- commands ---------------------------------------------------------------------------------

void testInterpolationHotkeys() {
  Fixture f;
  f.focus();
  f.key(kKeyA, events::Mod::kCtrl);
  f.key(kDigit(5));  // acceptance 8: constant interpolation everywhere
  for (const Curve& c : f.graph().curves()) {
    for (const curve::Key& k : c.keys) R1_EXPECT(k.interp == curve::Interp::Constant);
  }
  R1_EXPECT(f.log.begins.size() == 1 && f.balanced());
  f.key(kDigit(4));
  R1_EXPECT(f.curve(1).keys[0].interp == curve::Interp::Linear);
  f.key(kDigit(5));
  f.key(kDigit(5));  // already constant: no interaction
  R1_EXPECT(f.log.begins.size() == 3);
  f.key(kDigit(2));  // linked cubic
  R1_EXPECT(f.curve(1).keys[1].interp == curve::Interp::Cubic && f.curve(1).keys[1].mode == curve::TangentMode::Linked);
  f.key(kDigit(3));
  R1_EXPECT(f.curve(1).keys[1].mode == curve::TangentMode::Independent);
  f.key(kDigit(0));
  R1_EXPECT(f.curve(1).keys[1].mode == curve::TangentMode::AutoSmooth);
  f.key(kDigit(1));
  R1_EXPECT(f.curve(1).keys[1].mode == curve::TangentMode::AutoAverage);
  f.key(kDigit(6));  // flatten
  R1_EXPECT(f.curve(1).keys[1].mode == curve::TangentMode::Independent && f.curve(1).keys[1].inSlope == 0.0 && f.curve(1).keys[1].outSlope == 0.0);
  f.key(kKeyW, events::Mod::kCtrl);
  R1_EXPECT(f.curve(1).keys[1].weighted);
  f.key(kKeyW, events::Mod::kCtrl);
  R1_EXPECT(!f.curve(1).keys[1].weighted);
  // ctrl+H moves keys to whole frames while staying on their curve.
  std::vector<Curve> curves = f.graph().curves();
  curves[0].keys[1].time = 2.013;
  curves[0].keys[1].interp = curve::Interp::Linear;
  f.graph().setCurves(curves);
  f.graph().setSelection([] {
    curve::Selection s;
    s.add({1, 2, Part::Key});
    return s;
  }());
  const Curve before = f.curve(1);
  f.key(kKeyA == kKeyA ? static_cast<events::Key>('H') : kKeyA, events::Mod::kCtrl);
  R1_EXPECT(nearD(f.curve(1).keys[1].time * 30.0, std::round(f.curve(1).keys[1].time * 30.0), 1e-6));
  R1_EXPECT(nearD(f.curve(1).keys[1].value, curve::evaluate(before, f.curve(1).keys[1].time), 1e-9));
  // Keys outside the selection are untouched by all of the above.
  R1_EXPECT(f.curve(2) == *curve::findCurve(f.graph().curves(), 2));
}

void testCopyPaste() {
  Fixture f;
  f.clickKey(1, 2);
  f.clickKey(1, 3, events::Mod::kShift);
  f.focus();
  f.key(kKeyC, events::Mod::kCtrl);
  R1_EXPECT(f.graph().hasClipboard() && f.log.begins.empty());
  // Acceptance 11: move the scrub time, paste: the keys start at the scrub time and overwrite the span.
  f.graph().setScrubTime(5.0);
  f.clearLog();
  f.key(kKeyV, events::Mod::kCtrl);
  R1_EXPECT(f.log.begins.size() == 1 && f.log.begins[0] == "Paste keys" && f.balanced());
  // Clip keys at 2 and 4 land at 5 and 7; the destination key at 6 lies inside the span and is replaced.
  R1_EXPECT(curve::indexOfKey(f.curve(1), 4) == curve::npos);
  size_t at5 = 0;
  size_t at7 = 0;
  for (const curve::Key& k : f.curve(1).keys) {
    at5 += nearD(k.time, 5.0);
    at7 += nearD(k.time, 7.0);
  }
  R1_EXPECT(at5 == 1 && at7 == 1 && f.graph().selection().size() == 2 && curve::valid(f.curve(1)));
  // Merge keeps what lies in between.
  f.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}, {6, 4}, {8, 2}}), makeCurve(2, "Beta", {{1, -0.5}})});
  f.clickKey(1, 2);
  f.clickKey(1, 3, events::Mod::kShift);
  f.key(kKeyC, events::Mod::kCtrl);
  f.graph().setScrubTime(5.0);
  f.key(kKeyV, events::Mod::kCtrl | events::Mod::kShift);
  R1_EXPECT(f.curve(1).keys.size() == 7 && f.log.begins.back() == "Merge keys");
  // Cut removes and copies in one step.
  f.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}})});
  f.clearLog();
  f.clickKey(1, 2);
  f.key(kKeyX, events::Mod::kCtrl);
  R1_EXPECT(f.curve(1).keys.size() == 2 && f.log.begins.size() == 1 && f.log.begins[0] == "Cut keys" && f.graph().hasClipboard());
  // Delete (acceptance for rule 68) and the empty clipboard / empty selection cases.
  f.clickKey(1, 1);
  f.key(events::Key::Delete);
  R1_EXPECT(f.curve(1).keys.size() == 1 && f.graph().selection().empty());
  const size_t steps = f.log.begins.size();
  f.key(events::Key::Delete);  // nothing selected
  f.key(kKeyC, events::Mod::kCtrl);
  R1_EXPECT(f.log.begins.size() == steps);
  Fixture g;  // paste with an empty clipboard records no step
  g.focus();
  g.key(kKeyV, events::Mod::kCtrl);
  R1_EXPECT(g.log.begins.empty());
}

// ---- tangents ---------------------------------------------------------------------------------

void testTangentHandles() {
  Fixture f;
  std::vector<Curve> curves = {makeCurve(1, "Alpha", {{0, 0}, {4, 2}, {8, 0}}, curve::Interp::Cubic)};
  f.graph().setCurves(curves);
  f.clickKey(1, 2);
  curve::Point out;
  curve::Point in;
  R1_EXPECT(f.graph().handlePosition(1, 2, true, out) && f.graph().handlePosition(1, 2, false, in));
  const curve::Point kp = f.graph().keyPosition(1, 2);
  R1_EXPECT(std::hypot(out.x - kp.x, out.y - kp.y) > 30.0 && std::hypot(out.x - kp.x, out.y - kp.y) < 40.0 && out.x > kp.x && in.x < kp.x);
  R1_EXPECT(!f.graph().handlePosition(1, 1, false, in));  // the first key has no incoming tangent
  // Press the out handle and drag it up: the key's mode becomes Linked and both slopes follow.
  f.down(f.rect().x + out.x, f.rect().y + out.y);
  R1_EXPECT(f.graph().selection().contains({1, 2, Part::Out}));
  f.move(f.rect().x + out.x + 10, f.rect().y + out.y - 60);
  R1_EXPECT(f.log.begins.size() == 1 && f.log.begins[0] == "Move tangent");
  R1_EXPECT(f.curve(1).keys[1].mode == curve::TangentMode::Linked && f.curve(1).keys[1].outSlope > 0.1 && f.curve(1).keys[1].inSlope == f.curve(1).keys[1].outSlope);
  f.up(f.rect().x + out.x + 10, f.rect().y + out.y - 60);
  R1_EXPECT(f.balanced() && f.log.committed == 1);
  // Escape restores the tangent.
  const Curve afterFirst = f.curve(1);
  f.graph().handlePosition(1, 2, true, out);
  f.down(f.rect().x + out.x, f.rect().y + out.y);
  f.move(f.rect().x + out.x, f.rect().y + out.y + 80);
  R1_EXPECT(!(f.curve(1) == afterFirst));
  f.focus();
  f.t.ui.router().focus(f.graph().id(), events::FocusReason::Pointer);
  f.t.ui.keyDown(events::Key::Escape);
  R1_EXPECT(f.curve(1) == afterFirst);
  f.up(f.rect().x + out.x, f.rect().y + out.y + 80);
  // Weighted tangents: dragging also changes the handle length (as a weight).
  f.key(kKeyW, events::Mod::kCtrl);
  R1_EXPECT(f.curve(1).keys[1].weighted);
  curve::Point wout;
  f.graph().handlePosition(1, 2, true, wout);
  f.down(f.rect().x + wout.x, f.rect().y + wout.y);
  f.move(f.rect().x + wout.x + 60, f.rect().y + wout.y);
  f.up(f.rect().x + wout.x + 60, f.rect().y + wout.y);
  R1_EXPECT(f.curve(1).keys[1].outWeight > 0.4 && f.curve(1).keys[1].outWeight <= 1.0);
  // Tangent visibility: none hides the handles from drawing and hit testing.
  CurveSettings s = f.graph().settings();
  s.tangents = TangentVisibility::None;
  f.graph().setSettings(s);
  f.graph().handlePosition(1, 2, true, wout);
  f.clearLog();
  f.down(f.rect().x + wout.x, f.rect().y + wout.y);
  f.up(f.rect().x + wout.x, f.rect().y + wout.y);
  R1_EXPECT(!f.graph().selection().contains({1, 2, Part::Out}));
  f.paint();
  // Handles of selected keys only, then all keys.
  s.tangents = TangentVisibility::All;
  f.graph().setSettings(s);
  f.paint();
}

void testHoverAndTooltip() {
  Fixture f;
  const double v = curve::evaluate(f.curve(1), 3.0);
  f.t.ui.pointerMove(f.X(3.0), f.Y(v) + 3);
  R1_EXPECT(f.graph().hoveredCurve() == 1 && f.graph().tooltipText().substr(0, 6) == "Alpha:");
  f.t.ui.pointerMove(f.X(3.0), f.Y(v) + 9);  // more than 5 px away
  R1_EXPECT(f.graph().hoveredCurve() == 0 && f.graph().tooltipText().empty());
  CurveSettings s = f.graph().settings();
  s.curveTooltip = false;
  f.graph().setSettings(s);
  f.t.ui.pointerMove(f.X(3.0), f.Y(v));
  R1_EXPECT(f.graph().hoveredCurve() == 1 && f.graph().tooltipText().empty());
  // Hidden curves are not hovered; the cursor reflects what is under the pointer.
  const curve::Point kp = f.graph().keyPosition(1, 2);
  f.t.ui.pointerMove(f.rect().x + kp.x, f.rect().y + kp.y);
  R1_EXPECT(f.graph().cursor() == Cursor::Pointer);
  f.t.ui.pointerMove(f.X(3.0), f.Y(4.9));
  R1_EXPECT(f.graph().cursor() == Cursor::Default);
  f.t.ui.pointerMove(f.rect().x + 300, f.rect().y + 8);
  R1_EXPECT(f.graph().cursor() == Cursor::ResizeHorizontal);
  f.t.ui.pointerLeftWindow();
  R1_EXPECT(f.graph().hoveredCurve() == 0);
}

// ---- fields -----------------------------------------------------------------------------------

void typeInto(Fixture& f, PickerEntry& e, std::string_view text) {
  f.t.ui.router().focus(e.id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(kKeyA, events::Mod::kCtrl);
  for (char c : text) f.t.ui.textInput(static_cast<char32_t>(c));
  f.t.ui.keyDown(events::Key::Enter);
}

void testKeyFields() {
  Fixture f;
  CurveKeyFields& fields = f.editor->fields();
  R1_EXPECT(!fields.timeEntry().enabled() && fields.timeEntry().text().empty());
  f.clickKey(1, 2);
  R1_EXPECT(fields.timeEntry().enabled() && fields.timeEntry().text() == "2" && fields.valueEntry().text() == "3");
  R1_EXPECT(fields.interpolationSelect().selected() == 1 && fields.tangentSelect().selected() == 0 && fields.preSelect().selected() == 0);
  f.clickKey(1, 3, events::Mod::kShift);
  R1_EXPECT(fields.timeEntry().text() == "Mixed" && fields.valueEntry().text() == "Mixed");  // rule 65
  f.clearLog();
  typeInto(f, fields.valueEntry(), "2.5");  // all selected keys get the same value (rule 64)
  R1_EXPECT(f.curve(1).keys[1].value == 2.5 && f.curve(1).keys[2].value == 2.5 && f.log.begins.size() == 1 && f.balanced());
  R1_EXPECT(fields.valueEntry().text() == "2.5");
  typeInto(f, fields.valueEntry(), "oops");  // rejected: reverts, no step
  R1_EXPECT(fields.valueEntry().text() == "2.5" && f.log.begins.size() == 1);
  typeInto(f, fields.valueEntry(), "2.5");  // the value it already has: no step
  R1_EXPECT(f.log.begins.size() == 1);
  // Time moves the group keeping the distance between the keys.
  typeInto(f, fields.timeEntry(), "3");
  R1_EXPECT(nearD(f.curve(1).keys[1].time, 3.0) && nearD(f.curve(1).keys[2].time, 5.0) && f.log.begins.size() == 2);
  // Interpolation select and the weights toggle.
  fields.interpolationSelect().onSelect(2);
  R1_EXPECT(f.curve(1).keys[1].interp == curve::Interp::Cubic && f.curve(1).keys[2].interp == curve::Interp::Cubic);
  fields.tangentSelect().onSelect(3);
  R1_EXPECT(f.curve(1).keys[1].mode == curve::TangentMode::Independent);
  fields.weightedButton().onActivate();
  R1_EXPECT(f.curve(1).keys[1].weighted && fields.weightedButton().active());
  // Extrapolation of the curve of the selection.
  fields.postSelect().onSelect(static_cast<int>(curve::Extrapolation::Repeat));
  R1_EXPECT(f.curve(1).post == curve::Extrapolation::Repeat && f.curve(2).post == curve::Extrapolation::Constant);
  fields.postSelect().onSelect(static_cast<int>(curve::Extrapolation::Repeat));  // unchanged: no step
  const size_t steps = f.log.begins.size();
  fields.postSelect().onSelect(static_cast<int>(curve::Extrapolation::Repeat));
  R1_EXPECT(f.log.begins.size() == steps);
  f.graph().clearSelection();
  R1_EXPECT(!fields.timeEntry().enabled() && fields.interpolationSelect().selected() == -1);
  f.paint();
}

// ---- performance and hostile data -------------------------------------------------------------

void testCullingWithManyKeys() {
  r1test::TestUi t(900, 700);
  t.ui.rootStyle().alignItems = layout::Align::Start;
  CurveEditor& editor = t.ui.create<CurveEditor>(t.ui.root());
  editor.style().width = layout::Length::px(800);
  editor.style().height = layout::Length::px(600);
  t.layout();
  Curve big;
  big.id = 1;
  big.name = "big";
  const int n = 100000;
  for (int i = 0; i < n; ++i) big.keys.push_back(makeKey(i + 1, i * 0.01, std::sin(i * 0.05) * 3.0, i % 3 == 0 ? curve::Interp::Cubic : curve::Interp::Linear));
  editor.graph().setCurves({big});
  // Zoomed so that about 200 keys are visible.
  editor.graph().setView({500.0, 502.0, -4.0, 4.0});
  r1ui::render::Painter painter;
  const auto paint = [&] {
    painter.begin(900, 700);
    t.ui.paint(painter);
    t.ui.finishPaint();
    painter.end();
  };
  paint();
  R1_EXPECT(editor.graph().lastDrawnKeyCount() > 150 && editor.graph().lastDrawnKeyCount() < 260);
  const auto t0 = std::chrono::steady_clock::now();
  const int frames = 30;
  for (int i = 0; i < frames; ++i) {
    editor.graph().setView({500.0 + i * 0.03, 502.0 + i * 0.03, -4.0, 4.0});  // pan continuously
    paint();
  }
  const double perFrame = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
  std::fprintf(stdout, "curve editor: 100000 keys, 200 visible: %.2f ms per paint (%zu keys drawn, %zu polyline points)\n", perFrame, editor.graph().lastDrawnKeyCount(),
               editor.graph().lastDrawnPointCount());
  R1_EXPECT(perFrame < 100.0);
  // Zoomed all the way out all 100000 keys fall on 800 columns: the polyline is reduced to the envelope.
  editor.graph().setView({0.0, 1000.0, -4.0, 4.0});
  const auto t1 = std::chrono::steady_clock::now();
  paint();
  const double outMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
  std::fprintf(stdout, "curve editor: 100000 keys all visible: %.2f ms per paint (%zu keys drawn, %zu polyline points)\n", outMs, editor.graph().lastDrawnKeyCount(),
               editor.graph().lastDrawnPointCount());
  R1_EXPECT(editor.graph().lastDrawnKeyCount() < 100 && editor.graph().lastDrawnPointCount() < 8 * 800);
#ifdef NDEBUG
  R1_EXPECT(outMs < 500.0);
#else
  R1_EXPECT(outMs < 3000.0);  // the Debug runtime (checked iterators) is about 40 times slower
#endif
  // Selecting every key, moving them all, paints and stays valid.
  editor.graph().setView({0.0, 20.0, -4.0, 4.0});
  const auto t2 = std::chrono::steady_clock::now();
  editor.graph().selectAll();
  editor.graph().nudgeSelected(0.5, 0.0, "Nudge keys");
  const double allMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t2).count();
  std::fprintf(stdout, "curve editor: select all and nudge 100000 keys took %.1f ms\n", allMs);
  R1_EXPECT(curve::valid(editor.graph().curves()[0]) && editor.graph().selection().size() == 100000);
  R1_EXPECT(allMs < 5000.0);  // was quadratic: a lookup by key id per selected key
  paint();
}

void testHostileData() {
  Fixture f;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  Curve bad = makeCurve(1, "Bad", {{nan, 1}, {1, nan}, {2, inf}, {3, 3}, {3, 4}, {1e300, -1e300}, {-1e300, 0}});
  Curve dup = makeCurve(1, "Duplicate id", {{0, 0}, {1, 1}});
  Curve empty;
  empty.id = 0;
  f.graph().setCurves({bad, dup, empty});
  R1_EXPECT(f.graph().curves().size() == 3);
  for (const Curve& c : f.graph().curves()) R1_EXPECT(curve::valid(c));
  R1_EXPECT(f.graph().curves()[0].id != f.graph().curves()[1].id && f.graph().curves()[2].id != 0);
  f.graph().setView({nan, inf, 1, 1});
  R1_EXPECT(curve::validView(f.graph().view()));
  f.graph().setScrubTime(nan);
  f.graph().setScrubTime(1e300);
  CurveSettings s;
  s.framesPerSecond = nan;
  s.wheelMultiplier = inf;
  f.graph().setSettings(s);
  R1_EXPECT(f.graph().settings().framesPerSecond == 30.0 && f.graph().settings().wheelMultiplier == curve::kMaxWheelMultiplier);
  f.paint();
  // Input over absurd views and with a zero-size widget.
  f.graph().setView({-1e9, 1e9, -1e9, 1e9});
  f.paint();
  f.t.ui.pointerMove(f.rect().x + 100, f.rect().y + 100);
  f.t.ui.wheel(f.rect().x + 100, f.rect().y + 100, 0.0, 1.0);
  f.graph().setView({0.0, 1e-9, 0.0, 1e-9});
  f.paint();
  f.editor->style().height = layout::Length::px(0);
  f.editor->style().width = layout::Length::px(0);
  f.editor->requestLayout();
  f.t.layout();
  f.paint();
  f.t.ui.pointerMove(5, 5);
  f.t.ui.pointerDown(5, 5);
  f.t.ui.pointerUp(5, 5);
  f.focus();
  f.key(kKeyA, events::Mod::kCtrl);
  f.key(events::Key::Enter);
  R1_EXPECT(true);
}

void testDestroyInsideCallbacks() {
  {
    Fixture f;
    f.editor->onBeginInteraction = [&](const std::string&) { f.t.ui.destroy(f.editor->id()); };
    const curve::Point p = f.graph().keyPosition(1, 3);
    const double x = f.rect().x + p.x;
    const double y = f.rect().y + p.y;
    f.t.ui.pointerMove(x, y);
    f.t.ui.pointerDown(x, y);
    f.t.ui.pointerMove(x + 30, y);
    f.t.ui.pointerMove(x + 60, y);
    f.t.ui.pointerUp(x + 60, y);
    f.t.layout();
    f.paint();
  }
  {
    Fixture f;
    f.editor->onEndInteraction = [&](bool) { f.t.ui.destroy(f.editor->id()); };
    f.clickKey(1, 2);
    f.focus();
    f.key(events::Key::Delete);
    f.t.layout();
    f.paint();
  }
  {
    Fixture f;
    f.editor->onSelectionChanged = [&] { f.t.ui.destroy(f.editor->id()); };
    f.clickKey(1, 2);
    f.t.layout();
    f.paint();
  }
  {
    Fixture f;
    f.editor->onContextMenu = [&](const CurveContext&) { f.t.ui.destroy(f.editor->id()); };
    f.down(f.rect().x + 700, f.rect().y + 500, events::Button::Right);
    f.up(f.rect().x + 700, f.rect().y + 500, events::Button::Right);
    f.t.layout();
    f.paint();
  }
  for (const float scale : {1.25f, 2.0f}) {
    r1test::TestUi t(1000, 800, scale);
    t.ui.rootStyle().alignItems = layout::Align::Start;
    CurveEditor& e = t.ui.create<CurveEditor>(t.ui.root());
    e.style().width = layout::Length::px(600);
    e.style().height = layout::Length::px(400);
    e.graph().setCurves({makeCurve(1, "Alpha", {{0, 0}, {2, 3}, {4, 1}}, curve::Interp::Cubic)});
    e.graph().setView({0, 5, -1, 4});
    t.layout();
    r1ui::render::Painter painter;
    painter.begin(static_cast<uint32_t>(1000 * scale), static_cast<uint32_t>(800 * scale));
    t.ui.paint(painter);
    t.ui.finishPaint();
    painter.end();
    R1_EXPECT(painter.stats().texInstances > 0);  // labels were drawn
  }
}

}  // namespace

int main() {
  testGeometryAndWheel();
  testPanZoomAndContext();
  testFraming();
  testSelectionRules();
  testMarquee();
  testDragKeys();
  testEscapeAndCaptureLoss();
  testAxisLockAndSnap();
  testNudge();
  testMiddleInsert();
  testDoubleClickEnterAndLocked();
  testInterpolationHotkeys();
  testCopyPaste();
  testTangentHandles();
  testHoverAndTooltip();
  testKeyFields();
  testCullingWithManyKeys();
  testHostileData();
  testDestroyInsideCallbacks();
  return r1test::finish();
}
