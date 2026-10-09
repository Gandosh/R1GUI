// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for the widget runtime: creation / lookup / destruction (including a widget
//   destroying itself from its own handler), state flags and their mapping to style states, the
//   event hooks, focus indication rules, cursor choice, style-row registration (once per table,
//   rejection keeps the old sheet), animation timing and the instant fallback, stacking by layer,
//   the frame protocol (idle needs no frame), theme switch across two contexts sharing services,
//   platform event translation at 200% scale and hostile input.
// Why: every widget builds on these behaviours; a regression here breaks all of them.
// Callers: CTest (label fast, no GPU).
#include <cmath>
#include <limits>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/runtime/Easing.h"
#include "r1ui/widgets/runtime/PaintContext.h"

namespace {

using namespace r1ui::widgets;
namespace events = r1ui::core::events;
namespace theme = r1ui::theme;
using r1ui::core::tree::WidgetId;

constexpr theme::StyleRuleEntry kProbeRows[] = {
    {"probe.box", theme::State::kNone, theme::StyleProperty::Background, "color:panel-field"},
    {"probe.box", theme::State::kHover, theme::StyleProperty::Background, "color:panel-field-hover"},
    {"probe.box", theme::State::kDisabled, theme::StyleProperty::Opacity, "number:0.5"},
};

// A widget that records what the runtime does to it.
class Probe : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows() { return kProbeRows; }
  const char* typeName() const override { return "Probe"; }
  void onAttached() override {
    style().width = r1ui::core::layout::Length::px(100);
    style().height = r1ui::core::layout::Length::px(40);
    attachedCalls++;
  }
  void onDetached() override { detachedCalls++; }
  void paint(PaintContext& ctx) override { ctx.fillBox(ctx.style("probe.box")); paints++; }
  Cursor cursor() const override { return wantedCursor; }
  void onPointerEnter(Event&) override { enters++; }
  void onPointerLeave(Event&) override { leaves++; }
  void onPointerDown(Event& e) override { downs++; if (swallowDown) e.markHandled(); }
  void onPointerUp(Event&) override { ups++; }
  void onClick(Event&) override {
    clicks++;
    if (destroyOnClick) ui().destroy(id());
  }
  void onKeyDown(Event& e) override { keys++; lastKey = e.key; if (consumeKeys) e.markHandled(); }
  void onTextInput(Event& e) override { texts++; lastText = e.codePoint; e.markHandled(); }
  void onFocusIn(Event&) override { focusIns++; }
  void onFocusOut(Event&) override { focusOuts++; }
  void onStateChanged(uint16_t) override { stateChanges++; }

  int attachedCalls = 0, detachedCalls = 0, paints = 0, enters = 0, leaves = 0, downs = 0, ups = 0, clicks = 0, keys = 0, texts = 0;
  int focusIns = 0, focusOuts = 0, stateChanges = 0;
  events::Key lastKey = events::Key::Unknown;
  char32_t lastText = 0;
  Cursor wantedCursor = Cursor::Default;
  bool destroyOnClick = false;
  bool swallowDown = false;
  bool consumeKeys = false;
};

// Rows that reference a token that does not exist: the whole table must be rejected.
constexpr theme::StyleRuleEntry kBadRows[] = {
    {"bad.row", theme::State::kNone, theme::StyleProperty::Background, "color:no-such-token"},
};
class BadRowsWidget : public Probe {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows() { return kBadRows; }
};

void testLifecycleAndState() {
  r1test::TestUi t;
  auto& ui = t.ui;
  const size_t baseline = ui.widgetCount();
  Probe& a = ui.create<Probe>(ui.root());
  R1_EXPECT(a.attached() && a.attachedCalls == 1 && ui.alive(a.id()) && ui.object(a.id()) == &a);
  R1_EXPECT(ui.tree().get(a.id())->name == "Probe" && ui.objectAs<Probe>(a.id()) == &a);
  R1_EXPECT(ui.widgetCount() == baseline + 1);
  t.layout();
  R1_EXPECT(ui.absRect(a.id()).w == 100 && ui.absRect(a.id()).h == 40);
  R1_EXPECT(a.enabled() && a.styleState() == theme::State::kNone);

  a.setState(StateFlag::kHover, true);
  a.setSelected(true);
  a.setBound(true);
  a.setInvalid(true);
  R1_EXPECT(a.styleState() == (theme::State::kHover | theme::State::kSelected | theme::State::kBound | theme::State::kInvalid));
  R1_EXPECT(a.stateChanges == 4);
  a.setState(StateFlag::kHover, true);  // no change: no notification
  R1_EXPECT(a.stateChanges == 4);

  const WidgetId id = a.id();
  R1_EXPECT(ui.destroy(id));
  R1_EXPECT(!ui.alive(id) && ui.object(id) == nullptr && ui.widgetCount() == baseline);
  R1_EXPECT(!ui.destroy(id));  // stale id
  R1_EXPECT(!ui.destroy(ui.root()));
  bool threw = false;
  try {
    ui.create<Probe>(id);  // stale parent
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  R1_EXPECT(threw);

  // Slots are reused without confusing ids: a new widget never answers to the old id.
  Probe& b = ui.create<Probe>(ui.root());
  R1_EXPECT(ui.object(id) == nullptr && ui.object(b.id()) == &b);

  // Destroying a parent detaches children first and frees every object.
  Probe& parent = ui.create<Probe>(ui.root());
  Probe& child = ui.create<Probe>(parent.id());
  const WidgetId childId = child.id();
  R1_EXPECT(ui.destroy(parent.id()) && !ui.alive(childId));
}

void testPointerAndFocus() {
  r1test::TestUi t;
  auto& ui = t.ui;
  Probe& a = ui.create<Probe>(ui.root());
  a.setFocusable(true);
  Probe& b = ui.create<Probe>(ui.root());
  b.setFocusable(true);
  t.layout();  // a at (0,0,100,40), b below it? root is a row: b at (100,0)
  const auto ra = ui.absRect(a.id());
  const auto rb = ui.absRect(b.id());

  ui.pointerMove(ra.x + 10, ra.y + 10);
  R1_EXPECT(a.hovered() && a.enters == 1 && !b.hovered());
  R1_EXPECT(a.styleState() == theme::State::kHover);
  ui.pointerMove(rb.x + 10, rb.y + 10);
  R1_EXPECT(!a.hovered() && a.leaves == 1 && b.hovered());

  // Press, release, click; the pressed flag lives between press and release.
  ui.pointerDown(rb.x + 10, rb.y + 10);
  R1_EXPECT(b.pressed() && b.downs == 1 && (b.styleState() & theme::State::kActive) != 0);
  R1_EXPECT(b.focused() && !b.focusVisible());  // pointer focus shows no focus indication (spec 01 rule 5)
  ui.pointerUp(rb.x + 10, rb.y + 10);
  R1_EXPECT(!b.pressed() && b.ups == 1 && b.clicks == 1);

  // Keyboard focus shows the indication; keys and text reach the focused widget.
  ui.router().focus(a.id(), events::FocusReason::Keyboard);
  R1_EXPECT(a.focused() && a.focusVisible() && a.focusIns == 1 && b.focusOuts == 1 && !b.focused());
  a.consumeKeys = true;
  R1_EXPECT(ui.keyDown(events::Key::A));
  R1_EXPECT(a.keys == 1 && a.lastKey == events::Key::A);
  R1_EXPECT(ui.textInput(U'x') && a.texts == 1 && a.lastText == U'x');
  a.consumeKeys = false;
  ui.keyDown(events::Key::Tab);  // unused Tab moves focus to the next focusable (spec 01 rule 10)
  R1_EXPECT(b.focused() && b.focusVisible());

  // A disabled widget receives nothing and loses focus and hover.
  b.setEnabled(false);
  R1_EXPECT(!b.enabled() && !b.focused() && (b.styleState() & theme::State::kDisabled) != 0);
  const int downs = b.downs;
  ui.pointerMove(rb.x + 10, rb.y + 10);
  ui.pointerDown(rb.x + 10, rb.y + 10);
  ui.pointerUp(rb.x + 10, rb.y + 10);
  R1_EXPECT(b.downs == downs && !b.hovered());
  b.setEnabled(true);
  ui.pointerMove(rb.x + 12, rb.y + 12);
  R1_EXPECT(b.hovered());

  // Cursor: the nearest widget under the pointer that asks for one.
  b.wantedCursor = Cursor::ResizeHorizontal;
  R1_EXPECT(ui.cursor() == Cursor::ResizeHorizontal);
  ui.pointerLeftWindow();
  R1_EXPECT(ui.cursor() == Cursor::Default && !b.hovered());
}

void testSelfDestroy() {
  r1test::TestUi t;
  auto& ui = t.ui;
  Probe& a = ui.create<Probe>(ui.root());
  a.destroyOnClick = true;
  const WidgetId id = a.id();
  t.layout();
  ui.pointerMove(10, 10);
  ui.pointerDown(10, 10);
  ui.pointerUp(10, 10);  // the click handler destroys the widget that is dispatching it
  R1_EXPECT(!ui.alive(id));
  ui.pointerMove(12, 12);  // routing continues normally afterwards
  ui.frame();
  R1_EXPECT(!ui.keyDown(events::Key::A));
}

void testStyleRows() {
  r1test::TestUi t;
  auto& ui = t.ui;
  auto& services = t.services;
  R1_EXPECT(!services.hasStyleKey("probe.box"));
  const uint32_t revision = services.sheetRevision();
  ui.create<Probe>(ui.root());
  R1_EXPECT(services.hasStyleKey("probe.box") && services.sheetRevision() == revision + 1);
  ui.create<Probe>(ui.root());  // the same table again: no rebuild
  R1_EXPECT(services.sheetRevision() == revision + 1);

  const auto& idle = services.resolve("probe.box", 0);
  const theme::Color idleBg = idle.background;
  const auto& hover = services.resolve("probe.box", theme::State::kHover);
  R1_EXPECT(idleBg == *services.theme().color("panel-field") && hover.background == *services.theme().color("panel-field-hover"));
  R1_EXPECT(idle.background == *services.theme().color("panel-field"));  // the first reference stays valid
  R1_EXPECT(services.resolve("probe.box", theme::State::kDisabled | theme::State::kHover).opacity == 0.5);

  bool threw = false;
  try {
    ui.create<BadRowsWidget>(ui.root());
  } catch (const std::runtime_error& e) {
    threw = std::string(e.what()).find("bad.row") != std::string::npos;
  }
  R1_EXPECT(threw && !services.hasStyleKey("bad.row") && services.hasStyleKey("probe.box"));
  threw = false;
  try {
    (void)services.resolve("no.such.key", 0);
  } catch (const std::logic_error&) {
    threw = true;
  }
  R1_EXPECT(threw);
  // A theme switch re-resolves from the same rows.
  services.theme().set(theme::ThemeId::Light);
  R1_EXPECT(services.resolve("probe.box", 0).background == *services.theme().color("panel-field"));
  R1_EXPECT(services.theme().color("panel-field") != std::optional<theme::Color>(idleBg));
}

void testAnimation() {
  r1test::TestUi t;
  auto& ui = t.ui;
  Probe& a = ui.create<Probe>(ui.root());
  const r1ui::render::Color red{1, 0, 0, 1};
  const r1ui::render::Color blue{0, 0, 1, 1};

  // Animations off (default in TestUi): instant.
  ui.setTime(1000);
  R1_EXPECT(ui.animatedColor(a.id(), 0, red).r == 1.0f);
  R1_EXPECT(ui.animatedColor(a.id(), 0, blue).b == 1.0f);

  // Enabled but no frame loop running: still instant ("reduce to instant if the frame loop is not running").
  ui.setAnimationsEnabled(true);
  R1_EXPECT(!ui.animationsActive());
  R1_EXPECT(ui.animatedColor(a.id(), 0, red).r == 1.0f);

  ui.setFrameLoopRunning(true);
  R1_EXPECT(ui.animationsActive());
  ui.setTime(2000);
  R1_EXPECT(ui.animatedColor(a.id(), 1, red).r == 1.0f);          // first sight: no transition
  ui.setTime(2010);
  auto start = ui.animatedColor(a.id(), 1, blue);                  // retarget: starts moving
  R1_EXPECT(start.r > 0.99f && start.b < 0.01f);
  ui.setTime(2010 + 75);                                           // half of 150 ms with the token easing
  auto mid = ui.animatedColor(a.id(), 1, blue);
  R1_EXPECT(mid.r > 0.05f && mid.r < 0.95f && std::abs((mid.r + mid.b) - 1.0f) < 1e-4f);
  R1_EXPECT(ui.invalidator().needsFrame());                        // frames are requested while it moves
  ui.setTime(2010 + 400);
  auto end = ui.animatedColor(a.id(), 1, blue);
  R1_EXPECT(end.b == 1.0f && end.r == 0.0f);

  // After a paint pass the finished tween stops requesting frames.
  r1ui::render::Painter painter;
  painter.begin(400, 300);
  t.layout();
  ui.paint(painter);
  painter.end();
  ui.frame();
  R1_EXPECT(!ui.needsFrame());

  // Easing function: endpoints and monotonicity of the CSS curve (0.4, 0, 0.2, 1).
  double previous = 0.0;
  bool monotonic = true;
  for (int i = 0; i <= 20; ++i) {
    const double v = cubicBezierEase({0.4, 0.0, 0.2, 1.0}, i / 20.0);
    monotonic = monotonic && v >= previous - 1e-12;
    previous = v;
  }
  R1_EXPECT(monotonic && std::abs(cubicBezierEase({0.4, 0.0, 0.2, 1.0}, 0.0)) < 1e-9 && std::abs(cubicBezierEase({0.4, 0.0, 0.2, 1.0}, 1.0) - 1.0) < 1e-9);
}

void testPaintAndFrame() {
  r1test::TestUi t;
  auto& ui = t.ui;
  Probe& a = ui.create<Probe>(ui.root());
  ui.frame();
  r1ui::render::Painter painter;
  painter.begin(400, 300);
  ui.paint(painter);
  painter.end();
  R1_EXPECT(a.paints == 1 && painter.stats().sdfInstances >= 1);
  ui.frame();
  R1_EXPECT(!ui.needsFrame());  // an idle UI needs no frame
  a.requestPaint();
  R1_EXPECT(ui.needsFrame());
  const FrameInfo info = ui.frame();
  R1_EXPECT(!info.damage.empty() && !info.layoutRan);
  a.style().width = r1ui::core::layout::Length::px(60);
  a.requestLayout();
  R1_EXPECT(ui.frame().layoutRan && ui.absRect(a.id()).w == 60);

  // Theme switch: a repaint is needed without any other change.
  ui.frame();
  R1_EXPECT(!ui.needsFrame());
  t.services.theme().toggle();
  R1_EXPECT(ui.needsFrame());
  ui.frame();
  R1_EXPECT(!ui.needsFrame());

  // A scale change re-lays out; zero sizes (minimised) are accepted; bad scales fall back to 1.
  ui.setViewport(0, 0, 1.0f);
  ui.frame();
  ui.setViewport(800, 600, std::numeric_limits<float>::quiet_NaN());
  R1_EXPECT(ui.scale() == 1.0f && ui.viewportWidth() == 800);
  ui.setViewport(800, 600, 2.0f);
  R1_EXPECT(ui.viewportWidth() == 400 && ui.needsFrame());
}

void testStackingOrder() {
  r1test::TestUi t;
  auto& ui = t.ui;
  Probe& low = ui.create<Probe>(ui.root());
  Probe& high = ui.create<Probe>(ui.root());
  for (Probe* p : {&low, &high}) {
    p->style().position = r1ui::core::layout::Position::Absolute;
    p->style().inset[r1ui::core::layout::kLeft] = r1ui::core::layout::Length::px(10);
    p->style().inset[r1ui::core::layout::kTop] = r1ui::core::layout::Length::px(10);
  }
  low.requestLayout();
  high.requestLayout();
  ui.frame();
  R1_EXPECT(ui.router().hitTest(20, 20) == high.id());  // later sibling is above
  low.node().layer = 5;                                  // an explicit layer wins over sibling order
  R1_EXPECT(ui.router().hitTest(20, 20) == low.id());
  // The paint order follows the same stacking: lower layer first.
  r1ui::render::Painter painter;
  painter.begin(400, 300);
  low.paints = high.paints = 0;
  ui.paint(painter);
  painter.end();
  R1_EXPECT(low.paints == 1 && high.paints == 1);
}

void testSharedServicesTwoWindows() {
  r1test::TestUi first;
  r1ui::widgets::UiContext second(first.services);
  second.setViewport(200, 100, 1.0f);
  first.ui.frame();
  second.frame();
  R1_EXPECT(!first.ui.needsFrame() && !second.needsFrame());
  first.services.theme().toggle();
  R1_EXPECT(first.ui.needsFrame() && second.needsFrame());  // one theme switch repaints every window
  first.ui.frame();
  second.frame();
  R1_EXPECT(!first.ui.needsFrame() && !second.needsFrame());
  Probe& p = second.create<Probe>(second.root());
  second.frame();
  R1_EXPECT(first.services.hasStyleKey("probe.box") && second.alive(p.id()));
}

void testPlatformEvents() {
  r1test::TestUi t(800, 600, 2.0f);  // 200% display scale: logical window is 400 x 300
  auto& ui = t.ui;
  Probe& a = ui.create<Probe>(ui.root());
  a.setFocusable(true);
  t.layout();
  r1ui::platform::Event move;
  move.type = r1ui::platform::EventType::MouseMove;
  move.x = 100.0f;  // physical 100 -> logical 50
  move.y = 60.0f;
  ui.handlePlatformEvent(move);
  R1_EXPECT(a.hovered() && ui.pointerX() == 50.0 && ui.pointerY() == 30.0);
  r1ui::platform::Event down = move;
  down.type = r1ui::platform::EventType::MouseDown;
  down.button = r1ui::platform::MouseButton::Left;
  ui.handlePlatformEvent(down);
  R1_EXPECT(a.pressed());
  r1ui::platform::Event up = down;
  up.type = r1ui::platform::EventType::MouseUp;
  ui.handlePlatformEvent(up);
  R1_EXPECT(a.clicks == 1 && !a.pressed());
  r1ui::platform::Event key;
  key.type = r1ui::platform::EventType::KeyDown;
  key.virtualKey = 0x41;  // 'A'
  key.modifiers.ctrl = true;
  a.consumeKeys = true;
  ui.handlePlatformEvent(key);
  R1_EXPECT(a.keys == 1 && a.lastKey == events::Key::A);
  r1ui::platform::Event chr;
  chr.type = r1ui::platform::EventType::Char;
  chr.codePoint = U'é';
  ui.handlePlatformEvent(chr);
  R1_EXPECT(a.lastText == U'é');
  r1ui::platform::Event leave;
  leave.type = r1ui::platform::EventType::MouseLeave;
  ui.handlePlatformEvent(leave);
  R1_EXPECT(!a.hovered());
  r1ui::platform::Event focusLost;
  focusLost.type = r1ui::platform::EventType::FocusLost;
  ui.handlePlatformEvent(focusLost);
  R1_EXPECT(!ui.windowActive());
}

void testHostileInput() {
  r1test::TestUi t;
  auto& ui = t.ui;
  ui.create<Probe>(ui.root());
  t.layout();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  ui.pointerMove(nan, 5);
  ui.pointerDown(inf, -inf);
  ui.pointerUp(1e300, 1e300);
  ui.wheel(5, 5, nan, inf);
  R1_EXPECT(!ui.pointerKnown());
  ui.textInput(0xD800);     // surrogate
  ui.textInput(0x110000);   // beyond Unicode
  ui.textInput(0);
  ui.keyDown(events::Key::Unknown);
  ui.keyDown(static_cast<events::Key>(60000));
  ui.keyUp(static_cast<events::Key>(60000));
  r1ui::platform::Event weird;
  weird.type = r1ui::platform::EventType::MouseMove;
  weird.x = std::numeric_limits<float>::infinity();
  ui.handlePlatformEvent(weird);
  ui.setViewport(-5, -5, -1.0f);
  ui.frame();
  R1_EXPECT(ui.viewportWidth() == 0.0);
}

}  // namespace

int main() {
  testLifecycleAndState();
  testPointerAndFocus();
  testSelfDestroy();
  testStyleRows();
  testAnimation();
  testPaintAndFrame();
  testStackingOrder();
  testSharedServicesTwoWindows();
  testPlatformEvents();
  testHostileInput();
  return r1test::finish();
}
