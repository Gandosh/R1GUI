// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the pure rules of the text weight model (emboldenStrength and the WeightModel validation of
//   TextEngine): strength is linear in the text luminance between the anchors of a weight class,
//   interpolates between the classes for weights in between, never goes below zero, and hostile inputs
//   (NaN, infinity, out-of-range luminance, absurd weights) give a defined result.
// Why: the constants are calibrated against images by text_calibration_visual_test (GPU); this test
//   pins the function shape without a device so a refactor cannot change it silently.
// Callers: CTest (label fast).
#include <cmath>
#include <limits>

#include "TestSupport.h"
#include "r1ui/widgets/text/TextEngine.h"

namespace {

using namespace r1ui::widgets;

constexpr WeightModel kModel{{-0.5f, 1.0f}, {0.0f, 2.0f}, {1.0f, 3.0f}};

void testLuminanceIsLinearBetweenTheAnchors() {
  R1_EXPECT_NEAR(emboldenStrength(kModel, 1.0f, 400), 1.0f, 1e-6);
  R1_EXPECT_NEAR(emboldenStrength(kModel, 0.5f, 400), 0.25f, 1e-6);
  R1_EXPECT_NEAR(emboldenStrength(kModel, 1.0f, 500), 2.0f, 1e-6);
  R1_EXPECT_NEAR(emboldenStrength(kModel, 0.0f, 600), 1.0f, 1e-6);
  R1_EXPECT_NEAR(emboldenStrength(kModel, 0.5f, 700), 2.0f, 1e-6);  // heavier than 600 uses the 600 anchors
}

void testWeightsInBetweenInterpolate() {
  R1_EXPECT_NEAR(emboldenStrength(kModel, 1.0f, 450), 1.5f, 1e-6);   // halfway 400 -> 500
  R1_EXPECT_NEAR(emboldenStrength(kModel, 1.0f, 550), 2.5f, 1e-6);   // halfway 500 -> 600
  R1_EXPECT_NEAR(emboldenStrength(kModel, 1.0f, 300), 1.0f, 1e-6);   // lighter than 400 uses the 400 anchors
}

void testNeverNegative() {
  R1_EXPECT_NEAR(emboldenStrength(kModel, 0.0f, 400), 0.0f, 1e-6);  // -0.5 clamps to 0
  R1_EXPECT(emboldenStrength(kModel, 0.1f, 400) >= 0.0f);
}

void testHostileInputsAreDefined() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  R1_EXPECT(std::isfinite(emboldenStrength(kModel, nan, 400)));
  R1_EXPECT_NEAR(emboldenStrength(kModel, inf, 400), emboldenStrength(kModel, 1.0f, 400), 1e-6);
  R1_EXPECT_NEAR(emboldenStrength(kModel, -3.0f, 600), emboldenStrength(kModel, 0.0f, 600), 1e-6);
  R1_EXPECT_NEAR(emboldenStrength(kModel, 5.0f, 500), emboldenStrength(kModel, 1.0f, 500), 1e-6);
  R1_EXPECT(emboldenStrength(kModel, 0.5f, std::numeric_limits<int>::min()) >= 0.0f);
  R1_EXPECT_NEAR(emboldenStrength(kModel, 0.5f, std::numeric_limits<int>::max()), emboldenStrength(kModel, 0.5f, 600), 1e-6);
}

void testShippedModelOrdersTheWeights() {
  // Bright text: the heavier the weight the thicker, and bright text is thicker than dark text.
  for (const float luminance : {0.13f, 0.53f, 0.88f, 1.0f}) {
    R1_EXPECT(emboldenStrength(kCalibratedWeights, luminance, 600) > emboldenStrength(kCalibratedWeights, luminance, 400));
  }
  for (const int weight : {400, 500, 600}) {
    R1_EXPECT(emboldenStrength(kCalibratedWeights, 0.9f, weight) > emboldenStrength(kCalibratedWeights, 0.4f, weight));
  }
  R1_EXPECT(emboldenStrength(kNoThickening, 1.0f, 600) == 0.0f);
}

}  // namespace

int main() {
  testLuminanceIsLinearBetweenTheAnchors();
  testWeightsInBetweenInterpolate();
  testNeverNegative();
  testHostileInputsAreDefined();
  testShippedModelOrdersTheWeights();
  return r1test::finish();
}
