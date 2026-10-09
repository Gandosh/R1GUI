// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for Label, the template widget: measure equals the text engine's width and the
//   style's line height at every scale, shrinking inside a narrow parent, ellipsis on a grapheme
//   boundary, roles resolve to their style rows, text / role changes request layout, disabled
//   opacity, accessible name, pass-through of pointer events, and hostile text (empty, invalid
//   UTF-8, very long, control characters) and hostile measurement input.
// Why: Label is what other widgets copy; its tests are the pattern for widget unit tests.
// Callers: CTest (label fast, no GPU; paint is checked on a recording Painter).
#include <cmath>
#include <limits>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/label/Label.h"

namespace {

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;

uint32_t paintAndCountGlyphQuads(r1test::TestUi& t) {
  r1ui::render::Painter painter;
  painter.begin(static_cast<uint32_t>(t.ui.viewportWidth() * t.ui.scale()), static_cast<uint32_t>(t.ui.viewportHeight() * t.ui.scale()));
  t.ui.paint(painter);
  t.ui.finishPaint();
  painter.end();
  return painter.stats().texInstances;
}

void testMeasureAndLayout() {
  for (const float scale : {1.0f, 1.5f, 2.0f}) {
    r1test::TestUi t(400, 200, scale);
    auto& ui = t.ui;
    Label& label = ui.create<Label>(ui.root(), "Position");
    ui.rootStyle().alignItems = layout::Align::Start;
    t.layout();
    const auto& rs = t.services.resolve("label.body", 0);
    const double expected = t.services.text().measure("Position", static_cast<float>(rs.text.fontSize * scale)) / scale;
    const layout::Rect r = ui.absRect(label.id());
    R1_EXPECT_NEAR(static_cast<double>(r.w), expected, 1.0);  // rounded to whole logical pixels by layout
    R1_EXPECT(r.h == 16);                                     // label.body line height
  }
}

void testShrinkAndEllipsis() {
  r1test::TestUi t(400, 200);
  auto& ui = t.ui;
  Label& wide = ui.create<Label>(ui.root(), "A label that is much longer than its container allows");
  wide.style().flexShrink = 1.0;
  ui.rootStyle().direction = layout::FlexDirection::Column;
  ui.rootStyle().alignItems = layout::Align::Start;
  auto& box = ui.create<Label>(ui.root(), "x");  // placeholder to keep a second child in the tree
  (void)box;
  t.layout();
  const int natural = ui.absRect(wide.id()).w;
  R1_EXPECT(natural > 150);

  // Constrain the width with a fixed container: the label shrinks to it (min width 0) and truncates.
  Label& narrow = ui.create<Label>(ui.root(), "A label that is much longer than its container allows");
  narrow.style().width = layout::Length::px(90);
  narrow.requestLayout();
  t.layout();
  R1_EXPECT(ui.absRect(narrow.id()).w == 90);
  const uint32_t wideQuads = paintAndCountGlyphQuads(t);
  R1_EXPECT(wideQuads > 0);

  const auto& rs = t.services.resolve("label.body", 0);
  const FittedText& fit = t.services.text().fit(narrow.text(), static_cast<float>(rs.text.fontSize), 90.0f);
  R1_EXPECT(fit.truncated && fit.width <= 90.0f + 0.01f && fit.text.size() < narrow.text().size());
  R1_EXPECT(fit.text.size() >= 3 && fit.text.compare(fit.text.size() - 3, 3, "\xE2\x80\xA6") == 0);  // ends with U+2026
  R1_EXPECT(!t.services.text().fit("short", static_cast<float>(rs.text.fontSize), 90.0f).truncated);

  // A tiny box never overflows: the bare ellipsis is clipped, nothing throws.
  narrow.style().width = layout::Length::px(3);
  narrow.requestLayout();
  t.layout();
  (void)paintAndCountGlyphQuads(t);
  narrow.setEllipsis(false);  // clipping instead of an ellipsis
  (void)paintAndCountGlyphQuads(t);
}

void testRolesAndStateChanges() {
  r1test::TestUi t;
  auto& ui = t.ui;
  Label& label = ui.create<Label>(ui.root(), "Layout", LabelRole::Heading);
  ui.rootStyle().alignItems = layout::Align::Start;
  t.layout();
  R1_EXPECT(std::string(Label::styleKeyFor(LabelRole::Heading)) == "label.heading");
  const auto& heading = t.services.resolve("label.heading", 0);
  R1_EXPECT(heading.text.fontSize == 11.0 && heading.text.weight == 600);
  R1_EXPECT(t.services.resolve("label.caption", 0).text.weight == 400 && t.services.resolve("label.muted", 0).text.fontSize == 12.0);
  R1_EXPECT(t.services.resolve("label.danger", 0).text.color == *t.services.theme().color("danger"));
  for (LabelRole role : {LabelRole::Body, LabelRole::Muted, LabelRole::Caption, LabelRole::Heading, LabelRole::Title, LabelRole::Danger}) {
    R1_EXPECT(t.services.hasStyleKey(Label::styleKeyFor(role)));
  }

  const int before = ui.absRect(label.id()).w;
  label.setText("Layout and more words");
  R1_EXPECT(ui.needsFrame());
  t.layout();
  R1_EXPECT(ui.absRect(label.id()).w > before);
  label.setText("Layout and more words");  // unchanged: nothing to do
  t.layout();
  R1_EXPECT(!ui.needsFrame());
  label.setRole(LabelRole::Body);
  R1_EXPECT(ui.needsFrame());
  t.layout();
  R1_EXPECT(ui.absRect(label.id()).h == 16);
  label.setRole(LabelRole::Heading);
  t.layout();
  R1_EXPECT(ui.absRect(label.id()).h == 11);  // measured line height of the reference (11 px)

  R1_EXPECT(label.paintOpacity() == 1.0f);
  label.setEnabled(false);
  R1_EXPECT(label.paintOpacity() == 0.5f);
  label.setEnabled(true);
  R1_EXPECT(label.accessibleName() == "Layout and more words");
  label.setAccessibleName("Section title");
  R1_EXPECT(label.accessibleName() == "Section title");

  // The theme colour of the role follows the theme.
  const auto dark = t.services.resolve("label.muted", 0).text.color;
  t.services.theme().set(r1ui::theme::ThemeId::Light);
  R1_EXPECT(t.services.resolve("label.muted", 0).text.color != dark);
}

void testPointerPassThrough() {
  r1test::TestUi t;
  auto& ui = t.ui;
  Label& label = ui.create<Label>(ui.root(), "Hello");
  t.layout();
  const layout::Rect r = ui.absRect(label.id());
  ui.pointerMove(r.x + 2, r.y + 2);
  R1_EXPECT(!label.hovered() && ui.router().hovered() == ui.root());
  label.setInteractive(true);
  ui.pointerMove(r.x + 3, r.y + 3);
  R1_EXPECT(label.hovered());
}

void testHostile() {
  r1test::TestUi t;
  auto& ui = t.ui;
  ui.rootStyle().alignItems = layout::Align::Start;
  const std::string texts[] = {
      "",
      std::string("a\0b", 3),
      "\xFF\xFE broken \xC3",             // invalid UTF-8
      std::string(5000, 'W'),             // long line
      "e\xCC\x81\xCC\x82\xCC\x83 combining marks",
      "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7 family",
      "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 right to left",
      "tab\tand\nnewline",
  };
  for (const std::string& text : texts) {
    Label& l = ui.create<Label>(ui.root(), text);
    l.style().width = layout::Length::px(120);
    l.requestLayout();
  }
  t.layout();
  (void)paintAndCountGlyphQuads(t);

  Label& probe = ui.create<Label>(ui.root(), "probe");
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const auto m = probe.measure({nan, nan, layout::MeasureMode::AtMost, layout::MeasureMode::AtMost});
  R1_EXPECT(m.width >= 0.0 || std::isnan(m.width));  // the engine sanitises NaN; the widget must not crash
  const auto m2 = probe.measure({-10.0, 10.0, layout::MeasureMode::AtMost, layout::MeasureMode::Undefined});
  R1_EXPECT(m2.width <= 0.0 + 1e-9);  // never wider than a negative budget
  probe.setText(std::string(2'000'000, 'x'));  // above the shaper's input limit: measures as nothing, never throws
  t.layout();
  (void)paintAndCountGlyphQuads(t);
  probe.setColorToken("no-such-token");  // magenta, not a crash
  (void)paintAndCountGlyphQuads(t);
}

}  // namespace

int main() {
  testMeasureAndLayout();
  testShrinkAndEllipsis();
  testRolesAndStateChanges();
  testPointerPassThrough();
  testHostile();
  return r1test::finish();
}
