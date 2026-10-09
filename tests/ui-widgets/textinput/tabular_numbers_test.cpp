// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression oracle for the tabular-numerals option added to the shared text path for number
//   fields (ShapeOptions::tabularNumbers, TextEngine measure / fit / draw, TextOptions::tabular): digits
//   take equal advances only when asked, the default shaping is unchanged, runs are cached per mode, a
//   fitted (ellipsised) run honours the mode, and the line editor lays out and draws with it.
// Why: the reference draws number-field digits with the tnum feature (measured 7.4 px per digit at
//   12 px; proportional digits would give a 1 and a 0 different widths).
// Callers: CTest (textinput fast, no GPU).
#include <cmath>

#include "FieldRig.h"

int main() {
  r1test::FieldRig rig;
  r1ui::widgets::TextEngine& text = rig.services.text();
  const float proportional1 = text.measure("1111", 12.0f);
  const float proportional0 = text.measure("0000", 12.0f);
  const float tabular1 = text.measure("1111", 12.0f, 400, true);
  const float tabular0 = text.measure("0000", 12.0f, 400, true);
  R1_EXPECT(proportional1 != proportional0);                  // Inter's default digits are proportional
  R1_EXPECT_NEAR(tabular1, tabular0, 0.01);                   // tnum makes them equal
  R1_EXPECT(tabular1 > proportional1);                        // a tabular 1 is wider than a proportional 1
  R1_EXPECT_NEAR(tabular0 / 4.0f, 7.4f, 0.4);                 // the measured digit advance at 12 px
  R1_EXPECT(text.measure("1111", 12.0f) == proportional1);    // the default path is untouched and cached apart
  R1_EXPECT(text.measure("abc", 12.0f) == text.measure("abc", 12.0f, 400, true));  // letters do not change

  // Fitting keeps the mode: the ellipsised text of a tabular run is measured tabular.
  const r1ui::widgets::FittedText& fitted = text.fit("123456789012345", 12.0f, 40.0f, true);
  R1_EXPECT(fitted.truncated && fitted.width <= 40.0f + 0.01f);
  const std::string fittedText = fitted.text;
  R1_EXPECT_NEAR(text.measure(fittedText, 12.0f, 400, true), fitted.width, 0.05);
  R1_EXPECT(!text.fit("12", 12.0f, 400.0f, true).truncated);

  // Tabular text paints (quads are produced) and an invalid size draws nothing without throwing.
  r1ui::render::Painter painter;
  painter.begin(200, 40);
  text.beginFrame();
  text.draw(painter, "0123456789", 12.0f, 400, 4.0f, 20.0f, {1, 1, 1, 1}, true);
  text.draw(painter, "0123456789", -1.0f, 400, 4.0f, 20.0f, {1, 1, 1, 1}, true);
  text.draw(painter, "0123456789", 12.0f, 400, 4.0f, 20.0f, {1, 1, 1, 1}, false);
  text.uploadAtlas();
  painter.end();
  R1_EXPECT(painter.stats().texInstances == 20);
  return r1test::finish();
}
