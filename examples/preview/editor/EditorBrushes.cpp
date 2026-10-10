// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of EditorBrushes.h: the sample brush table and the procedural pictures.
// Invariants: ids are unique (the table is checked by the brush model when it is set and by the preview
//   tests); a picture is a pure function of (brush, edge): no state, no randomness, no files.
// Callers: EditorApp, tests/preview.
#include "EditorBrushes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace preview::editor {

namespace cb = r1ui::commands::brushes;
namespace th = r1ui::widgets::thumbs;

namespace {

struct Row {
  const char* name;
  const char* category;
  const char* description;
  const char* icon;
};

// Generic sculpt-style names in six categories. Several share first letters on purpose (S, C, M, P) so the
// second letter of the quick access matters.
constexpr Row kRows[] = {
    {"Standard", "Sculpt", "Pushes the surface in or out along its normal", "palette"},
    {"Clay", "Sculpt", "Builds material up in smooth, rounded layers", "layers"},
    {"Clay Buildup", "Sculpt", "Builds material up in short, sharper strokes", "layers"},
    {"Clay Tubes", "Sculpt", "Lays down tube-shaped ribbons of material", "layers"},
    {"Inflate", "Sculpt", "Moves points outward along their own normals", "circle"},
    {"Blob", "Sculpt", "Swells the surface into a soft, rounded mass", "circle"},
    {"Pinch", "Sculpt", "Pulls points toward the centre of the stroke", "pen-tool"},
    {"Crease", "Sculpt", "Cuts a sharp, narrow groove into the surface", "pen-tool"},
    {"Dam Standard", "Sculpt", "Draws a thin, deep line with a raised edge", "pen-tool"},
    {"Magnify", "Sculpt", "Pushes points away from the centre of the stroke", "circle"},
    {"Stamp", "Sculpt", "Presses a repeated shape into the surface", "shapes"},
    {"Orb Cracks", "Sculpt", "Breaks the surface into a network of fine cracks", "shapes"},
    {"Smooth", "Smooth", "Averages neighbouring points to remove roughness", "droplets"},
    {"Smooth Stronger", "Smooth", "Smooths harder and flattens small bumps", "droplets"},
    {"Relax", "Smooth", "Evens out the spacing of points without changing the shape", "droplets"},
    {"Polish", "Smooth", "Gives the surface a clean, glossy finish", "sparkles"},
    {"Hpolish", "Smooth", "Polishes hard edges while keeping flat areas flat", "sparkles"},
    {"Soften", "Smooth", "Rounds off sharp ridges and creases", "droplets"},
    {"Move", "Move", "Drags the points under the cursor along the stroke", "move-3d"},
    {"Snake Hook", "Move", "Pulls out long tubes that follow the stroke", "move-3d"},
    {"Elastic Grab", "Move", "Moves a large area with a natural falloff", "hand"},
    {"Nudge", "Move", "Slides the surface sideways along the stroke", "hand"},
    {"Pull", "Move", "Pulls points toward the camera", "move-vertical"},
    {"Push", "Move", "Pushes points away from the camera", "move-vertical"},
    {"Twist", "Move", "Rotates the points around the centre of the stroke", "rotate-cw"},
    {"Mask Pen", "Mask", "Paints a mask that protects the surface", "pen-tool"},
    {"Mask Lasso", "Mask", "Masks everything inside a drawn outline", "mouse-pointer"},
    {"Mask Blur", "Mask", "Softens the edge of an existing mask", "droplets"},
    {"Mask Clear", "Mask", "Removes the mask under the stroke", "x"},
    {"Flatten", "Surface", "Levels the surface to its average height", "square"},
    {"Planar", "Surface", "Flattens areas into clean planes", "square"},
    {"Layer", "Surface", "Raises the surface by a fixed amount", "layers"},
    {"Scrape", "Surface", "Shaves high points down to a plane", "square"},
    {"Surface Noise", "Surface", "Adds fine random detail to the surface", "sparkles"},
    {"Fill", "Surface", "Fills hollows up to the surrounding level", "paint-bucket"},
    {"Slash", "Cut", "Cuts a clean line across the surface", "scissors"},
    {"Slice", "Cut", "Slices off the part of the surface above a line", "scissors"},
    {"Trim", "Cut", "Trims the surface back to a drawn outline", "scissors"},
    {"Cut Curve", "Cut", "Cuts along a curve you draw", "scissors"},
    {"Peel", "Cut", "Lifts a thin skin from the surface", "scissors"},
};

// FNV-1a over the id: the look of a picture follows the brush, not its position in the list.
uint64_t hashOf(const std::string& text) {
  uint64_t h = 1469598103934665603ull;
  for (const char c : text) {
    h ^= static_cast<unsigned char>(c);
    h *= 1099511628211ull;
  }
  return h;
}

float smoothstep(float a, float b, float x) {
  const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

struct Ink {
  float r, g, b;
};

Ink inkOf(const std::string& category) {
  if (category == "Sculpt") return {0.95f, 0.62f, 0.24f};
  if (category == "Smooth") return {0.30f, 0.80f, 0.88f};
  if (category == "Move") return {0.42f, 0.85f, 0.45f};
  if (category == "Mask") return {0.70f, 0.50f, 0.95f};
  if (category == "Surface") return {0.40f, 0.62f, 0.98f};
  return {0.95f, 0.42f, 0.42f};  // Cut
}

float distanceToSegment(float px, float py, float ax, float ay, float bx, float by) {
  const float dx = bx - ax;
  const float dy = by - ay;
  const float t = std::clamp(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
  return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

// How much of the ink covers the point (u, v) in [-1, 1] for one of eight stamp shapes.
float coverage(unsigned style, float u, float v) {
  const float d = std::hypot(u, v);
  switch (style) {
    case 0: return std::exp(-d * d * 4.5f);                                                   // soft falloff
    case 1: return 1.0f - smoothstep(0.52f, 0.60f, d);                                        // hard disc
    case 2: return smoothstep(0.42f, 0.50f, d) * (1.0f - smoothstep(0.62f, 0.70f, d));        // ring
    case 3: {                                                                                 // a 3 x 3 stamp of dots
      float c = 0.0f;
      for (const float cy : {-0.5f, 0.0f, 0.5f}) {
        for (const float cx : {-0.5f, 0.0f, 0.5f}) c = std::max(c, 1.0f - smoothstep(0.12f, 0.17f, std::hypot(u - cx, v - cy)));
      }
      return c;
    }
    case 4: return 1.0f - smoothstep(0.10f, 0.19f, distanceToSegment(u, v, -0.6f, 0.5f, 0.6f, -0.5f));  // a stroke
    case 5: {                                                                                 // hatching inside a disc
      const float phase = (u + v) * 3.2f;
      const float line = 1.0f - smoothstep(0.12f, 0.26f, std::fabs(phase - std::floor(phase) - 0.5f) * 2.0f);
      return line * (1.0f - smoothstep(0.62f, 0.70f, d));
    }
    case 6: return std::max(0.55f * std::exp(-d * d * 14.0f), smoothstep(0.50f, 0.56f, d) * (1.0f - smoothstep(0.66f, 0.72f, d)));  // dot in a ring
    default: {                                                                                // a wedge
      const float half = (0.62f - v) * 0.55f;
      const float inside = std::min(half - std::fabs(u), v + 0.62f);
      return smoothstep(0.0f, 0.07f, inside);
    }
  }
}

}  // namespace

std::vector<cb::BrushInfo> sampleBrushes() {
  std::vector<cb::BrushInfo> list;
  for (const Row& row : kRows) {
    cb::BrushInfo info;
    info.name = row.name;
    for (const char* c = row.name; *c != 0; ++c) info.id += *c == ' ' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
    info.category = row.category;
    info.description = row.description;
    info.icon = row.icon;
    list.push_back(std::move(info));
  }
  return list;
}

th::ThumbnailStatus SampleBrushThumbnails::produce(uint64_t itemKey, uint32_t sizePx, th::ThumbnailImage& out) {
  const auto index = model_.indexOfThumbnailKey(itemKey);
  if (!index) return th::ThumbnailStatus::Failed;
  const cb::BrushInfo& brush = model_.brushes()[*index];
  const uint32_t edge = std::clamp<uint32_t>(sizePx, 32, 256);
  const unsigned style = static_cast<unsigned>(hashOf(brush.id) % 8);
  const Ink ink = inkOf(brush.category);
  out.width = edge;
  out.height = edge;
  out.rgba.assign(static_cast<size_t>(edge) * edge * 4, 255);
  for (uint32_t y = 0; y < edge; ++y) {
    for (uint32_t x = 0; x < edge; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(edge) * 2.0f - 1.0f;
      const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(edge) * 2.0f - 1.0f;
      const float shade = 0.16f + 0.05f * (0.5f - v * 0.5f);  // a quiet vertical gradient behind the stamp
      const float c = std::clamp(coverage(style, u, v), 0.0f, 1.0f);
      const float lit = 0.72f + 0.28f * (0.5f - 0.5f * (u * 0.6f + v * 0.8f));
      uint8_t* p = &out.rgba[(static_cast<size_t>(y) * edge + x) * 4];
      p[0] = static_cast<uint8_t>(255.0f * std::clamp(shade * (1.0f - c) + ink.r * lit * c, 0.0f, 1.0f));
      p[1] = static_cast<uint8_t>(255.0f * std::clamp(shade * (1.0f - c) + ink.g * lit * c, 0.0f, 1.0f));
      p[2] = static_cast<uint8_t>(255.0f * std::clamp((shade + 0.03f) * (1.0f - c) + ink.b * lit * c, 0.0f, 1.0f));
    }
  }
  return th::ThumbnailStatus::Ready;
}

}  // namespace preview::editor
