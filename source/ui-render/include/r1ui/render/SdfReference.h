// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the CPU oracle of the shader math (signed distance of a rounded box, 1 px anti-aliased
//   coverage, border ring, gaussian box-shadow coverage, line coverage) and the CSS rule that
//   scales oversized corner radii.
// Why: shaders/sdf.frag cannot be unit tested on a CPU; this double-precision twin defines the
//   intended result, lets the fast tier check the formulas, and gives the GPU tests an analytic
//   reference. The Painter also uses normalizeRadii so CPU and GPU agree on the geometry.
// Callers: paint/Painter.cpp (normalizeRadii), tests/ui-render. Pure functions, no state.
// Pixel convention: a pixel (ix, iy) is sampled at its centre (ix + 0.5, iy + 0.5).
#pragma once

#include "r1ui/render/ClipStack.h"
#include "r1ui/render/Painter.h"

namespace r1ui::render::reference {

// CSS corner scaling: when two radii on one side sum to more than the side, every radius is
// multiplied by the smallest such ratio. Negative radii become 0. Rect size must be positive.
CornerRadii normalizeRadii(const Rect& rect, const CornerRadii& radii);

// Signed distance in pixels from point (px, py) to the rounded box (negative inside).
double signedDistance(double px, double py, const Rect& rect, const CornerRadii& radii);

// Coverage of a pixel whose centre has signed distance d: a 1 px wide linear ramp centred on the
// edge, so a pixel-aligned edge gives exactly 0 or 1.
double coverageFromDistance(double d);

double fillCoverage(double px, double py, const Rect& rect, const CornerRadii& radii);

// Ring of `width` pixels inside the outer box (radii shrink by `width`, never below 0).
double borderCoverage(double px, double py, const Rect& outer, const CornerRadii& radii, double width);

// Coverage of a gaussian-blurred rounded box (shadow body, before the knock-out of the source box
// and before colour alpha). Evaluated by dense numeric integration; sigma <= 0 degenerates to
// fillCoverage. This is what the shader approximates with 16 samples.
double blurredBoxCoverage(double px, double py, const Rect& rect, const CornerRadii& radii, double sigma);

// Butt-capped segment of the given width.
double lineCoverage(double px, double py, double x0, double y0, double x1, double y1, double width);

}  // namespace r1ui::render::reference
