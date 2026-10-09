// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for layout::layoutTree. Each case states a layout whose pixel results were worked
//   out by hand from the CSS Flexible Box Layout rules (comments show the arithmetic), plus the
//   hostile inputs (NaN, infinity, negatives, 1e9+, zero-size, cyclic percent) and the 10k-node
//   timing that is recorded in the slice evidence.
// Callers: CTest (label fast).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <vector>

#include "TestSupport.h"

using namespace core_test;
using namespace r1ui::core::layout;
using r1ui::core::tree::TreeLimits;

namespace {

// Text-like leaf: natural single-line width w; wraps into ceil(w / available) lines of lineH.
class TestMeasure : public MeasureProvider {
 public:
  struct Spec {
    double width;
    double lineHeight;
  };
  std::unordered_map<WidgetId, Spec> specs;
  int calls = 0;
  const char* deepestMarker = nullptr;  // address of a local in the most recent callback

  MeasureResult measure(WidgetId id, const MeasureInput& in) override {
    ++calls;
    char marker = 0;
    deepestMarker = &marker;
    const auto it = specs.find(id);
    if (it == specs.end()) return {};
    double w = it->second.width;
    if (in.widthMode == MeasureMode::Exactly) w = in.width;
    else if (in.widthMode == MeasureMode::AtMost) w = std::min(w, in.width);
    const double lines = std::ceil(it->second.width / std::max(w, 1.0));
    return {w, lines * it->second.lineHeight};
  }
};

WidgetId addText(WidgetTree& t, WidgetId parent, TestMeasure& m, double width, double lineH, Style s = Style{}) {
  s.hasMeasure = true;
  const WidgetId id = addChild(t, parent, s);
  m.specs[id] = {width, lineH};
  return id;
}

void run(WidgetTree& t, WidgetId root, double w, double h, MeasureProvider* m = nullptr) {
  (void)layoutTree(t, root, LayoutInput{w, h}, m);
}

Style flexItem(double w, double h, double grow = 0.0, double shrink = 1.0) {
  Style s = sized(w, h);
  s.flexGrow = grow;
  s.flexShrink = shrink;
  return s;
}

void rowGrow() {
  // 300 wide, bases 50/100/50 (200), free 100 split 1:2:0 -> 83.33 / 166.67 / 50.
  // Edges 0, 83.33, 250, 300 round to 0, 83, 250, 300.
  WidgetTree t;
  const WidgetId root = addRoot(t, sized(300, 100));
  const WidgetId a = addChild(t, root, flexItem(50, 0, 1));
  const WidgetId b = addChild(t, root, flexItem(100, 0, 2));
  const WidgetId c = addChild(t, root, flexItem(50, 0, 0));
  t.get(a)->style.height = Length::autoValue();
  t.get(b)->style.height = Length::autoValue();
  t.get(c)->style.height = Length::autoValue();
  run(t, root, 300, 100);
  expectRect(rectOf(t, a), 0, 0, 83, 100, "A grows by one third");
  expectRect(rectOf(t, b), 83, 0, 167, 100, "B grows by two thirds");
  expectRect(rectOf(t, c), 250, 0, 50, 100, "C does not grow; items stretch to the container height");
}

void columnShrink() {
  // Column of height 150: bases 200 and 100 (300), overflow 150. Scaled shrink factors are
  // 1*200 and 2*100, equal, so each loses 75 -> 125 and 25.
  WidgetTree t;
  const WidgetId root = addRoot(t, sized(100, 150));
  t.get(root)->style.direction = FlexDirection::Column;
  Style sa = sized(0, 200);
  sa.width = Length::autoValue();
  Style sb = sized(0, 100);
  sb.width = Length::autoValue();
  sb.flexShrink = 2;
  const WidgetId a = addChild(t, root, sa);
  const WidgetId b = addChild(t, root, sb);
  run(t, root, 100, 150);
  expectRect(rectOf(t, a), 0, 0, 100, 125, "A shrinks to 125");
  expectRect(rectOf(t, b), 0, 125, 100, 25, "B shrinks to 25");

  // With min-height 40 on B, B freezes at 40 and A absorbs the rest: 150 - 40 = 110.
  t.get(b)->style.minHeight = Length::px(40);
  run(t, root, 100, 150);
  expectRect(rectOf(t, a), 0, 0, 100, 110, "A takes what B's minimum leaves");
  expectRect(rectOf(t, b), 0, 110, 100, 40, "B frozen at its minimum");
}

void wrapWithGap() {
  // 100 wide, 40 px items with a 10 px gap: two fit (90), the third wraps.
  WidgetTree t;
  Style rs = sized(100, 100);
  rs.wrap = FlexWrap::Wrap;
  rs.gapRow = 10;
  rs.gapColumn = 10;
  rs.alignContent = AlignContent::Start;
  const WidgetId root = addRoot(t, rs);
  const WidgetId a = addChild(t, root, flexItem(40, 20, 0, 0));
  const WidgetId b = addChild(t, root, flexItem(40, 20, 0, 0));
  const WidgetId c = addChild(t, root, flexItem(40, 20, 0, 0));
  run(t, root, 100, 100);
  expectRect(rectOf(t, a), 0, 0, 40, 20, "first line, first item");
  expectRect(rectOf(t, b), 50, 0, 40, 20, "first line, second item after the gap");
  expectRect(rectOf(t, c), 0, 30, 40, 20, "second line starts after the row gap");

  // align-content stretch: free = 100 - (20 + 10 + 20) = 50, +25 per line -> line 2 at 45 + 10.
  t.get(root)->style.alignContent = AlignContent::Stretch;
  run(t, root, 100, 100);
  expectRect(rectOf(t, c), 0, 55, 40, 20, "stretched lines push the second line down");

  // align-content center: 50 free -> 25 above the lines.
  t.get(root)->style.alignContent = AlignContent::Center;
  run(t, root, 100, 100);
  expectRect(rectOf(t, a), 0, 25, 40, 20, "centered lines");

  // wrap-reverse stacks lines from the bottom: line 1 at y=80, line 2 at y=50.
  t.get(root)->style.alignContent = AlignContent::Start;
  t.get(root)->style.wrap = FlexWrap::WrapReverse;
  run(t, root, 100, 100);
  expectRect(rectOf(t, a), 0, 80, 40, 20, "wrap-reverse first line at the bottom");
  expectRect(rectOf(t, c), 0, 50, 40, 20, "wrap-reverse second line above it");
}

void justifyContent() {
  WidgetTree t;
  Style rs = sized(300, 50);
  rs.justifyContent = Justify::SpaceBetween;
  const WidgetId root = addRoot(t, rs);
  // Outer widths: A 10+50+10 = 70, B 50, C 20+50 = 70 -> 190; free 110; between = 55.
  Style sa = flexItem(50, 0, 0, 0);
  sa.height = Length::autoValue();
  sa.margin[kLeft] = Length::px(10);
  sa.margin[kRight] = Length::px(10);
  Style sb = flexItem(50, 0, 0, 0);
  sb.height = Length::autoValue();
  Style sc = sb;
  sc.margin[kLeft] = Length::px(20);
  const WidgetId a = addChild(t, root, sa);
  const WidgetId b = addChild(t, root, sb);
  const WidgetId c = addChild(t, root, sc);
  run(t, root, 300, 50);
  expectRect(rectOf(t, a), 10, 0, 50, 50, "A after its left margin");
  expectRect(rectOf(t, b), 125, 0, 50, 50, "B: 70 + 55 gap");
  expectRect(rectOf(t, c), 250, 0, 50, 50, "C ends flush right: 230 + 20 margin");

  // Three 50 px items in 300: free 150.
  t.get(a)->style.margin[kLeft] = Length::px(0);
  t.get(a)->style.margin[kRight] = Length::px(0);
  t.get(c)->style.margin[kLeft] = Length::px(0);
  t.get(root)->style.justifyContent = Justify::SpaceAround;  // 50 between, 25 at the ends
  run(t, root, 300, 50);
  expectRect(rectOf(t, a), 25, 0, 50, 50, "space-around first");
  expectRect(rectOf(t, b), 125, 0, 50, 50, "space-around middle");
  expectRect(rectOf(t, c), 225, 0, 50, 50, "space-around last");
  t.get(root)->style.justifyContent = Justify::SpaceEvenly;  // 37.5 slots: 37.5, 125, 212.5
  run(t, root, 300, 50);
  expectRect(rectOf(t, a), 38, 0, 50, 50, "space-evenly first (37.5 rounds half up)");
  expectRect(rectOf(t, b), 125, 0, 50, 50, "space-evenly middle");
  expectRect(rectOf(t, c), 213, 0, 50, 50, "space-evenly last (212.5 rounds half up)");
  t.get(root)->style.justifyContent = Justify::Center;
  run(t, root, 300, 50);
  expectRect(rectOf(t, a), 75, 0, 50, 50, "center");
  t.get(root)->style.justifyContent = Justify::End;
  run(t, root, 300, 50);
  expectRect(rectOf(t, c), 250, 0, 50, 50, "end");
  // Negative free space: space-between falls back to start.
  t.get(root)->style.justifyContent = Justify::SpaceBetween;
  t.get(a)->style.width = Length::px(200);
  t.get(b)->style.width = Length::px(200);
  t.get(a)->style.flexShrink = 0;
  t.get(b)->style.flexShrink = 0;
  t.get(c)->style.flexShrink = 0;
  run(t, root, 300, 50);
  expectRect(rectOf(t, a), 0, 0, 200, 50, "overflowing space-between starts at the start edge");
  expectRect(rectOf(t, b), 200, 0, 200, 50, "and packs the items");
}

void directionsReverse() {
  WidgetTree t;
  Style rs = sized(300, 100);
  rs.direction = FlexDirection::RowReverse;
  const WidgetId root = addRoot(t, rs);
  const WidgetId a = addChild(t, root, flexItem(50, 20));
  const WidgetId b = addChild(t, root, flexItem(50, 20));
  run(t, root, 300, 100);
  expectRect(rectOf(t, a), 250, 0, 50, 20, "row-reverse: first item at the right edge");
  expectRect(rectOf(t, b), 200, 0, 50, 20, "row-reverse: second item to its left");
  t.get(root)->style.direction = FlexDirection::ColumnReverse;
  t.get(a)->style.height = Length::px(30);
  t.get(b)->style.height = Length::px(30);
  run(t, root, 300, 100);
  expectRect(rectOf(t, a), 0, 70, 50, 30, "column-reverse: first item at the bottom");
  expectRect(rectOf(t, b), 0, 40, 50, 30, "column-reverse: second above it");
}

void percentSizes() {
  WidgetTree t;
  const WidgetId root = addRoot(t, sized(400, 200));
  Style cs;
  cs.width = Length::percent(25);
  cs.height = Length::percent(50);
  cs.flexShrink = 0;
  const WidgetId c = addChild(t, root, cs);
  run(t, root, 400, 200);
  expectRect(rectOf(t, c), 0, 0, 100, 100, "25% x 50% of 400 x 200");
  t.get(root)->style.padding[kLeft] = 20;
  t.get(root)->style.padding[kRight] = 20;
  t.get(root)->style.padding[kTop] = 20;
  t.get(root)->style.padding[kBottom] = 20;
  run(t, root, 400, 200);
  expectRect(rectOf(t, c), 20, 20, 90, 80, "percentages resolve against the padded inner box (360 x 160)");
}

void minMaxClamping() {
  WidgetTree t;
  const WidgetId root = addRoot(t, sized(300, 100));
  Style big = sized(500, 0);
  big.height = Length::autoValue();
  big.minWidth = Length::px(400);
  const WidgetId a = addChild(t, root, big);
  run(t, root, 300, 100);
  expectRect(rectOf(t, a), 0, 0, 400, 100, "min-width beats the shrink to 300");

  WidgetTree t2;
  const WidgetId r2 = addRoot(t2, sized(300, 100));
  Style ga;
  ga.flexGrow = 1;
  ga.flexBasis = Length::px(0);
  ga.maxWidth = Length::px(100);
  Style gb;
  gb.flexGrow = 1;
  gb.flexBasis = Length::px(0);
  const WidgetId x = addChild(t2, r2, ga);
  const WidgetId y = addChild(t2, r2, gb);
  run(t2, r2, 300, 100);
  expectRect(rectOf(t2, x), 0, 0, 100, 100, "A stops at max-width");
  expectRect(rectOf(t2, y), 100, 0, 200, 100, "B takes the remaining 200");

  // min wins over max.
  t2.get(x)->style.minWidth = Length::px(150);
  run(t2, r2, 300, 100);
  expectRect(rectOf(t2, x), 0, 0, 150, 100, "min-width 150 overrides max-width 100");
}

void absolutePositioning() {
  WidgetTree t;
  const WidgetId root = addRoot(t, sized(200, 100));
  const WidgetId flow = addChild(t, root, flexItem(60, 20, 0, 0));
  Style a = sized(50, 30);
  a.position = Position::Absolute;
  a.inset[kLeft] = Length::px(10);
  a.inset[kTop] = Length::px(20);
  const WidgetId abs1 = addChild(t, root, a);
  Style b = sized(40, 20);
  b.position = Position::Absolute;
  b.inset[kRight] = Length::px(10);
  b.inset[kBottom] = Length::px(10);
  const WidgetId abs2 = addChild(t, root, b);
  Style c;
  c.position = Position::Absolute;
  c.inset[kLeft] = Length::px(10);
  c.inset[kRight] = Length::px(10);
  c.inset[kTop] = Length::px(0);
  c.inset[kBottom] = Length::px(0);
  const WidgetId abs3 = addChild(t, root, c);
  Style p = sized(0, 10);
  p.width = Length::percent(50);
  p.position = Position::Absolute;
  p.inset[kLeft] = Length::percent(10);
  const WidgetId abs4 = addChild(t, root, p);
  run(t, root, 200, 100);
  expectRect(rectOf(t, flow), 0, 0, 60, 20, "absolute children do not take part in the flow");
  expectRect(rectOf(t, abs1), 10, 20, 50, 30, "left/top insets");
  expectRect(rectOf(t, abs2), 150, 70, 40, 20, "right/bottom insets: 200-10-40, 100-10-20");
  expectRect(rectOf(t, abs3), 10, 0, 180, 100, "left+right+top+bottom with auto size fills the gap");
  expectRect(rectOf(t, abs4), 20, 0, 100, 10, "percent inset and size resolve against the container");
  // Static position follows justify-content / align-items of the container.
  Style d = sized(20, 20);
  d.position = Position::Absolute;
  const WidgetId abs5 = addChild(t, root, d);
  t.get(root)->style.justifyContent = Justify::Center;
  t.get(root)->style.alignItems = Align::End;
  run(t, root, 200, 100);
  expectRect(rectOf(t, abs5), 90, 80, 20, 20, "static position centered on main, end on cross");
}

void crossAlignment() {
  WidgetTree t;
  const WidgetId root = addRoot(t, sized(200, 100));
  Style cs = sized(40, 20);
  const WidgetId c = addChild(t, root, cs);
  t.get(root)->style.alignItems = Align::Center;
  run(t, root, 200, 100);
  expectRect(rectOf(t, c), 0, 40, 40, 20, "center");
  t.get(root)->style.alignItems = Align::End;
  run(t, root, 200, 100);
  expectRect(rectOf(t, c), 0, 80, 40, 20, "end");
  t.get(root)->style.alignItems = Align::Start;
  t.get(c)->style.alignSelf = Align::End;
  run(t, root, 200, 100);
  expectRect(rectOf(t, c), 0, 80, 40, 20, "align-self overrides align-items");
  t.get(c)->style.alignSelf = Align::Baseline;
  run(t, root, 200, 100);
  expectRect(rectOf(t, c), 0, 0, 40, 20, "baseline lays out as start (documented)");
  t.get(c)->style.alignSelf = Align::Auto;
  t.get(root)->style.alignItems = Align::Stretch;
  t.get(c)->style.height = Length::autoValue();
  run(t, root, 200, 100);
  expectRect(rectOf(t, c), 0, 0, 40, 100, "stretch with auto height fills the line");
  t.get(c)->style.maxHeight = Length::px(60);
  run(t, root, 200, 100);
  expectRect(rectOf(t, c), 0, 0, 40, 60, "stretch is clamped by max-height");

  // Auto-height row container takes the tallest item; stretched siblings follow.
  WidgetTree t2;
  const WidgetId r2 = addRoot(t2, sized(200, 500));
  t2.get(r2)->style.alignItems = Align::Start;
  const WidgetId wrapper = addChild(t2, r2, Style{});
  t2.get(wrapper)->style.width = Length::px(200);
  t2.get(wrapper)->style.flexShrink = 0;
  addChild(t2, wrapper, sized(10, 30));
  Style au = sized(10, 0);
  au.height = Length::autoValue();
  const WidgetId stretched = addChild(t2, wrapper, au);
  run(t2, r2, 200, 500);
  expectRect(rectOf(t2, wrapper), 0, 0, 200, 30, "auto-height row takes the tallest item");
  expectRect(rectOf(t2, stretched), 10, 0, 10, 30, "auto-height sibling stretches to it");
}

void contentSizedContainers() {
  // A row container with auto width inside a 500 wide row: 30 + 5 gap + 40 = 75.
  WidgetTree t;
  const WidgetId root = addRoot(t, sized(500, 100));
  Style inner;
  inner.gapColumn = 5;
  inner.flexShrink = 0;
  const WidgetId box = addChild(t, root, inner);
  addChild(t, box, sized(30, 10));
  addChild(t, box, sized(40, 25));
  run(t, root, 500, 100);
  expectRect(rectOf(t, box), 0, 0, 75, 100, "auto width = max-content of the children plus gaps");
  // Auto-height column: height is the sum of the items plus gaps.
  Style wrapper;
  wrapper.direction = FlexDirection::Column;
  WidgetTree t3;
  const WidgetId r3 = addRoot(t3, sized(80, 500));
  const WidgetId w3 = addChild(t3, r3, wrapper);
  t3.get(w3)->style.alignSelf = Align::Start;
  t3.get(w3)->style.gapRow = 4;
  t3.get(w3)->style.flexShrink = 0;
  t3.get(r3)->style.direction = FlexDirection::Column;
  addChild(t3, w3, sized(10, 20));
  addChild(t3, w3, sized(10, 30));
  run(t3, r3, 80, 500);
  expectRect(rectOf(t3, w3), 0, 0, 10, 54, "auto-height column = 20 + 4 + 30, width = widest child");
  // Padding is part of the content-sized box (border-box sizing).
  t3.get(w3)->style.padding[kLeft] = 3;
  t3.get(w3)->style.padding[kTop] = 2;
  t3.get(w3)->style.padding[kBottom] = 2;
  run(t3, r3, 80, 500);
  expectRect(rectOf(t3, w3), 0, 0, 13, 58, "padding adds to the content size");
}

void nestedFlex() {
  WidgetTree t;
  Style rs = sized(200, 200);
  rs.direction = FlexDirection::Column;
  const WidgetId root = addRoot(t, rs);
  Style top;
  top.flexGrow = 1;
  const WidgetId row = addChild(t, root, top);
  Style leaf;
  leaf.flexGrow = 1;
  const WidgetId l1 = addChild(t, row, leaf);
  const WidgetId l2 = addChild(t, row, leaf);
  const WidgetId bottom = addChild(t, root, sized(0, 50));
  t.get(bottom)->style.width = Length::autoValue();
  run(t, root, 200, 200);
  expectRect(rectOf(t, row), 0, 0, 200, 150, "growing row takes 200 - 50");
  expectRect(rectOf(t, l1), 0, 0, 100, 150, "first leaf");
  expectRect(rectOf(t, l2), 100, 0, 100, 150, "second leaf");
  expectRect(rectOf(t, bottom), 0, 150, 200, 50, "fixed-height footer");
  expectRect(absOf(t, l2), 100, 0, 100, 150, "absolute rect of the nested leaf");
}

void roundingWithoutGaps() {
  WidgetTree t;
  const WidgetId root = addRoot(t, sized(100, 10));
  std::vector<WidgetId> kids;
  for (int i = 0; i < 3; ++i) {
    Style s;
    s.flexGrow = 1;
    s.flexBasis = Length::px(0);
    kids.push_back(addChild(t, root, s));
  }
  run(t, root, 100, 10);
  expectRect(rectOf(t, kids[0]), 0, 0, 33, 10, "thirds: 33");
  expectRect(rectOf(t, kids[1]), 33, 0, 34, 10, "thirds: 34");
  expectRect(rectOf(t, kids[2]), 67, 0, 33, 10, "thirds: 33");

  // Property: for many widths and counts siblings tile the row with no gap and no overlap, and
  // the widths add up to the container, with and without a fractional offset and gaps.
  for (int count = 1; count <= 9; ++count) {
    for (int width = 1; width <= 61; width += 5) {
      for (const double offset : {0.0, 0.5, 0.25}) {
        WidgetTree tt;
        Style rs = sized(width, 10);
        rs.padding[kLeft] = offset;
        rs.padding[kRight] = 0;
        const WidgetId r = addRoot(tt, rs);
        std::vector<WidgetId> ids;
        for (int i = 0; i < count; ++i) {
          Style s;
          s.flexGrow = 1;
          s.flexBasis = Length::px(0);
          ids.push_back(addChild(tt, r, s));
        }
        run(tt, r, width, 10);
        bool ok = true;
        int sum = 0;
        for (int i = 0; i < count; ++i) {
          const Rect& a = absOf(tt, ids[static_cast<size_t>(i)]);
          sum += a.w;
          if (a.w < 0) ok = false;
          if (i + 1 < count && a.right() != absOf(tt, ids[static_cast<size_t>(i + 1)]).x) ok = false;
        }
        const Rect& first = absOf(tt, ids.front());
        const Rect& last = absOf(tt, ids.back());
        if (first.x != static_cast<int>(std::floor(offset + 0.5))) ok = false;
        if (last.right() != width) ok = false;
        if (sum != width - first.x) ok = false;
        if (!ok) {
          std::fprintf(stderr, "  tiling broke for count=%d width=%d offset=%.2f\n", count, width, offset);
        }
        expect(ok, "siblings tile the container without gaps");
      }
    }
  }
}

void propertyPanel() {
  // Panel 258 wide with 12 px side padding -> 234 px content width (docs/spec/widgets.md).
  WidgetTree t;
  Style ps = sized(258, 200);
  ps.direction = FlexDirection::Column;
  ps.padding[kLeft] = 12;
  ps.padding[kRight] = 12;
  ps.padding[kTop] = 8;
  ps.gapRow = 6;
  const WidgetId panel = addRoot(t, ps);

  // Row 1: two half-width fields, gap 6 -> (234 - 6) / 2 = 114 each.
  Style rowS;
  rowS.gapColumn = 6;
  rowS.height = Length::px(26);
  rowS.flexShrink = 0;
  const WidgetId row1 = addChild(t, panel, rowS);
  Style half;
  half.flexGrow = 1;
  half.flexBasis = Length::px(0);
  const WidgetId f1 = addChild(t, row1, half);
  const WidgetId f2 = addChild(t, row1, half);
  // Row 2: one field plus the 26 px rail.
  const WidgetId row2 = addChild(t, panel, rowS);
  const WidgetId g1 = addChild(t, row2, half);
  Style railS = sized(26, 0);
  railS.height = Length::autoValue();
  railS.flexShrink = 0;
  const WidgetId rail = addChild(t, row2, railS);
  // Row 3: "two-rail": two 114 px columns plus a rail; the 266 px of content overflows the
  // 234 px row by 32, which the shrinkable columns absorb equally (114 - 16 = 98 each).
  const WidgetId row3 = addChild(t, panel, rowS);
  Style col = sized(114, 0);
  col.height = Length::autoValue();
  const WidgetId h1 = addChild(t, row3, col);
  const WidgetId h2 = addChild(t, row3, col);
  const WidgetId rail3 = addChild(t, row3, railS);

  run(t, panel, 258, 200);
  expectRect(absOf(t, row1), 12, 8, 234, 26, "row spans the 234 px content width");
  expectRect(absOf(t, f1), 12, 8, 114, 26, "left half field: 114 x 26 at x=12");
  expectRect(absOf(t, f2), 132, 8, 114, 26, "right half field: 114 x 26 at x=12+114+6");
  expectRect(absOf(t, row2), 12, 40, 234, 26, "second row after the 6 px row gap");
  expectRect(absOf(t, g1), 12, 40, 202, 26, "field beside the rail: 234 - 6 - 26 = 202");
  expectRect(absOf(t, rail), 220, 40, 26, 26, "26 px rail at the right edge");
  expectRect(absOf(t, row3), 12, 72, 234, 26, "third row");
  expectRect(absOf(t, h1), 12, 72, 98, 26, "two-rail: first column shrinks 114 -> 98");
  expectRect(absOf(t, h2), 116, 72, 98, 26, "two-rail: second column");
  expectRect(absOf(t, rail3), 220, 72, 26, 26, "two-rail: rail keeps 26");
}

void displayNone() {
  WidgetTree t;
  Style rs = sized(300, 50);
  rs.gapColumn = 10;
  const WidgetId root = addRoot(t, rs);
  const WidgetId a = addChild(t, root, flexItem(50, 20, 0, 0));
  const WidgetId hidden = addChild(t, root, flexItem(50, 20, 0, 0));
  const WidgetId c = addChild(t, root, flexItem(50, 20, 0, 0));
  t.get(hidden)->style.display = Display::None;
  addChild(t, hidden, sized(5, 5));
  run(t, root, 300, 50);
  expectRect(rectOf(t, a), 0, 0, 50, 20, "first");
  expectRect(rectOf(t, c), 60, 0, 50, 20, "hidden item contributes no size and no gap");
  expect(rectOf(t, hidden).w == 0 && rectOf(t, hidden).h == 0, "hidden item has an empty rect");
  expect(!t.get(hidden)->layoutDirty, "hidden subtree holds no stale dirty bit");
}

void aspectAndAutoMargins() {
  WidgetTree t;
  Style rs = sized(400, 400);
  rs.alignItems = Align::Start;
  const WidgetId root = addRoot(t, rs);
  Style ar;
  ar.width = Length::px(100);
  ar.aspectRatio = 2.0;
  ar.flexShrink = 0;
  const WidgetId a = addChild(t, root, ar);
  run(t, root, 400, 400);
  expectRect(rectOf(t, a), 0, 0, 100, 50, "aspect ratio 2:1 derives the height");

  WidgetTree t2;
  const WidgetId r2 = addRoot(t2, sized(300, 100));
  Style m = flexItem(50, 20, 0, 0);
  m.margin[kLeft] = Length::autoValue();
  const WidgetId b = addChild(t2, r2, m);
  run(t2, r2, 300, 100);
  expectRect(rectOf(t2, b), 250, 0, 50, 20, "auto left margin pushes the item right");
  t2.get(b)->style.margin[kRight] = Length::autoValue();
  run(t2, r2, 300, 100);
  expectRect(rectOf(t2, b), 125, 0, 50, 20, "auto margins on both sides center it");
  t2.get(b)->style.margin[kTop] = Length::autoValue();
  t2.get(b)->style.margin[kBottom] = Length::autoValue();
  run(t2, r2, 300, 100);
  expectRect(rectOf(t2, b), 125, 40, 50, 20, "auto margins on the cross axis center it vertically");
}

void measureCallbacks() {
  TestMeasure m;
  // A 60 wide row holding a text of natural width 80: it shrinks to 60 and wraps to two lines.
  WidgetTree t;
  Style rs;
  rs.width = Length::px(60);
  const WidgetId root = addRoot(t, rs);
  const WidgetId text = addText(t, root, m, 80, 20);
  run(t, root, 60, 500, &m);
  expectRect(rectOf(t, text), 0, 0, 60, 500, "stretched to the viewport height given to the root");

  // Same row but content-sized in height: the root's own height is auto via a wrapper.
  WidgetTree t2;
  const WidgetId outer = addRoot(t2, sized(60, 500));
  t2.get(outer)->style.alignItems = Align::Start;
  Style wrap;
  wrap.flexShrink = 1;
  const WidgetId box = addChild(t2, outer, wrap);
  const WidgetId txt = addText(t2, box, m, 80, 20);
  run(t2, outer, 60, 500, &m);
  expectRect(rectOf(t2, txt), 0, 0, 60, 40, "text wrapped to two lines at 60 px");
  expectRect(rectOf(t2, box), 0, 0, 60, 40, "wrapper height follows the measured text");

  // With room the text keeps its natural single line.
  run(t2, outer, 200, 500, &m);
  expectRect(rectOf(t2, txt), 0, 0, 80, 20, "natural size when there is room");

  // fit-content width: never stretches, caps at the available width.
  t2.get(box)->style.width = Length::fitContent();
  t2.get(outer)->style.direction = FlexDirection::Column;
  t2.get(outer)->style.alignItems = Align::Stretch;
  run(t2, outer, 100, 500, &m);
  expect(rectOf(t2, box).w == 80, "fit-content = max-content when it fits");
  m.specs[txt].width = 150;
  run(t2, outer, 100, 500, &m);
  expect(rectOf(t2, box).w == 100, "fit-content is capped by the available width");

  // Hostile measure results are sanitised.
  struct Evil : MeasureProvider {
    MeasureResult measure(WidgetId, const MeasureInput&) override {
      return {std::numeric_limits<double>::quiet_NaN(), -50.0};
    }
  } evil;
  WidgetTree t3;
  const WidgetId r3 = addRoot(t3, sized(100, 100));
  Style leaf;
  leaf.hasMeasure = true;
  leaf.alignSelf = Align::Start;
  const WidgetId l3 = addChild(t3, r3, leaf);
  run(t3, r3, 100, 100, &evil);
  expectRect(rectOf(t3, l3), 0, 0, 0, 0, "NaN / negative measure results become 0");
  run(t3, r3, 100, 100, nullptr);
  expectRect(rectOf(t3, l3), 0, 0, 0, 0, "a missing provider measures 0");
}

void cacheKeepsNestingLinear() {
  // 40 nested content-sized containers around one text leaf; without the cache the number of
  // measurements would double with each level.
  TestMeasure m;
  WidgetTree t(TreeLimits{256, 1000});
  const WidgetId root = addRoot(t, sized(300, 300));
  WidgetId cur = root;
  for (int i = 0; i < 40; ++i) {
    Style s;
    s.alignSelf = Align::Start;
    cur = addChild(t, cur, s);
  }
  addText(t, cur, m, 80, 20);
  const LayoutStats stats = layoutTree(t, root, LayoutInput{300, 300}, &m);
  std::fprintf(stderr, "INFO nested-40: %zu measure calls, %zu cache hits, %zu committed\n", stats.measureCalls,
               stats.cacheHits, stats.nodesCommitted);
  expect(stats.measureCalls < 200, "nested content-sized containers measure linearly");
  expect(stats.nodesCommitted == 42, "every node committed once");
  expectRect(absOf(t, cur), 0, 0, 80, 20, "innermost container wraps the text exactly");
}

void deepNesting() {
  // The deepest supported tree (512 levels) of content-sized containers around a text leaf:
  // measuring recurses once per level, so this guards the stack use per level.
  TestMeasure m;
  WidgetTree t(TreeLimits{512, 2000});
  const WidgetId root = addRoot(t, sized(300, 300));
  WidgetId cur = root;
  for (int i = 0; i < 511; ++i) {
    Style s;
    s.alignSelf = Align::Start;
    cur = addChild(t, cur, s);
  }
  addText(t, cur, m, 80, 20);
  char top = 0;
  const LayoutStats stats = layoutTree(t, root, LayoutInput{300, 300}, &m);
  const long long used = std::llabs(static_cast<long long>(&top - m.deepestMarker));
  std::fprintf(stderr, "INFO deep-nesting: about %lld stack bytes per level at the deepest measure\n", used / 1024);
  expect(used / 1024 < 800, "layout recursion stays under 800 stack bytes per tree level");
  expect(stats.nodesCommitted == 513, "every level committed");
  expectRect(absOf(t, cur), 0, 0, 80, 20, "innermost container wraps the text");
  expectRect(absOf(t, t.parent(cur)), 0, 0, 80, 20, "and so does its parent");
}

void hostileInputs() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();

  // Style sanitising.
  Style s;
  s.width = Length::px(nan);
  s.height = Length::px(-5);
  s.flexGrow = nan;
  s.flexShrink = -3;
  s.padding[kLeft] = -10;
  s.gapRow = inf;
  s.margin[kTop] = Length::px(nan);
  s.margin[kLeft] = Length::percent(10);
  s.inset[kLeft] = Length::px(nan);
  s.aspectRatio = nan;
  s.minWidth = Length::px(-4);
  const Style q = sanitizeStyle(s);
  expect(q.width.kind == Length::Kind::Auto && q.height.kind == Length::Kind::Auto, "NaN / negative sizes -> auto");
  expect(q.flexGrow == 0.0 && q.flexShrink == 0.0, "grow NaN -> 0, shrink negative -> 0");
  expect(q.padding[kLeft] == 0.0 && q.gapRow == kMaxExtent, "negative padding -> 0, infinite gap clamps");
  expect(q.margin[kTop].value == 0.0 && q.margin[kLeft].value == 0.0, "NaN / percent margins -> 0");
  expect(q.inset[kLeft].kind == Length::Kind::Auto && q.aspectRatio == 0.0, "NaN inset -> auto, NaN ratio -> none");
  expect(q.minWidth.value == 0.0, "negative min -> 0");

  // Hostile viewport.
  WidgetTree t;
  const WidgetId root = addRoot(t);
  const WidgetId child = addChild(t, root, flexItem(10, 10));
  run(t, root, nan, -5);
  expectRect(rectOf(t, root), 0, 0, 0, 0, "NaN / negative viewport -> 0 x 0");
  run(t, root, inf, 1e300);
  expect(rectOf(t, root).w == 1000000000 && rectOf(t, root).h == 1000000000, "infinite viewport clamps to 1e9");
  (void)child;

  // Zero-size container with growing, percent and wrapping children: finite zero results.
  WidgetTree z;
  Style zs = sized(0, 0);
  zs.wrap = FlexWrap::Wrap;
  zs.justifyContent = Justify::SpaceAround;
  const WidgetId zr = addRoot(z, zs);
  Style g;
  g.flexGrow = 1;
  g.width = Length::percent(50);
  g.height = Length::percent(50);
  const WidgetId zc = addChild(z, zr, g);
  addChild(z, zc, flexItem(5, 5));
  run(z, zr, 0, 0);
  expectRect(rectOf(z, zc), 0, 0, 0, 0, "children of a zero-size container collapse");

  // Huge sizes: nothing overflows; every output stays inside +-1e9.
  WidgetTree h;
  const WidgetId hr = addRoot(h, sized(1e9, 100));
  std::vector<WidgetId> hs;
  for (int i = 0; i < 4; ++i) {
    Style hug = sized(1e300, 1e300);
    hug.flexShrink = 0;
    hs.push_back(addChild(h, hr, hug));
  }
  addChild(h, hs[0], flexItem(1e9, 1e9, 1e9, 0));
  run(h, hr, 1e9, 100);
  bool inRange = true;
  h.forEachDescendant(hr, [&](WidgetId id) {
    const Rect& r = absOf(h, id);
    inRange = inRange && r.w >= 0 && r.h >= 0 && std::abs(int64_t{r.x}) <= 1000000000 &&
              std::abs(int64_t{r.y}) <= 1000000000 && r.w <= 1000000000 && r.h <= 1000000000;
  });
  expect(inRange, "1e9 and 1e300 sizes stay inside the coordinate range");

  // Cyclic percent: an auto-height container cannot resolve a percent height of its content.
  WidgetTree c;
  Style cs;
  cs.direction = FlexDirection::Column;
  cs.width = Length::px(200);
  cs.alignSelf = Align::Start;
  const WidgetId outer = addRoot(c, sized(300, 300));
  const WidgetId auto1 = addChild(c, outer, cs);
  Style pct;
  pct.height = Length::percent(50);
  pct.flexShrink = 0;
  const WidgetId pc = addChild(c, auto1, pct);
  run(c, outer, 300, 300);
  expectRect(rectOf(c, auto1), 0, 0, 200, 0, "auto-height parent ignores the child's percent height");
  expectRect(rectOf(c, pc), 0, 0, 200, 0, "cyclic percent height behaves as auto");
  // Content-sized parent width with a percent child: the percent counts as auto while sizing.
  WidgetTree d;
  const WidgetId dr = addRoot(d, sized(500, 50));
  Style ps;
  ps.flexShrink = 0;
  const WidgetId dbox = addChild(d, dr, ps);
  Style half;
  half.width = Length::percent(50);
  half.flexShrink = 0;
  const WidgetId dhalf = addChild(d, dbox, half);
  addChild(d, dbox, flexItem(100, 10, 0, 0));
  run(d, dr, 500, 50);
  expectRect(rectOf(d, dbox), 0, 0, 100, 50, "box width = 0 (percent as auto) + 100");
  expect(rectOf(d, dhalf).w == 50, "percent resolves against the final width (50% of 100)");
}

void measuredPerformance() {
  // 100 rows x 99 children (+ 100 rows + root) = 10,001 nodes: fixed, growing and text leaves.
  TestMeasure m;
  WidgetTree t(TreeLimits{256, 20000});
  Style rs = sized(1200, 4000);
  rs.direction = FlexDirection::Column;
  const WidgetId root = addRoot(t, rs);
  for (int r = 0; r < 100; ++r) {
    Style row;
    row.height = Length::px(30);
    row.flexShrink = 0;
    row.alignItems = Align::Center;
    row.gapColumn = 2;
    const WidgetId rowId = addChild(t, root, row);
    for (int i = 0; i < 99; ++i) {
      if (i % 3 == 0) {
        addChild(t, rowId, flexItem(8, 20, 0, 0));
      } else if (i % 3 == 1) {
        Style g;
        g.flexGrow = 1;
        g.height = Length::px(20);
        addChild(t, rowId, g);
      } else {
        addText(t, rowId, m, 6, 14);
      }
    }
  }
  expect(t.nodeCount() == 10001, "perf tree has 10,001 nodes");
  double best = 1e9;
  double total = 0;
  const int runs = 7;
  LayoutStats stats;
  for (int i = 0; i < runs; ++i) {
    const auto t0 = std::chrono::steady_clock::now();
    stats = layoutTree(t, root, LayoutInput{1200, 4000}, &m);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    best = std::min(best, ms);
    total += ms;
  }
  std::fprintf(stderr,
               "PERF layout: 10,001 nodes full pass: best %.2f ms, mean %.2f ms over %d runs "
               "(committed %zu, measure calls %zu, cache hits %zu)\n",
               best, total / runs, runs, stats.nodesCommitted, stats.measureCalls, stats.cacheHits);
  expect(best < 250.0, "10k-node full layout stays well inside a frame budget even unoptimised");
  expect(absOf(t, root).w == 1200, "perf tree laid out");
}

}  // namespace

int main() {
  runCase("row_grow", rowGrow);
  runCase("column_shrink", columnShrink);
  runCase("wrap_with_gap", wrapWithGap);
  runCase("justify_content", justifyContent);
  runCase("directions_reverse", directionsReverse);
  runCase("percent_sizes", percentSizes);
  runCase("min_max_clamping", minMaxClamping);
  runCase("absolute_positioning", absolutePositioning);
  runCase("cross_alignment", crossAlignment);
  runCase("content_sized_containers", contentSizedContainers);
  runCase("nested_flex", nestedFlex);
  runCase("rounding_without_gaps", roundingWithoutGaps);
  runCase("property_panel", propertyPanel);
  runCase("display_none", displayNone);
  runCase("aspect_and_auto_margins", aspectAndAutoMargins);
  runCase("measure_callbacks", measureCallbacks);
  runCase("cache_keeps_nesting_linear", cacheKeepsNestingLinear);
  runCase("deep_nesting", deepNesting);
  runCase("hostile_inputs", hostileInputs);
  runCase("measured_performance", measuredPerformance);
  return finish("ui-core.layout");
}
