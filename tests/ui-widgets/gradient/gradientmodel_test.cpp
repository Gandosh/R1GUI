// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for the gradient value model: the sorted-stops invariant under every mutator,
//   evaluation (ends, interior, hard edges, premultiplied alpha), the minimum / maximum stop counts,
//   stable ids across moves, and hostile input (NaN, infinities, huge lists, unknown ids).
// Callers: CTest (gradient, fast tier, no GPU).
#include <cmath>
#include <limits>
#include <random>

#include "TestSupport.h"
#include "r1ui/widgets/gradient/GradientModel.h"

namespace {

using namespace r1ui::widgets::gradient;
using r1ui::widgets::color::Rgba;

bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

bool invariantHolds(const Gradient& g) {
  if (g.size() < kMinStops || g.size() > kMaxStops) return false;
  for (size_t i = 0; i < g.size(); ++i) {
    const Stop& s = g.stops()[i];
    if (!(s.position >= 0.0 && s.position <= 1.0) || s.id == 0) return false;
    if (i > 0 && s.position < g.stops()[i - 1].position) return false;
    for (size_t j = 0; j < i; ++j) {
      if (g.stops()[j].id == s.id) return false;
    }
  }
  return true;
}

void testDefaultAndEvaluate() {
  Gradient g;
  R1_EXPECT(g.size() == 2 && invariantHolds(g) && g.type() == GradientType::Linear);
  R1_EXPECT(near(g.evaluate(0).rgb.r, 212.0 / 255) && g.evaluate(1).rgb.r == 1.0);
  R1_EXPECT(near(g.evaluate(0.5).rgb.r, (212.0 / 255 + 1) / 2));
  R1_EXPECT(g.evaluate(-5) == g.evaluate(0) && g.evaluate(9) == g.evaluate(1));
  R1_EXPECT(g.evaluate(std::numeric_limits<double>::quiet_NaN()) == g.evaluate(0));
  R1_EXPECT(g.evaluate(std::numeric_limits<double>::infinity()) == g.evaluate(1));

  // Before the first and after the last stop the end colours extend.
  Gradient h = Gradient::fromStops({{0.25, {{1, 0, 0}, 1}}, {0.75, {{0, 0, 1}, 1}}});
  R1_EXPECT(h.evaluate(0.1) == h.stops()[0].color && h.evaluate(0.9) == h.stops()[1].color);
  R1_EXPECT(near(h.evaluate(0.5).rgb.r, 0.5) && near(h.evaluate(0.5).rgb.b, 0.5));

  // Hard edge: two stops at the same position, the later one wins from there on.
  Gradient e = Gradient::fromStops({{0, {{1, 0, 0}, 1}}, {0.5, {{0, 1, 0}, 1}}, {0.5, {{0, 0, 1}, 1}}, {1, {{1, 1, 1}, 1}}});
  R1_EXPECT(e.evaluate(0.5).rgb.b == 1.0 && e.evaluate(0.4999).rgb.g > 0.99 && e.evaluate(0.5001).rgb.b > 0.99);

  // Transparent stops mix in premultiplied space: no grey halo from the colour of a clear stop.
  Gradient t = Gradient::fromStops({{0, {{1, 0, 0}, 1}}, {1, {{0, 1, 0}, 0}}});
  const Rgba mid = t.evaluate(0.5);
  R1_EXPECT(near(mid.a, 0.5) && near(mid.rgb.r, 1.0) && near(mid.rgb.g, 0.0));
}

void testAddKeepsLook() {
  Gradient g;
  const Rgba before = g.evaluate(0.3);
  const uint32_t id = g.addStop(0.3);
  R1_EXPECT(id != 0 && g.size() == 3 && invariantHolds(g));
  R1_EXPECT(g.find(id)->color == before);
  R1_EXPECT(near(g.evaluate(0.3).rgb.r, before.rgb.r) && near(g.evaluate(0.15).rgb.r, Gradient().evaluate(0.15).rgb.r, 1e-6));
  // Out of range clamps; NaN is refused.
  const uint32_t lo = g.addStop(-3, Rgba{{0, 0, 0}, 1});
  const uint32_t hi = g.addStop(7, Rgba{{0, 0, 0}, 1});
  R1_EXPECT(g.find(lo)->position == 0.0 && g.find(hi)->position == 1.0 && invariantHolds(g));
  R1_EXPECT(g.addStop(std::numeric_limits<double>::quiet_NaN()) == 0);
  // A new stop on top of an existing one goes after it.
  const uint32_t same = g.addStop(0.3, Rgba{{1, 0, 0}, 1});
  R1_EXPECT(g.indexOf(same) == g.indexOf(id) + 1);
  // The count is bounded.
  Gradient many;
  for (int i = 0; i < 1000; ++i) many.addStop(i / 1000.0);
  R1_EXPECT(many.size() == kMaxStops && many.addStop(0.5) == 0 && invariantHolds(many));
}

void testRemoveMoveAndIds() {
  Gradient g;
  const uint32_t a = g.stops()[0].id;
  const uint32_t b = g.stops()[1].id;
  R1_EXPECT(!g.removeStop(a) && !g.removeStop(b) && g.size() == 2);  // never below two stops
  const uint32_t c = g.addStop(0.5);
  R1_EXPECT(g.removeStop(c) && g.size() == 2 && !g.removeStop(c) && !g.removeStop(0) && !g.removeStop(999));
  const uint32_t d = g.addStop(0.5);
  R1_EXPECT(d != c);  // ids are never reused
  // Moving the first stop past the middle one re-sorts and keeps its identity.
  R1_EXPECT(g.moveStop(a, 0.8) && g.indexOf(a) == 1 && g.indexOf(d) == 0 && invariantHolds(g));
  R1_EXPECT(!g.moveStop(a, 0.8));                        // no change
  R1_EXPECT(!g.moveStop(a, std::numeric_limits<double>::quiet_NaN()) && g.find(a)->position == 0.8);
  R1_EXPECT(g.moveStop(a, 1e9) && g.find(a)->position == 1.0);
  R1_EXPECT(g.moveStop(a, -1e9) && g.find(a)->position == 0.0 && g.indexOf(a) <= 1);
  R1_EXPECT(!g.moveStop(12345, 0.2));
  // Moving onto an equal position: right goes after, left goes before.
  Gradient h = Gradient::fromStops({{0, {{1, 0, 0}, 1}}, {0.5, {{0, 1, 0}, 1}}, {0.5, {{0, 0, 1}, 1}}, {1, {{1, 1, 1}, 1}}});
  const uint32_t first = h.stops()[0].id;
  h.moveStop(first, 0.5);
  R1_EXPECT(h.indexOf(first) == 2);
  const uint32_t last = h.stops()[3].id;
  h.moveStop(last, 0.5);
  R1_EXPECT(h.indexOf(last) == 0 && invariantHolds(h));
  // Recolour.
  R1_EXPECT(g.setStopColor(a, {{0.2, 0.3, 0.4}, 0.5}) && !g.setStopColor(a, {{0.2, 0.3, 0.4}, 0.5}) && !g.setStopColor(777, {}));
  R1_EXPECT(g.setStopColor(a, {{std::numeric_limits<double>::quiet_NaN(), 9, -9}, 4}) && g.find(a)->color.rgb.g == 1.0 && g.find(a)->color.a == 1.0);
}

void testGeometry() {
  Gradient g;
  R1_EXPECT(g.setType(GradientType::Radial) && !g.setType(GradientType::Radial));
  R1_EXPECT(g.setAngle(-90) && g.angleDegrees() == 270 && g.setAngle(720.5) && near(g.angleDegrees(), 0.5));
  R1_EXPECT(!g.setAngle(std::numeric_limits<double>::infinity()) && !g.setAngle(std::numeric_limits<double>::quiet_NaN()) && near(g.angleDegrees(), 0.5));
  R1_EXPECT(g.setCenter(2, -1) && g.centerX() == 1 && g.centerY() == 0 && !g.setCenter(std::numeric_limits<double>::quiet_NaN(), 0.5));
}

void testFromStopsHostile() {
  R1_EXPECT(invariantHolds(Gradient::fromStops({})));
  R1_EXPECT(invariantHolds(Gradient::fromStops({{0.5, {}}})));
  std::vector<std::pair<double, Rgba>> huge;
  for (int i = 0; i < 1'000'000; ++i) huge.push_back({std::fmod(i * 0.37, 3.0) - 1.0, Rgba{{std::numeric_limits<double>::quiet_NaN(), 0, 0}, 1}});
  const Gradient g = Gradient::fromStops(std::move(huge));
  R1_EXPECT(invariantHolds(g) && g.size() == kMaxStops);
  // A random walk of operations never breaks the invariants.
  std::mt19937 rng(7);
  Gradient w;
  std::uniform_real_distribution<double> pos(-0.2, 1.2);
  for (int i = 0; i < 20000; ++i) {
    switch (rng() % 4) {
      case 0: w.addStop(pos(rng)); break;
      case 1: if (!w.stops().empty()) w.removeStop(w.stops()[rng() % w.size()].id); break;
      case 2: w.moveStop(w.stops()[rng() % w.size()].id, pos(rng)); break;
      default: w.setStopColor(w.stops()[rng() % w.size()].id, {{pos(rng), pos(rng), pos(rng)}, pos(rng)}); break;
    }
    if (!invariantHolds(w)) {
      R1_EXPECT(false);
      break;
    }
  }
  // Evaluation at 1e6 positions stays finite and inside range.
  bool ok = true;
  for (int i = 0; i < 1'000'000; ++i) {
    const Rgba c = w.evaluate(i / 1.0e6);
    ok = ok && c.rgb.r >= 0 && c.rgb.r <= 1 && c.a >= 0 && c.a <= 1 && std::isfinite(c.rgb.g);
  }
  R1_EXPECT(ok);
}

}  // namespace

int main() {
  testDefaultAndEvaluate();
  testAddKeepsLook();
  testRemoveMoveAndIds();
  testGeometry();
  testFromStopsHostile();
  return r1test::finish();
}
