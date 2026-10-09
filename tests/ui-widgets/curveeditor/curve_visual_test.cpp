// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: golden-image test of the curve graph (no OpenPencil reference exists for a curve editor): the
//   graph is rendered offscreen in both themes with known curves and the picture is checked
//   structurally: the ruler strip and canvas colours, the major and minor time and value grid lines at
//   the columns and rows the grid maths predicts (zero lines in the stronger colour), curve pixels on
//   the analytic curve to within one pixel (cubic, weighted, constant and linear segments, repeat
//   extrapolation), key markers at the key positions (selected in the accent colour), tangent
//   handles, the scrub line and a marquee. The renders are written as PNG into the artifact folder
//   for visual inspection.
// Callers: CTest (curveeditor gpu: renders offscreen on a Vulkan device, no window).
#include <cmath>
#include <cstdio>
#include <string>

#include "VisualSupport.h"
#include "r1ui/widgets/curveeditor/CurveGraph.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;
using r1ui::widgets::image::Image;

constexpr int kPad = 6;
constexpr int kW = 480;
constexpr int kH = 300;
const curve::View kView{0.0, 10.0, -1.0, 5.0};

curve::Key makeKey(uint32_t id, double t, double v, curve::Interp interp, curve::TangentMode mode = curve::TangentMode::AutoSmooth) {
  curve::Key k;
  k.id = id;
  k.time = t;
  k.value = v;
  k.interp = interp;
  k.mode = mode;
  return k;
}

std::vector<curve::Curve> testCurves() {
  curve::Curve red;
  red.id = 1;
  red.name = "Red";
  red.colour = {1.0, 0.0, 0.0};  // pure red: the detection weight R - (G + B) / 2 ignores grey
  red.keys = {makeKey(1, 1.0, 0.0, curve::Interp::Cubic), makeKey(2, 3.0, 3.0, curve::Interp::Cubic), makeKey(3, 5.0, 1.0, curve::Interp::Cubic), makeKey(4, 7.0, 4.0, curve::Interp::Linear),
              makeKey(5, 9.0, 2.0, curve::Interp::Linear)};
  curve::Curve green;
  green.id = 2;
  green.name = "Green";
  green.colour = {0.0, 0.8, 0.0};
  green.keys = {makeKey(1, 0.5, -0.5, curve::Interp::Constant), makeKey(2, 4.0, 2.5, curve::Interp::Constant), makeKey(3, 6.0, 0.5, curve::Interp::Linear)};
  green.post = curve::Extrapolation::Repeat;
  curve::Curve weighted;
  weighted.id = 3;
  weighted.name = "Weighted";
  weighted.colour = {1.0, 0.0, 0.0};
  weighted.keys = {makeKey(1, 2.0, 4.5, curve::Interp::Cubic, curve::TangentMode::Independent), makeKey(2, 6.0, 3.5, curve::Interp::Cubic, curve::TangentMode::Independent)};
  weighted.keys[0].outSlope = -1.5;
  weighted.keys[1].inSlope = 1.0;
  weighted.keys[0].weighted = weighted.keys[1].weighted = true;
  weighted.keys[0].outWeight = 0.6;
  weighted.keys[1].inWeight = 0.15;
  return {red, green, weighted};
}

WidgetId buildGraph(UiContext& ui, WidgetId parent, bool decorated) {
  CurveGraph& g = ui.create<CurveGraph>(parent);
  g.style().width = layout::Length::px(kW);
  g.style().height = layout::Length::px(kH);
  g.style().flexGrow = 0.0;
  std::vector<curve::Curve> curves = testCurves();
  if (!decorated) curves.resize(1);
  g.setCurves(curves);
  g.setView(kView);
  CurveSettings s = g.settings();
  s.framesPerSecond = 30.0;
  s.tangents = TangentVisibility::Selected;
  g.setSettings(s);
  if (decorated) {
    curve::Selection sel;
    sel.add({1, 2, curve::Part::Key});
    sel.add({3, 1, curve::Part::Key});
    g.setSelection(sel);
    g.setScrubTime(6.5);
  }
  return g.id();
}

struct Probe {
  const Image& img;
  uint8_t r(int x, int y) const { return img.rgba[(static_cast<size_t>(y) * img.width + static_cast<size_t>(x)) * 4 + 0]; }
  uint8_t g(int x, int y) const { return img.rgba[(static_cast<size_t>(y) * img.width + static_cast<size_t>(x)) * 4 + 1]; }
  uint8_t b(int x, int y) const { return img.rgba[(static_cast<size_t>(y) * img.width + static_cast<size_t>(x)) * 4 + 2]; }
  // Redness of a pixel: 0 for greys, 1 for pure red.
  double red(int x, int y) const { return std::max(0.0, (r(x, y) - (g(x, y) + b(x, y)) / 2.0) / 255.0); }
  double green(int x, int y) const { return std::max(0.0, (g(x, y) - (r(x, y) + b(x, y)) / 2.0) / 255.0); }
};

bool near(const Probe& p, int x, int y, const r1ui::theme::Color& c, int tol) {
  return std::abs(p.r(x, y) - c.r) <= tol && std::abs(p.g(x, y) - c.g) <= tol && std::abs(p.b(x, y) - c.b) <= tol;
}

// Centroid of the colour weight in the window [y0, y1] of column x; negative when there is none.
double centroid(const Probe& p, int x, int y0, int y1, bool useGreen) {
  double sum = 0.0;
  double weighted = 0.0;
  for (int y = y0; y <= y1; ++y) {
    const double w = useGreen ? p.green(x, y) : p.red(x, y);
    sum += w;
    weighted += w * (y + 0.5);
  }
  return sum > 0.15 ? weighted / sum : -1.0;
}

void checkTheme(r1ui::theme::ThemeId theme, const char* name) {
  const auto paths = r1test::visual::paths();
  r1ui::widgets::testing::RenderSpec spec;
  spec.width = kW + 2 * kPad;
  spec.height = kH + 2 * kPad;
  spec.theme = theme;
  spec.background = "panel";
  const r1ui::widgets::testing::BuildFn build = [](UiContext& ui, WidgetId parent) { return buildGraph(ui, parent, true); };
  const Image img = r1ui::widgets::testing::renderWidget(build, spec, paths);
  const std::string out = (paths.artifactDir / (std::string("curve-graph-golden-") + name + ".png")).string();
  r1ui::widgets::image::writePng(out, img.width, img.height, img.rgba);
  std::printf("curve graph golden (%s): %s\n", name, out.c_str());
  const Probe p{img};
  Services& services = r1ui::widgets::testing::sharedServices(paths);
  services.theme().set(theme);
  const auto token = [&](const char* n) { return *services.theme().color(n); };
  const curve::Mapping m{static_cast<double>(kPad), static_cast<double>(kPad) + CurveGraph::kRulerHeight, static_cast<double>(kW), kH - CurveGraph::kRulerHeight, kView};

  // ---- canvas and ruler colours ----
  R1_EXPECT(near(p, kPad + 3, kPad + 40, token("canvas"), 2) || near(p, kPad + 3, kPad + 41, token("canvas"), 2));
  R1_EXPECT(near(p, kPad + 200, kPad + 2, token("ruler-bg"), 2));

  // ---- grid lines ----
  const curve::TimeGrid tg = curve::timeGrid(kView.tMin, kView.tMax, m.w, 30.0);
  R1_EXPECT(!tg.lines.empty());
  int majorsChecked = 0;
  for (const curve::GridLine& l : tg.lines) {
    const int x = static_cast<int>(std::round(m.toX(l.position)));
    if (x < kPad + 2 || x >= kPad + kW - 2) continue;
    // Look at a row inside the plot that no curve crosses near this column: the very bottom.
    const int y = kPad + kH - 4;
    const bool zero = l.position == 0.0;
    const r1ui::theme::Color expected = zero ? token("border-strong") : token("border");
    if (l.major) {
      R1_EXPECT(near(p, x, y, expected, 3));
      ++majorsChecked;
    } else {
      R1_EXPECT(!near(p, x, y, token("canvas"), 2));  // a minor line is darker or lighter than the canvas
    }
    // The pixel between two lines is canvas.
    R1_EXPECT(near(p, x + 1, y, token("canvas"), 3) || tg.lines.size() > 100);
  }
  R1_EXPECT(majorsChecked >= 1 && tg.lines.size() >= 8);  // 5 s majors with 1 s minors over the 10 s view
  const std::vector<curve::GridLine> vg = curve::valueGrid(kView.vMin, kView.vMax, m.h);
  int rowsChecked = 0;
  for (const curve::GridLine& l : vg) {
    const int y = static_cast<int>(std::round(m.toY(l.position)));
    if (y < kPad + 30 || y >= kPad + kH - 2) continue;
    const int x = kPad + kW - 6;  // the far right: no key or label there
    // Not where a curve or a dotted value indicator (at the selected keys' values 3 and 4.5) crosses.
    bool crossed = std::abs(m.toY(3.0) - y) < 5.0 || std::abs(m.toY(4.5) - y) < 5.0;
    for (const curve::Curve& c : testCurves()) crossed |= std::abs(m.toY(curve::evaluate(c, m.toTime(x + 0.5))) - y) < 5.0;
    if (l.major && !crossed) {
      const bool zero = l.position == 0.0;
      R1_EXPECT(near(p, x, y, zero ? token("border-strong") : token("border"), 3));
      ++rowsChecked;
    }
  }
  R1_EXPECT(rowsChecked >= 2);

  // ---- the red curve (cubic, then linear) on its analytic curve to within one pixel ----
  curve::Curve red = testCurves()[0];
  int checked = 0;
  int worstColumn = 0;
  double worst = 0.0;
  for (int x = static_cast<int>(m.toX(1.0)) + 12; x < static_cast<int>(m.toX(9.0)) - 12; x += 3) {
    // Skip columns near keys, handles and the other curves that cross this one.
    bool skip = false;
    for (const curve::Key& k : red.keys) skip |= std::abs(m.toX(k.time) - x) < 14.0;
    const double t = m.toTime(x + 0.5);
    const double expected = m.toY(curve::evaluate(red, t));
    const double slope = std::abs(curve::slopeAt(red, t) * m.timePerPixel() / m.valuePerPixel());
    if (skip || slope > 1.0 || expected < m.y + 6 || expected > m.y + m.h - 6) continue;
    // The weighted curve and the green curve may cross: only trust columns where nothing else is near.
    bool crowded = false;
    for (const curve::Curve& other : testCurves()) {
      if (other.id == 1) continue;
      crowded |= std::abs(m.toY(curve::evaluate(other, t)) - expected) < 12.0;
    }
    if (crowded) continue;
    const double c = centroid(p, x, static_cast<int>(expected) - 6, static_cast<int>(expected) + 6, false);
    if (c < 0.0) {
      R1_EXPECT(false);
      continue;
    }
    ++checked;
    const double d = std::abs(c - expected);
    if (d > worst) {
      worst = d;
      worstColumn = x;
    }
  }
  std::printf("curve graph golden (%s): red curve, %d columns, worst deviation from the analytic curve %.2f px (column %d)\n", name, checked, worst, worstColumn);
  R1_EXPECT(checked >= 15);
  R1_EXPECT(worst <= 1.0);

  // ---- the constant (stepped) green curve: flat runs at the key values and a repeating tail ----
  curve::Curve green = testCurves()[1];
  for (const double t : {1.5, 3.0, 4.5, 5.0}) {
    const int x = static_cast<int>(m.toX(t));
    const double expected = m.toY(curve::evaluate(green, t));
    const double c = centroid(p, x, static_cast<int>(expected) - 6, static_cast<int>(expected) + 6, true);
    R1_EXPECT(c > 0.0 && std::abs(c - expected) <= 1.0);
  }
  // Repeat extrapolation after the last key at t = 6: the curve of the span 0.5 .. 6 tiles to the right.
  for (const double t : {7.0, 8.0, 9.2}) {
    const int x = static_cast<int>(m.toX(t));
    const double expected = m.toY(curve::evaluate(green, t));
    const double c = centroid(p, x, static_cast<int>(expected) - 6, static_cast<int>(expected) + 6, true);
    R1_EXPECT(c > 0.0 && std::abs(c - expected) <= 1.5);
  }

  // ---- markers: an unselected key is the curve colour, a selected key the accent with a white edge ----
  const int ux = static_cast<int>(std::round(m.toX(5.0)));
  const int uy = static_cast<int>(std::round(m.toY(1.0)));
  R1_EXPECT(p.red(ux, uy) > 0.8);
  const int sx = static_cast<int>(std::round(m.toX(3.0)));
  const int sy = static_cast<int>(std::round(m.toY(3.0)));
  R1_EXPECT(near(p, sx, sy, token("accent"), 12));
  // Tangent handles of the selected keys: a small accent-free disc of the curve colour at about 36 px.
  curve::Curve c1 = red;
  const curve::Slopes sl = curve::effectiveSlopes(c1, 1);
  const double dx = 1.0 / m.timePerPixel();
  const double dy = -sl.out / m.valuePerPixel();
  const double len = std::hypot(dx, dy);
  const int hx = static_cast<int>(std::round(m.toX(3.0) + dx / len * CurveGraph::kHandleLength));
  const int hy = static_cast<int>(std::round(m.toY(3.0) + dy / len * CurveGraph::kHandleLength));
  R1_EXPECT(p.red(hx, hy) > 0.6);
  // Scrub line at t = 6.5 in the accent colour, and the value indicators (dotted) at the selected values.
  const int scrubX = static_cast<int>(std::round(m.toX(6.5)));
  R1_EXPECT(near(p, scrubX, kPad + kH - 4, token("accent"), 6));
}

}  // namespace

int main() {
  checkTheme(r1ui::theme::ThemeId::Dark, "dark");
  checkTheme(r1ui::theme::ThemeId::Light, "light");
  return r1test::finish();
}
