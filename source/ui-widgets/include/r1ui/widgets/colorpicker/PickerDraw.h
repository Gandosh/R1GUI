// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the drawing helpers shared by the colour picker and the gradient editor: gradients drawn as
//   one-pixel strips clipped to a rounded rectangle, a checkerboard under translucent colour, the
//   round slider thumb / stop handle with its white border and the small drop shadow.
// Why: the Painter has no gradient primitive and no rounded clip, but the measured controls (hue and
//   alpha tracks with radius 6, the saturation/value square and the gradient bar with radius 4) need
//   both. Strips are exact (one colour per physical pixel column or row) and cost about one rectangle
//   per pixel of width, which is small for these controls.
// Callers: SvSquare, ColorSlider, GradientBar, swatches. Calls: render::Painter, PaintContext.
// Units: every rectangle, radius and size in this header is PHYSICAL pixels (the caller converts
//   with PaintContext::px) except where a parameter says logical. Rectangles are snapped to whole
//   pixels so adjacent strips never leave seams.
// Failure behavior: degenerate (empty, NaN) rectangles draw nothing.
#pragma once

#include <functional>

#include "r1ui/render/Painter.h"
#include "r1ui/widgets/runtime/PaintContext.h"

namespace r1ui::widgets::pickerdraw {

// Colour at position t in 0..1 (centre of the pixel along the gradient axis).
using ColourAt = std::function<render::Color(float t)>;

// Fills `box` (rounded corners of `radius`) with a gradient that varies along x.
void fillRoundedHorizontal(render::Painter& painter, const render::Rect& box, float radius, const ColourAt& colourAt);
// Same along y (t = 0 at the top). Used for the black overlay of the saturation/value square.
void fillRoundedVertical(render::Painter& painter, const render::Rect& box, float radius, const ColourAt& colourAt);

// Draws a checkerboard of squares of `squareLogical` logical pixels (two squares per period) inside
// `box` with rounded corners; the square at the top-left corner has colour `first`.
void drawCheckerboard(const PaintContext& ctx, const render::Rect& box, float radius, double squareLogical,
                      const render::Color& first, const render::Color& second);

// The `shadow.sm` token drawn under a box.
void drawSmallShadow(const PaintContext& ctx, const render::Rect& box, float radius);

// A slider thumb: circle of `diameterLogical` px (border box) with a white border of `borderLogical`
// px, an extra white ring of `ringLogical` px outside it (the measured box-shadow `0 0 0 1px white`),
// filled with `fill`, with the small shadow. `centre` in physical pixels.
void drawRoundThumb(const PaintContext& ctx, float centreX, float centreY, double diameterLogical, double borderLogical,
                    double ringLogical, const render::Color& fill);

// The popover surface of the pickers: the xl shadow token, then the `picker.panel` row (fill `panel`,
// 1 px border `border`, radius 8) over the widget's whole box. The row is registered by ColorPicker.
void drawPanelSurface(const PaintContext& ctx);

// A square stop handle (radius from the measured 4 px) with border colour `border`.
void drawSquareHandle(const PaintContext& ctx, const render::Rect& box, double radiusLogical, double borderLogical,
                      const render::Color& border, const render::Color& fill);

}  // namespace r1ui::widgets::pickerdraw
