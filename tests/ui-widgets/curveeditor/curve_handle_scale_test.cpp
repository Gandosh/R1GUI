// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression test for the cost of tangent handles on long curves (phase 4 review H4, M15): with
//   every key of a cubic curve selected and a window of a few hundred keys in the middle of the
//   array, painting and pointer hit testing must cost the same at 100000 keys as at 5000 (the cost
//   depends on the visible keys, not on the curve length), and selecting everything stays linear.
// Why: handle positions were found by a linear search for the key by id, once per visible key; at
//   20000 keys a paint took 180 ms and at 80000 keys 820 ms. The existing 100000 key test looked at
//   the start of the array and missed it.
// Callers: CTest (fast tier). Timing checks compare two sizes of the same run (a ratio), so they hold
//   in Debug and Release builds alike; the absolute figures are printed.
#include <chrono>
#include <cmath>
#include <cstdio>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/curveeditor/CurveEditor.h"

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;

namespace {

double msSince(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

struct Timing {
  double paintMs = 0.0;
  double moveMs = 0.0;
  double selectAllMs = 0.0;
  size_t selected = 0;
};

Timing measure(int n) {
  r1test::TestUi t{900, 700};
  t.ui.rootStyle().alignItems = layout::Align::Start;
  CurveEditor& editor = t.ui.create<CurveEditor>(t.ui.root());
  editor.style().width = layout::Length::px(800);
  editor.style().height = layout::Length::px(600);
  t.layout();
  curve::Curve big;
  big.id = 1;
  big.name = "big";
  for (int i = 0; i < n; ++i) {
    curve::Key k;
    k.id = static_cast<uint32_t>(i + 1);
    k.time = i * 0.01;
    k.value = std::sin(i * 0.05) * 3.0;
    k.interp = curve::Interp::Cubic;
    big.keys.push_back(k);
  }
  editor.graph().setCurves({big});
  const double mid = n * 0.01 * 0.5;
  editor.graph().setView({mid, mid + 2.0, -4.0, 4.0});  // about 200 keys visible, deep in the array
  Timing out;
  auto t0 = std::chrono::steady_clock::now();
  editor.graph().selectAll();
  out.selectAllMs = msSince(t0);
  out.selected = editor.graph().selection().size();

  r1ui::render::Painter painter;
  const auto paint = [&] {
    painter.begin(900, 700);
    t.ui.paint(painter);
    t.ui.finishPaint();
    painter.end();
  };
  paint();
  t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 3; ++i) paint();
  out.paintMs = msSince(t0) / 3.0;

  t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 20; ++i) t.ui.pointerMove(100 + i, 300);
  out.moveMs = msSince(t0) / 20.0;
  return out;
}

}  // namespace

int main() {
  const Timing small = measure(5000);
  const Timing large = measure(100000);
  std::printf("curve handles: 5000 keys paint %.2f ms move %.3f ms selectAll %.2f ms; 100000 keys paint %.2f ms move %.3f ms selectAll %.2f ms\n",
              small.paintMs, small.moveMs, small.selectAllMs, large.paintMs, large.moveMs, large.selectAllMs);
  R1_EXPECT(large.selected == 100000);
  // Visible-key cost: 20x more keys in the curve may cost a little more (cache effects), never 20x.
  R1_EXPECT(large.paintMs < 6.0 * small.paintMs + 3.0);
  R1_EXPECT(large.moveMs < 6.0 * small.moveMs + 1.0);
  R1_EXPECT(large.selectAllMs < 100.0 * small.selectAllMs + 50.0);  // selecting everything is not quadratic
  return r1test::finish();
}
