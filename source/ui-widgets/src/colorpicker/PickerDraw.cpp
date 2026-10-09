// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PickerDraw.h.
// Invariants: rectangles are snapped to whole physical pixels first; strips between the corner zones
//   are whole rectangles, corner pixels are single pixels whose alpha is the exact (4 x 4 sampled)
//   area under the corner arc, so a radius of half the height gives a true semicircular end.
// Callers: the colour picker and the gradient editor widgets.
#include "r1ui/widgets/colorpicker/PickerDraw.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets::pickerdraw {

namespace {

struct Snapped {
  float x, y, w, h;
  bool valid;
};

Snapped snap(const render::Rect& box) {
  if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.w) || !std::isfinite(box.h)) return {0, 0, 0, 0, false};
  const float x0 = std::round(box.x);
  const float y0 = std::round(box.y);
  const float x1 = std::round(box.x + box.w);
  const float y1 = std::round(box.y + box.h);
  if (x1 <= x0 || y1 <= y0) return {0, 0, 0, 0, false};
  return {x0, y0, x1 - x0, y1 - y0, true};
}

float clampRadius(float radius, float w, float h) {
  if (!(radius > 0.0f)) return 0.0f;
  return std::min(radius, std::min(w, h) * 0.5f);
}

// Centre of the corner circle on one axis for pixel `p` of a span of `length`, or a negative value
// when the pixel is outside both corner zones of that axis.
float axisCentre(int p, float length, float r) {
  if (static_cast<float>(p) < r) return r;
  if (static_cast<float>(p + 1) > length - r) return length - r;
  return -1.0f;
}

// Area of pixel (px, py) of a w x h rounded rectangle with corner radius r that lies inside it, in
// 0..1 (exact away from the arc, 4 x 4 supersampling on it).
float coverage(int px, int py, float w, float h, float r) {
  const float cx = axisCentre(px, w, r);
  const float cy = axisCentre(py, h, r);
  if (cx < 0.0f || cy < 0.0f) return 1.0f;
  int inside = 0;
  for (int a = 0; a < 4; ++a) {
    for (int b = 0; b < 4; ++b) {
      const float sx = static_cast<float>(px) + (static_cast<float>(a) + 0.5f) * 0.25f - cx;
      const float sy = static_cast<float>(py) + (static_cast<float>(b) + 0.5f) * 0.25f - cy;
      if (sx * sx + sy * sy <= r * r) ++inside;
    }
  }
  return static_cast<float>(inside) / 16.0f;
}

render::Color withCoverage(render::Color c, float cov) {
  c.a *= cov;
  return c;
}

}  // namespace

void fillRoundedHorizontal(render::Painter& painter, const render::Rect& box, float radius, const ColourAt& colourAt) {
  const Snapped s = snap(box);
  if (!s.valid) return;
  const float r = clampRadius(radius, s.w, s.h);
  const int columns = static_cast<int>(s.w);
  const int rows = static_cast<int>(s.h);
  const int ri = static_cast<int>(std::ceil(r));
  for (int c = 0; c < columns; ++c) {
    const render::Color colour = colourAt((static_cast<float>(c) + 0.5f) / s.w);
    const float x = s.x + static_cast<float>(c);
    if (r <= 0.0f || (c >= ri && c + ri < columns)) {
      painter.fillRect({x, s.y, 1.0f, s.h}, colour);
      continue;
    }
    // End column: the rows between the corner zones in one rectangle, the corner pixels singly.
    if (rows > 2 * ri) painter.fillRect({x, s.y + static_cast<float>(ri), 1.0f, static_cast<float>(rows - 2 * ri)}, colour);
    for (int j = 0; j < rows; ++j) {
      if (j >= ri && j < rows - ri) continue;
      const float cov = coverage(c, j, s.w, s.h, r);
      if (cov > 0.0f) painter.fillRect({x, s.y + static_cast<float>(j), 1.0f, 1.0f}, withCoverage(colour, cov));
    }
  }
}

void fillRoundedVertical(render::Painter& painter, const render::Rect& box, float radius, const ColourAt& colourAt) {
  const Snapped s = snap(box);
  if (!s.valid) return;
  const float r = clampRadius(radius, s.w, s.h);
  const int columns = static_cast<int>(s.w);
  const int rows = static_cast<int>(s.h);
  const int ri = static_cast<int>(std::ceil(r));
  for (int j = 0; j < rows; ++j) {
    const render::Color colour = colourAt((static_cast<float>(j) + 0.5f) / s.h);
    const float y = s.y + static_cast<float>(j);
    if (r <= 0.0f || (j >= ri && j + ri < rows)) {
      painter.fillRect({s.x, y, s.w, 1.0f}, colour);
      continue;
    }
    if (columns > 2 * ri) painter.fillRect({s.x + static_cast<float>(ri), y, static_cast<float>(columns - 2 * ri), 1.0f}, colour);
    for (int c = 0; c < columns; ++c) {
      if (c >= ri && c < columns - ri) continue;
      const float cov = coverage(c, j, s.w, s.h, r);
      if (cov > 0.0f) painter.fillRect({s.x + static_cast<float>(c), y, 1.0f, 1.0f}, withCoverage(colour, cov));
    }
  }
}

void drawCheckerboard(const PaintContext& ctx, const render::Rect& box, float radius, double squareLogical, const render::Color& first,
                      const render::Color& second) {
  const Snapped s = snap(box);
  if (!s.valid) return;
  render::Painter& painter = ctx.painter();
  const float r = clampRadius(radius, s.w, s.h);
  const int square = std::max(1, static_cast<int>(std::lround(ctx.px(squareLogical))));
  const int columns = static_cast<int>(s.w);
  const int rows = static_cast<int>(s.h);
  const int ri = static_cast<int>(std::ceil(r));
  const auto colourFor = [&](int column, int row) { return ((column / square + row / square) % 2 == 0) ? first : second; };
  const int rowsOfSquares = (rows + square - 1) / square;
  // The end caps (the first and last ceil(r) columns) are drawn per pixel with the corner coverage;
  // the columns between them as whole squares, one rectangle per square.
  for (int c = 0; c < columns; ++c) {
    if (r > 0.0f && (c < ri || c + ri >= columns)) {
      for (int j = 0; j < rows; ++j) {
        const float cov = coverage(c, j, s.w, s.h, r);
        if (cov > 0.0f) painter.fillRect({s.x + static_cast<float>(c), s.y + static_cast<float>(j), 1.0f, 1.0f}, withCoverage(colourFor(c, j), cov));
      }
      continue;
    }
    const int runEnd = r > 0.0f ? columns - ri : columns;
    const int squareEnd = std::min(runEnd, (c / square + 1) * square);
    for (int row = 0; row < rowsOfSquares; ++row) {
      const int top = row * square;
      const int height = std::min(square, rows - top);
      painter.fillRect({s.x + static_cast<float>(c), s.y + static_cast<float>(top), static_cast<float>(squareEnd - c), static_cast<float>(height)},
                       colourFor(c, top));
    }
    c = squareEnd - 1;
  }
}

void drawSmallShadow(const PaintContext& ctx, const render::Rect& box, float radius) {
  const auto layers = ctx.ui().services().tokens().shadow("sm");
  if (!layers) return;
  const render::CornerRadii radii = render::CornerRadii::uniform(radius);
  for (auto it = layers->rbegin(); it != layers->rend(); ++it) {
    render::ShadowSpec spec;
    spec.offsetX = ctx.px(it->offsetX);
    spec.offsetY = ctx.px(it->offsetY);
    spec.blur = ctx.px(it->blur);
    spec.spread = ctx.px(it->spread);
    spec.color = ctx.color(it->color);
    ctx.painter().shadow(box, radii, spec);
  }
}

void drawRoundThumb(const PaintContext& ctx, float cx, float cy, double diameterLogical, double borderLogical, double ringLogical,
                    const render::Color& fill) {
  if (!std::isfinite(cx) || !std::isfinite(cy)) return;
  const float d = ctx.px(diameterLogical);
  const float b = ctx.px(borderLogical);
  const float ring = ctx.px(ringLogical);
  const render::Rect box{cx - d * 0.5f, cy - d * 0.5f, d, d};
  drawSmallShadow(ctx, box, d * 0.5f);
  const render::Rect outer{box.x - ring, box.y - ring, d + 2 * ring, d + 2 * ring};
  ctx.painter().fillRoundedRect(outer, render::CornerRadii::uniform(outer.w * 0.5f), render::Color{1, 1, 1, 1});
  const render::Rect inner{box.x + b, box.y + b, std::max(0.0f, d - 2 * b), std::max(0.0f, d - 2 * b)};
  ctx.painter().fillRoundedRect(inner, render::CornerRadii::uniform(inner.w * 0.5f), fill);
}

void drawPanelSurface(const PaintContext& ctx) {
  const render::Rect box = ctx.box();
  const theme::ResolvedStyle& panel = ctx.resolve("picker.panel", 0);
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(panel.radius));
  if (const auto layers = ctx.ui().services().tokens().shadow("xl")) {
    for (auto it = layers->rbegin(); it != layers->rend(); ++it) {
      render::ShadowSpec spec;
      spec.offsetX = ctx.px(it->offsetX);
      spec.offsetY = ctx.px(it->offsetY);
      spec.blur = ctx.px(it->blur);
      spec.spread = ctx.px(it->spread);
      spec.color = ctx.color(it->color);
      ctx.painter().shadow(box, radii, spec);
    }
  }
  ctx.fillBox(panel, box);
}

void drawSquareHandle(const PaintContext& ctx, const render::Rect& box, double radiusLogical, double borderLogical, const render::Color& border,
                      const render::Color& fill) {
  const float r = ctx.px(radiusLogical);
  const float b = ctx.px(borderLogical);
  drawSmallShadow(ctx, box, r);
  const render::CornerRadii outer = render::CornerRadii::uniform(r);
  ctx.painter().fillRoundedRect(box, outer, fill);
  ctx.painter().border(box, outer, b, border);
}

}  // namespace r1ui::widgets::pickerdraw
