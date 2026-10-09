// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for Button and its Pressable base (shared by IconButton, Checkbox and Switch):
//   measured sizes per tone and size, click on release inside only, re-arming when the pointer
//   returns, keyboard activation on key-up (Space and Enter, not with modifiers, not on repeat,
//   cancelled by focus loss), disabled behaviour (also while pressed), the focus indication only for
//   keyboard focus, destroying the button from its own callback, rapid input, and hostile input
//   (invalid icon names, NaN sizes, invalid UTF-8, huge and empty labels, zero width).
// Why: every press-to-activate widget inherits these rules; they are pinned once here.
// Callers: CTest (button fast, no GPU; paint is checked on a recording Painter).
#include <cmath>
#include <limits>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/label/Label.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::FocusReason;
using r1ui::core::events::Key;
namespace layout = r1ui::core::layout;
namespace Mod = r1ui::core::events::Mod;

// Paints the whole UI once on a recording painter; true when nothing threw.
bool paintOnce(r1test::TestUi& t) {
  r1ui::render::Painter painter;
  painter.begin(static_cast<uint32_t>(t.ui.viewportWidth() * t.ui.scale()), static_cast<uint32_t>(t.ui.viewportHeight() * t.ui.scale()));
  try {
    t.ui.paint(painter);
    t.ui.finishPaint();
  } catch (const std::exception&) {
    painter.end();
    return false;
  }
  painter.end();
  return true;
}

struct Centre {
  double x, y;
};
Centre centreOf(r1test::TestUi& t, Button& b) {
  const layout::Rect r = t.ui.absRect(b.id());
  return {r.x + r.w * 0.5, r.y + r.h * 0.5};
}

void testSizesAndTones() {
  r1test::TestUi t;
  auto& ui = t.ui;
  ui.rootStyle().alignItems = layout::Align::Start;
  Button& sm = ui.create<Button>(ui.root(), "Share", ButtonTone::Accent, ButtonSize::Sm);
  Button& md = ui.create<Button>(ui.root(), "Share this file", ButtonTone::Accent, ButtonSize::Md);
  Button& icon = ui.create<Button>(ui.root(), "", ButtonTone::Ghost, ButtonSize::Icon);
  Button& iconSm = ui.create<Button>(ui.root(), "", ButtonTone::Ghost, ButtonSize::IconSm);
  icon.setIcon("plus");
  iconSm.setIcon("plus");
  t.layout();
  R1_EXPECT(ui.absRect(sm.id()).h == 28);
  R1_EXPECT(ui.absRect(md.id()).h == 32);
  R1_EXPECT(ui.absRect(icon.id()).w == 32 && ui.absRect(icon.id()).h == 32);
  R1_EXPECT(ui.absRect(iconSm.id()).w == 28 && ui.absRect(iconSm.id()).h == 28);
  // Natural width = 2 x 12 px padding + text (icon-less).
  const auto& rs = t.services.resolve("btn.accent", 0);
  const double text = t.services.text().measure("Share", static_cast<float>(12.0), rs.text.weight);
  R1_EXPECT_NEAR(static_cast<double>(ui.absRect(sm.id()).w), 24.0 + text, 1.0);
  sm.setIcon("share2");
  t.layout();
  R1_EXPECT_NEAR(static_cast<double>(ui.absRect(sm.id()).w), 24.0 + text + 14.0 + 6.0, 1.0);
  for (const char* key : {"btn.ghost", "btn.accent", "btn.panel", "btn.panelAccent", "btn.neutral", "btn.size.sm", "btn.size.md", "btn.size.icon", "btn.size.iconSm"}) R1_EXPECT(t.services.hasStyleKey(key));
  R1_EXPECT(std::string(Button::toneKey(ButtonTone::Panel)) == "btn.panel");
  R1_EXPECT(sm.accessibleName() == "Share");
  icon.setAccessibleName("Add");
  R1_EXPECT(icon.accessibleName() == "Add");
  R1_EXPECT(paintOnce(t));
}

void testPointerClick() {
  r1test::TestUi t;
  auto& ui = t.ui;
  ui.rootStyle().alignItems = layout::Align::Start;
  Button& b = ui.create<Button>(ui.root(), "Go", ButtonTone::Accent);
  Button& other = ui.create<Button>(ui.root(), "Other", ButtonTone::Ghost);
  int clicks = 0;
  b.setOnClick([&] { ++clicks; });
  t.layout();
  const Centre c = centreOf(t, b);
  const Centre o = centreOf(t, other);

  ui.pointerMove(c.x, c.y);
  R1_EXPECT(b.hovered() && !b.pressed());
  ui.pointerDown(c.x, c.y);
  R1_EXPECT(b.pressed());
  ui.pointerUp(c.x, c.y);
  R1_EXPECT(clicks == 1 && !b.pressed());
  R1_EXPECT(!b.focusVisible());  // a mouse click leaves no focus indication

  // Release elsewhere: no click.
  ui.pointerDown(c.x, c.y);
  ui.pointerMove(o.x, o.y);
  R1_EXPECT(!b.pressed());
  ui.pointerUp(o.x, o.y);
  R1_EXPECT(clicks == 1);

  // Leave and come back while held: pressed again and the release clicks.
  ui.pointerMove(c.x, c.y);
  ui.pointerDown(c.x, c.y);
  ui.pointerMove(o.x, o.y);
  R1_EXPECT(!b.pressed());
  ui.pointerMove(c.x, c.y);
  R1_EXPECT(b.pressed());
  ui.pointerUp(c.x, c.y);
  R1_EXPECT(clicks == 2);

  // A right click does not activate.
  ui.pointerDown(c.x, c.y, r1ui::core::events::Button::Right);
  ui.pointerUp(c.x, c.y, r1ui::core::events::Button::Right);
  R1_EXPECT(clicks == 2);
  R1_EXPECT(ui.cursor() == Cursor::Pointer);

  // Rapid input: every press/release pair is one click.
  for (int i = 0; i < 500; ++i) {
    ui.pointerDown(c.x, c.y);
    ui.pointerUp(c.x, c.y);
  }
  R1_EXPECT(clicks == 502);
  R1_EXPECT(b.click() && clicks == 503);
}

void testDisabled() {
  r1test::TestUi t;
  auto& ui = t.ui;
  ui.rootStyle().alignItems = layout::Align::Start;
  Button& b = ui.create<Button>(ui.root(), "Go", ButtonTone::Accent);
  int clicks = 0;
  b.setOnClick([&] { ++clicks; });
  t.layout();
  const Centre c = centreOf(t, b);
  b.setEnabled(false);
  ui.pointerMove(c.x, c.y);
  ui.pointerDown(c.x, c.y);
  ui.pointerUp(c.x, c.y);
  R1_EXPECT(clicks == 0 && !b.hovered() && !b.pressed());
  R1_EXPECT(!b.click());
  R1_EXPECT(ui.cursor() == Cursor::Default);
  R1_EXPECT(b.paintOpacity() == 0.5f);

  // Disabled while pressed: the release must not click and nothing stays pressed.
  b.setEnabled(true);
  ui.pointerMove(c.x, c.y);
  ui.pointerDown(c.x, c.y);
  R1_EXPECT(b.pressed());
  b.setEnabled(false);
  R1_EXPECT(!b.pressed());
  ui.pointerUp(c.x, c.y);
  R1_EXPECT(clicks == 0);

  // Keyboard press cancelled by disabling.
  b.setEnabled(true);
  ui.router().focus(b.id(), FocusReason::Keyboard);
  ui.keyDown(Key::Space);
  R1_EXPECT(b.pressed());
  b.setEnabled(false);
  ui.keyUp(Key::Space);
  R1_EXPECT(clicks == 0 && !b.pressed());
}

void testKeyboard() {
  r1test::TestUi t;
  auto& ui = t.ui;
  ui.rootStyle().alignItems = layout::Align::Start;
  Button& b = ui.create<Button>(ui.root(), "Go", ButtonTone::Accent);
  Button& other = ui.create<Button>(ui.root(), "Other");
  int clicks = 0;
  b.setOnClick([&] { ++clicks; });
  t.layout();

  // Tab reaches the button with the focus indication.
  R1_EXPECT(ui.keyDown(Key::Tab) || b.focused());
  R1_EXPECT(b.focused() && b.focusVisible());
  R1_EXPECT((b.styleState() & r1ui::theme::State::kFocus) != 0);

  ui.keyDown(Key::Space);
  R1_EXPECT(b.pressed() && clicks == 0);  // pressed on key-down, fires on key-up
  ui.keyDown(Key::Space, 0, true);        // auto-repeat
  R1_EXPECT(clicks == 0);
  ui.keyUp(Key::Space);
  R1_EXPECT(clicks == 1 && !b.pressed());
  ui.keyDown(Key::Enter);
  ui.keyUp(Key::Enter);
  R1_EXPECT(clicks == 2);

  // Modifiers disable acceptance; key-up without a matching key-down does nothing.
  ui.keyDown(Key::Space, Mod::kCtrl);
  ui.keyUp(Key::Space, Mod::kCtrl);
  ui.keyDown(Key::Enter, Mod::kShift);
  ui.keyUp(Key::Enter, Mod::kShift);
  ui.keyUp(Key::Space);
  R1_EXPECT(clicks == 2);
  ui.keyDown(Key::Space);
  ui.keyUp(Key::Enter);  // a different key
  R1_EXPECT(clicks == 2);
  ui.keyUp(Key::Space);
  R1_EXPECT(clicks == 3);

  // Focus loss between down and up cancels.
  ui.keyDown(Key::Space);
  ui.router().focus(other.id(), FocusReason::Keyboard);
  R1_EXPECT(!b.pressed());
  ui.keyUp(Key::Space);
  R1_EXPECT(clicks == 3);

  // Mouse focus shows no indication.
  const layout::Rect r = ui.absRect(b.id());
  ui.pointerDown(r.x + 3, r.y + 3);
  ui.pointerUp(r.x + 3, r.y + 3);
  R1_EXPECT(b.focused() && !b.focusVisible());
  R1_EXPECT((b.styleState() & r1ui::theme::State::kFocus) == 0);
}

void testDestroyInsideHandler() {
  r1test::TestUi t;
  auto& ui = t.ui;
  ui.rootStyle().alignItems = layout::Align::Start;
  Button& b = ui.create<Button>(ui.root(), "Bye");
  const auto id = b.id();
  int calls = 0;
  b.setOnClick([&] {
    ++calls;
    ui.destroy(id);
  });
  t.layout();
  const Centre c = centreOf(t, b);
  ui.pointerDown(c.x, c.y);
  ui.pointerUp(c.x, c.y);
  R1_EXPECT(calls == 1 && !ui.alive(id));

  Button& k = ui.create<Button>(ui.root(), "Key");
  const auto kid = k.id();
  k.setOnClick([&] { ui.destroy(kid); });
  t.layout();
  ui.router().focus(kid, FocusReason::Keyboard);
  ui.keyDown(Key::Enter);
  ui.keyUp(Key::Enter);
  R1_EXPECT(!ui.alive(kid));
  R1_EXPECT(paintOnce(t));
}

void testHostileInput() {
  r1test::TestUi t;
  auto& ui = t.ui;
  ui.rootStyle().alignItems = layout::Align::Start;
  Button& b = ui.create<Button>(ui.root(), "x", ButtonTone::Panel);
  R1_EXPECT(b.setIcon("plus"));
  for (const char* bad : {"../etc/passwd", "a/b", "a.svg", "has space", "\xFF\xFE", "ünï"}) R1_EXPECT(!b.setIcon(bad) && b.icon() == "plus");
  R1_EXPECT(!b.setIcon(std::string(65, 'a')));
  R1_EXPECT(!b.setTrailingIcon("x/y"));
  R1_EXPECT(b.setIcon(""));  // removing is allowed
  R1_EXPECT(!b.setIconSize(std::numeric_limits<double>::quiet_NaN()));
  R1_EXPECT(!b.setIconSize(1e9) && !b.setIconSize(-3.0) && !b.setIconSize(0.0));
  R1_EXPECT(!b.setRadius(std::numeric_limits<double>::infinity()) && !b.setRadius(-1.0));
  R1_EXPECT(!b.setPaddingX(std::numeric_limits<double>::quiet_NaN()) && !b.setPaddingX(-4.0));
  b.setWeight(-5);
  b.setWeight(100000);

  // Invalid UTF-8, control characters, empty and huge text still measure and paint.
  for (const std::string text : {std::string("\xC3\x28\xA0\xA1\xF0\x28\x8C\xBC"), std::string("a\tb\nc\x01\x7F"), std::string(), std::string(200000, 'W')}) {
    b.setText(text);
    t.layout();
    const layout::Rect r = ui.absRect(b.id());
    R1_EXPECT(r.w >= 0 && r.h == 28);
    R1_EXPECT(paintOnce(t));
    // A max width bounds even a 200000 character label; the text is shortened with an ellipsis.
    b.style().maxWidth = layout::Length::px(120);
    b.requestLayout();
    t.layout();
    R1_EXPECT(ui.absRect(b.id()).w <= 120);
    R1_EXPECT(paintOnce(t));
    b.style().maxWidth = layout::Length::autoValue();
  }

  // Zero width and a tiny box never overflow or throw.
  b.setText("Some label");
  b.setIcon("share2");
  b.setTrailingIcon("chevron-down");
  b.style().width = layout::Length::px(0);
  b.requestLayout();
  t.layout();
  R1_EXPECT(paintOnce(t));
  b.style().width = layout::Length::px(18);
  b.requestLayout();
  t.layout();
  R1_EXPECT(paintOnce(t));

  // Scaled displays measure what they draw.
  for (const float scale : {1.5f, 2.0f}) {
    r1test::TestUi s(400, 300, scale);
    s.ui.rootStyle().alignItems = layout::Align::Start;
    Button& sb = s.ui.create<Button>(s.ui.root(), "Create collection", ButtonTone::Neutral);
    s.layout();
    R1_EXPECT(s.ui.absRect(sb.id()).w > 80 && paintOnce(s));
  }

  // A tooltip is stored and the base text API works.
  b.setTooltip("Hint");
  R1_EXPECT(b.tooltipText() == "Hint");
}

// Regression for the foundation fix in Label::measure: layout rounds box widths, so a label sized
// to its own natural width must never be truncated by paint (the gallery captions showed "ghost ...").
void testLabelNaturalWidthNeverTruncates() {
  for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
    r1test::TestUi t(600, 400, scale);
    auto& ui = t.ui;
    ui.rootStyle().direction = layout::FlexDirection::Column;
    ui.rootStyle().alignItems = layout::Align::Start;
    const char* texts[] = {"ghost icon + text", "accent iconSm", "panelAccent disabled", "Clip content", "md active", "WWWWWWWW", "iiiiiiii"};
    for (const char* text : texts) ui.create<Label>(ui.root(), text, LabelRole::Caption);
    t.layout();
    const auto& rs = t.services.resolve("label.caption", 0);
    size_t i = 0;
    ui.tree().forEachChild(ui.root(), [&](r1ui::core::tree::WidgetId id) {
      if (ui.object(id)->typeName() != std::string("Label")) return;
      const layout::Rect r = ui.absRect(id);
      const FittedText& fit = t.services.text().fit(texts[i], static_cast<float>(rs.text.fontSize * scale), static_cast<float>(r.w * scale));
      R1_EXPECT(!fit.truncated);
      ++i;
    });
    R1_EXPECT(i == 7);
  }
}

}  // namespace

int main() {
  testLabelNaturalWidthNeverTruncates();
  testSizesAndTones();
  testPointerClick();
  testDisabled();
  testKeyboard();
  testDestroyInsideHandler();
  testHostileInput();
  return r1test::finish();
}
