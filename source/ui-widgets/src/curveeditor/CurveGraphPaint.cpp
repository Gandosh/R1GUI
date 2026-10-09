// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: painting of CurveGraph: background, grid and labels, the time ruler, curves as polylines,
//   key and tangent markers, value indicators, the scrub marker and the marquee.
// Invariants: paint never mutates curves or selection; every coordinate handed to the Painter is
//   finite and bounded (screen coordinates are clamped to +-1e6 px before use); the cost of a frame
//   follows the plot size and the keys inside the visible time range: keys and segments are found by
//   binary search, cubic segments are flattened to a 0.5 px tolerance in screen space, and a
//   polyline with more points than pixel columns is reduced to the envelope of each column.
// Callers: UiContext paint traversal through CurveGraph::paint / paintOver.
#include <algorithm>
#include <cmath>

#include "r1ui/widgets/colorpicker/NumberText.h"
#include "r1ui/widgets/curveeditor/CurveGraph.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using curve::Curve;
using curve::Key;
using curve::Part;

constexpr double kFlatness = 0.5;      // px
constexpr double kLabelPx = 10.0;      // logical font size of grid labels
constexpr double kClampPx = 1.0e6;

double clampPx(double v) {
  if (std::isnan(v)) return 0.0;
  return std::clamp(v, -kClampPx, kClampPx);
}

render::Color rgba(const color::Rgb& c, float a = 1.0f) { return {static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b), a}; }

render::Color withAlpha(render::Color c, float a) {
  c.a *= a;
  return c;
}

// Reduces a dense polyline to the envelope of each pixel column: the first, lowest, highest and last
// point of the column in order of appearance.
void decimate(std::vector<curve::Point>& pts, double columns) {
  if (pts.size() <= static_cast<size_t>(2.0 * columns) + 8) return;
  std::vector<curve::Point> out;
  out.reserve(static_cast<size_t>(columns) * 4 + 8);
  size_t i = 0;
  while (i < pts.size()) {
    const double col = std::floor(pts[i].x);
    size_t j = i;
    size_t lo = i;
    size_t hi = i;
    while (j < pts.size() && std::floor(pts[j].x) == col) {
      if (pts[j].y < pts[lo].y) lo = j;
      if (pts[j].y > pts[hi].y) hi = j;
      ++j;
    }
    const size_t last = j - 1;
    std::vector<size_t> pick = {i, lo, hi, last};
    std::sort(pick.begin(), pick.end());
    pick.erase(std::unique(pick.begin(), pick.end()), pick.end());
    for (const size_t k : pick) out.push_back(pts[k]);
    i = j;
  }
  pts = std::move(out);
}

}  // namespace

// ---- polyline of a curve ----------------------------------------------------------------------

void CurveGraph::curvePolyline(const curve::Mapping& m, const Curve& c, std::vector<curve::Point>& out) const {
  out.clear();
  const size_t n = c.keys.size();
  if (n == 0) return;
  const double tpp = m.timePerPixel();
  const double tLo = m.view.tMin - 2.0 * tpp;
  const double tHi = m.view.tMax + 2.0 * tpp;
  const auto P = [&](double t, double v) { return curve::Point{clampPx(m.toX(t)), clampPx(m.toY(v))}; };
  const Key& first = c.keys.front();
  const Key& last = c.keys.back();

  // Sampled extrapolation between two times (repeat modes) or a straight line (constant, linear).
  const auto extrapolate = [&](double ta, double tb, curve::Extrapolation mode) {
    if (!(tb > ta)) return;
    if (mode == curve::Extrapolation::Constant || mode == curve::Extrapolation::Linear || n < 2) {
      out.push_back(P(ta, curve::evaluate(c, ta)));
      out.push_back(P(tb, curve::evaluate(c, tb)));
      return;
    }
    const double xa = m.toX(ta);
    const double xb = m.toX(tb);
    const size_t steps = static_cast<size_t>(std::min(4000.0, std::max(1.0, std::ceil((xb - xa) / 1.5))));
    for (size_t s = 0; s <= steps; ++s) {
      const double t = ta + (tb - ta) * static_cast<double>(s) / static_cast<double>(steps);
      out.push_back(P(t, curve::evaluate(c, t)));
    }
  };

  if (tLo < first.time) extrapolate(tLo, std::min(first.time, tHi), c.pre);
  if (first.time <= tHi && last.time >= tLo) {
    size_t i = curve::segmentIndex(c, std::max(tLo, first.time));
    if (i == curve::npos) i = 0;
    bool started = false;
    for (; i < n && c.keys[i].time <= tHi; ++i) {
      const Key& k0 = c.keys[i];
      if (!started) {
        out.push_back(P(k0.time, k0.value));
        started = true;
      }
      if (i + 1 >= n) break;
      const Key& k1 = c.keys[i + 1];
      switch (k0.interp) {
        case curve::Interp::Constant:
          out.push_back(P(k1.time, k0.value));
          out.push_back(P(k1.time, k1.value));
          break;
        case curve::Interp::Linear: out.push_back(P(k1.time, k1.value)); break;
        case curve::Interp::Cubic: {
          curve::Bezier b;
          if (!curve::segmentBezier(c, i, b)) {
            out.push_back(P(k1.time, k1.value));
            break;
          }
          curve::Bezier s;
          for (int q = 0; q < 4; ++q) {
            s.x[q] = clampPx(m.toX(b.x[q]));
            s.y[q] = clampPx(m.toY(b.y[q]));
          }
          curve::flattenCubic(s, kFlatness, out);
          break;
        }
      }
    }
  }
  if (tHi > last.time) extrapolate(std::max(last.time, tLo), tHi, c.post);
  decimate(out, m.w);
}

// ---- grid -------------------------------------------------------------------------------------

namespace {

void drawLabel(PaintContext& ctx, const render::Rect& box, const std::string& text, double xLogical, double yTopLogical, const render::Color& colour) {
  const float size = ctx.px(kLabelPx);
  TextEngine& engine = ctx.ui().text();
  const float top = box.y + ctx.px(yTopLogical);
  engine.draw(ctx.painter(), text, size, 400, box.x + ctx.px(xLogical), top + engine.baselineInBox(size, ctx.px(12.0)), colour);
}

}  // namespace

void CurveGraph::paintGrid(PaintContext& ctx, const curve::Mapping& m) {
  const render::Rect box = ctx.box();
  render::Painter& painter = ctx.painter();
  const float hair = ctx.hairline();
  const render::Color minorColour = withAlpha(ctx.color("border"), 0.55f);
  const render::Color majorColour = ctx.color("border");
  const render::Color zeroColour = ctx.color("border-strong");
  const double plotTop = m.y;

  // ---- time lines and the ruler ----
  const curve::TimeGrid tg = curve::timeGrid(m.view.tMin, m.view.tMax, m.w, settings_.framesPerSecond);
  painter.fillRect({box.x, box.y, box.w, ctx.px(kRulerHeight)}, ctx.color("ruler-bg"));
  const render::Color tick = ctx.color("ruler-tick");
  const render::Color text = ctx.color("ruler-text");
  float nextFreeX = -1e9f;
  for (const curve::GridLine& line : tg.lines) {
    const double x = m.toX(line.position);
    const float px = box.x + std::round(ctx.px(x));
    const bool zero = line.position == 0.0;
    painter.fillRect({px, box.y + ctx.px(plotTop), hair, ctx.px(m.h)}, zero ? zeroColour : (line.major ? majorColour : minorColour));
    painter.fillRect({px, box.y + ctx.px(line.major ? kRulerHeight - 8.0 : kRulerHeight - 4.0), hair, ctx.px(line.major ? 8.0 : 4.0)}, tick);
    if (line.major && !line.label.empty()) {
      const float width = ctx.ui().text().measure(line.label, ctx.px(kLabelPx));
      if (px + ctx.px(3.0) >= nextFreeX) {  // rule 20: a label that would collide with the previous one is not drawn
        drawLabel(ctx, box, line.label, x + 3.0, 4.0, text);
        nextFreeX = px + ctx.px(3.0) + width + ctx.px(6.0);
      }
    }
  }
  painter.fillRect({box.x, box.y + ctx.px(kRulerHeight) - hair, box.w, hair}, majorColour);

  // ---- value lines and their labels ----
  const std::vector<curve::GridLine> vg = curve::valueGrid(m.view.vMin, m.view.vMax, m.h);
  float lastLabelY = 1e9f;
  for (const curve::GridLine& line : vg) {
    const double y = m.toY(line.position);
    const float py = box.y + std::round(ctx.px(y));
    const bool zero = line.position == 0.0;
    painter.fillRect({box.x, py, box.w, hair}, zero ? zeroColour : (line.major ? majorColour : minorColour));
    if (line.major && !line.label.empty()) {
      const float labelTop = py - ctx.px(13.0);
      if (std::fabs(labelTop - lastLabelY) >= ctx.px(13.0) && y - 13.0 >= plotTop) {
        drawLabel(ctx, box, line.label, 4.0, y - 13.0, text);
        lastLabelY = labelTop;
      }
    }
  }
}

// ---- curves -----------------------------------------------------------------------------------

void CurveGraph::paintCurve(PaintContext& ctx, const curve::Mapping& m, const Curve& c, bool emphasised) {
  curvePolyline(m, c, pointScratch_);
  drawnPoints_ += pointScratch_.size();
  if (pointScratch_.empty()) return;
  const render::Rect box = ctx.box();
  render::Color colour = rgba(c.colour, c.locked ? 0.55f : 1.0f);
  const float width = ctx.px(emphasised ? 2.5 : 1.5);
  render::Painter& painter = ctx.painter();
  const auto X = [&](double x) { return box.x + ctx.px(x); };
  const auto Y = [&](double y) { return box.y + ctx.px(y); };
  if (pointScratch_.size() == 1) {
    painter.fillRect({X(pointScratch_[0].x) - width * 0.5f, Y(pointScratch_[0].y) - width * 0.5f, width, width}, colour);
    return;
  }
  for (size_t i = 1; i < pointScratch_.size(); ++i) {
    const curve::Point& a = pointScratch_[i - 1];
    const curve::Point& b = pointScratch_[i];
    if (a.x == b.x && a.y == b.y) continue;
    painter.line(X(a.x), Y(a.y), X(b.x), Y(b.y), width, colour);
  }
}

void CurveGraph::paintKeys(PaintContext& ctx, const curve::Mapping& m, const Curve& c, bool handlesPass) {
  if (c.keys.empty()) return;
  const render::Rect box = ctx.box();
  render::Painter& painter = ctx.painter();
  const double tpp = m.timePerPixel();
  const double reach = kKeySize + 2.0;
  size_t i0 = curve::segmentIndex(c, m.view.tMin - reach * tpp);
  if (i0 == curve::npos) i0 = 0;
  size_t i1 = curve::segmentIndex(c, m.view.tMax + reach * tpp);
  if (i1 == curve::npos) return;
  const size_t visible = i1 - i0 + 1;
  const bool dense = static_cast<double>(visible) * 2.0 > m.w;  // fewer than 2 px per key: the markers would merge into a blob
  // Markers are drawn on whole pixels (centre on a pixel centre) so that they stay crisp.
  const float size = std::max(1.0f, std::round(ctx.px(kKeySize)));
  const float handleSize = std::max(1.0f, std::round(ctx.px(5.0)));
  const auto snapped = [](float v) { return std::floor(v) + 0.5f; };
  const render::Color accent = ctx.color("accent");
  const render::Color white{1, 1, 1, 1};
  const render::Color base = rgba(c.colour);
  const bool editable = curveEditable(c);

  const auto drawKey = [&](const Key& k, bool selected) {
    const float cx = snapped(box.x + ctx.px(m.toX(k.time)));
    const float cy = snapped(box.y + ctx.px(m.toY(k.value)));
    const render::Rect r{cx - size * 0.5f, cy - size * 0.5f, size, size};
    if (selected) {
      painter.fillRect(r, accent);
      painter.border(r, render::CornerRadii::uniform(0.0f), ctx.hairline(), white);
    } else {
      painter.fillRect(r, editable ? base : withAlpha(base, 0.55f));
      painter.border(r, render::CornerRadii::uniform(0.0f), ctx.hairline(), withAlpha(white, 0.8f));
    }
    ++drawnKeys_;
  };

  const auto drawHandles = [&](size_t index) {
    const Key& k = c.keys[index];
    for (const bool out : {false, true}) {
      curve::Point p;
      if (!handlePosition(c.id, k.id, out, p)) continue;
      const float kx = snapped(box.x + ctx.px(m.toX(k.time)));
      const float ky = snapped(box.y + ctx.px(m.toY(k.value)));
      const float hx = snapped(box.x + ctx.px(clampPx(p.x)));
      const float hy = snapped(box.y + ctx.px(clampPx(p.y)));
      const bool selected = selection_.contains({c.id, k.id, out ? Part::Out : Part::In});
      painter.line(kx, ky, hx, hy, ctx.hairline(), withAlpha(base, 0.7f));
      const render::Rect hr{hx - handleSize * 0.5f, hy - handleSize * 0.5f, handleSize, handleSize};
      painter.fillRoundedRect(hr, render::CornerRadii::uniform(handleSize * 0.5f), selected ? accent : base);
      painter.border(hr, render::CornerRadii::uniform(handleSize * 0.5f), ctx.hairline(), selected ? white : withAlpha(white, 0.8f));
    }
  };

  const auto handlesShownFor = [&](const Key& k, bool selected) {
    if (!editable) return false;
    if (settings_.tangents == TangentVisibility::All) return !dense || selected;
    return settings_.tangents == TangentVisibility::Selected && (selected || selection_.anyPartSelected(c.id, k.id));
  };
  // The handles of every curve go under the markers of every curve: a tangent line never crosses a key square.
  for (size_t i = i0; i <= i1 && i < c.keys.size(); ++i) {
    const Key& k = c.keys[i];
    const bool selected = selection_.containsKey(c.id, k.id);
    if (handlesPass) {
      if (handlesShownFor(k, selected)) drawHandles(i);
    } else if (!dense || selected) {
      drawKey(k, selected);
    }
  }
}

void CurveGraph::paintIndicators(PaintContext& ctx, const curve::Mapping& m) {
  const render::Rect box = ctx.box();
  render::Painter& painter = ctx.painter();
  const render::Color accent = ctx.color("accent");
  const float hair = ctx.hairline();
  // Value indicators: dotted lines at the lowest and highest selected key value (rule 28).
  if (settings_.valueIndicators && selection_.hasKeys()) {
    bool any = false;
    double lo = 0.0;
    double hi = 0.0;
    const curve::KeyLookup lookup(curves_);
    for (const curve::Selected& s : selection_.items()) {
      if (s.part != Part::Key) continue;
      const size_t i = lookup.indexOf(s.curve, s.key);
      if (i == curve::npos) continue;
      const double v = lookup.curve(s.curve)->keys[i].value;
      lo = any ? std::min(lo, v) : v;
      hi = any ? std::max(hi, v) : v;
      any = true;
    }
    if (any) {
      const render::Color dot = withAlpha(ctx.color("muted"), 0.8f);
      for (const double v : {lo, hi}) {
        const double y = m.toY(v);
        if (y < m.y || y > m.y + m.h) continue;
        const float py = box.y + std::round(ctx.px(y));
        const float step = ctx.px(6.0);
        size_t dashes = 0;
        for (float x = box.x; x < box.x + box.w && dashes < 2000; x += step, ++dashes) painter.fillRect({x, py, ctx.px(3.0), hair}, dot);
        if (lo == hi) break;
      }
    }
  }
  // Scrub marker.
  if (scrubTime_ >= m.view.tMin && scrubTime_ <= m.view.tMax) {
    const float x = box.x + std::round(ctx.px(m.toX(scrubTime_)));
    painter.fillRect({x, box.y, hair, box.h}, accent);
    const float w = ctx.px(7.0);
    painter.fillRect({x - w * 0.5f + hair * 0.5f, box.y, w, ctx.px(5.0)}, accent);
  }
}

void CurveGraph::paintMarquee(PaintContext& ctx) {
  const render::Rect box = ctx.box();
  render::Painter& painter = ctx.painter();
  const render::Color accent = ctx.color("accent");
  const float hair = ctx.hairline();
  if (gesture_.kind == Gesture::Kind::Marquee && gesture_.active) {
    const double x0 = std::min(gesture_.startX, gesture_.marqueeX);
    const double y0 = std::min(gesture_.startY, gesture_.marqueeY);
    const double w = std::fabs(gesture_.marqueeX - gesture_.startX);
    const double h = std::fabs(gesture_.marqueeY - gesture_.startY);
    const render::Rect r{box.x + ctx.px(x0), box.y + ctx.px(y0), ctx.px(w), ctx.px(h)};
    painter.fillRect(r, withAlpha(accent, 0.15f));
    painter.border(r, render::CornerRadii::uniform(0.0f), hair, accent);
  }
}

void CurveGraph::paint(PaintContext& ctx) {
  drawnKeys_ = 0;
  drawnPoints_ = 0;
  const curve::Mapping m = plotMapping();
  const render::Rect box = ctx.box();
  render::Painter& painter = ctx.painter();
  painter.fillRect(box, ctx.color("canvas"));
  paintGrid(ctx, m);

  painter.pushClip({box.x, box.y + ctx.px(kRulerHeight), box.w, box.h - ctx.px(kRulerHeight)});
  // Curves: plain ones first, then the ones with selected keys, the hovered one on top.
  std::vector<const Curve*> order;
  for (const Curve& c : curves_) {
    if (curveDrawn(c)) order.push_back(&c);
  }
  const auto rank = [&](const Curve* c) {
    if (c->id == hoverCurve_) return 2;
    return selection_.keysOf(c->id).empty() ? 0 : 1;
  };
  std::stable_sort(order.begin(), order.end(), [&](const Curve* a, const Curve* b) { return rank(a) < rank(b); });
  for (const Curve* c : order) paintCurve(ctx, m, *c, rank(c) > 0);
  paintIndicators(ctx, m);  // over the curves, under the tangent handles and the markers
  for (const Curve* c : order) paintKeys(ctx, m, *c, true);
  for (const Curve* c : order) paintKeys(ctx, m, *c, false);
  paintMarquee(ctx);
  painter.popClip();
}

void CurveGraph::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.box(), 0.0f);
}

}  // namespace r1ui::widgets
