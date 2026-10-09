// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PickerDraw.h.
// Invariants: rectangles are snapped to whole physical pixels first; the vertical (or horizontal)
//   extent of an end column follows the exact circle of the corner radius evaluated at the pixel
//   centre, so a radius of half the height gives a true semicircular end.
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

// How far the rounded outline is pulled in at distance `centre` from one end of a span of `length`
// whose corners have radius `r`.
float cornerInset(float centre, float length, float r) {
  if (r <= 0.0f) return 0.0f;
  float d = 0.0f;
  if (centre < r) d = r - centre;
  else if (centre > length - r) d = centre - (length - r);
  else return 0.0f;
  return r - std::sqrt(std::max(0.0f, r * r - d * d));
}

float clampRadius(float radius, float w, float h) {
  if (!(radius > 0.0f)) return 0.0f;
  return std::min(radius, std::min(w, h) * 0.5f);
}

}  // namespace

void fillRoundedHorizontal(render::Painter& painter, const render::Rect& box, float radius, const ColourAt& colourAt) {
  const Snapped s = snap(box);
  if (!s.valid) return;
  const float r = clampRadius(radius, s.w, s.h);
  const int columns = static_cast<int>(s.w);
  for (int i = 0; i < columns; ++i) {
    const float centre = static_cast<float>(i) + 0.5f;
    const float inset = cornerInset(centre, s.w, r);
    painter.fillRect({s.x + static_cast<float>(i), s.y + inset, 1.0f, s.h - 2.0f * inset}, colourAt(centre / s.w));
  }
}

void fillRoundedVertical(render::Painter& painter, const render::Rect& box, float radius, const ColourAt& colourAt) {
  const Snapped s = snap(box);
  if (!s.valid) return;
  const float r = clampRadius(radius, s.w, s.h);
  const int rows = static_cast<int>(s.h);
  for (int i = 0; i < rows; ++i) {
    const float centre = static_cast<float>(i) + 0.5f;
    const float inset = cornerInset(centre, s.h, r);
    painter.fillRect({s.x + inset, s.y + static_cast<float>(i), s.w - 2.0f * inset, 1.0f}, colourAt(centre / s.h));
  }
}

void drawCheckerboard(const PaintContext& ctx, const render::Rect& box, float radius, double squareLogical, const render::Color& first,
                      const render::Color& second) {
  const Snapped s = snap(box);
  if (!s.valid) return;
  render::Painter& painter = ctx.painter();
  const float r = clampRadius(radius, s.w, s.h);
  const float square = std::max(1.0f, std::round(ctx.px(squareLogical)));
  // One rectangle per square would overdraw the rounded corners, so squares are drawn per pixel
  // column inside the two end caps and as whole squares (clipped to the box) in between.
  const int capColumns = static_cast<int>(std::ceil(r));
  const int total = static_cast<int>(s.w);
  const auto colourFor = [&](int column, int row) { return ((column / static_cast<int>(square) + row / static_cast<int>(square)) % 2 == 0) ? first : second; };
  const int rowsOfSquares = static_cast<int>(std::ceil(s.h / square));
  for (int c = 0; c < total; ++c) {
    const bool inCap = c < capColumns || c >= total - capColumns;
    if (!inCap) {
      // Interior: jump to the end of the interior run, drawing squares that span whole columns.
      const int runEnd = total - capColumns;
      const int squareEnd = std::min(runEnd, (c / static_cast<int>(square) + 1) * static_cast<int>(square));
      for (int row = 0; row < rowsOfSquares; ++row) {
        const float top = static_cast<float>(row) * square;
        const float height = std::min(square, s.h - top);
        painter.fillRect({s.x + static_cast<float>(c), s.y + top, static_cast<float>(squareEnd - c), height}, colourFor(c, row * static_cast<int>(square)));
      }
      c = squareEnd - 1;
      continue;
    }
    const float inset = cornerInset(static_cast<float>(c) + 0.5f, s.w, r);
    for (int row = 0; row < rowsOfSquares; ++row) {
      const float top = std::max(static_cast<float>(row) * square, inset);
      const float bottom = std::min(static_cast<float>(row + 1) * square, s.h - inset);
      if (bottom <= top) continue;
      painter.fillRect({s.x + static_cast<float>(c), s.y + top, 1.0f, bottom - top}, colourFor(c, row * static_cast<int>(square)));
    }
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
