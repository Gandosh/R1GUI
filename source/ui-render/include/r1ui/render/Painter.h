// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the 2D drawing API of the toolkit (rects, rounded rects with per-corner radii, borders,
//   CSS-style drop shadows, lines, textured quads, clip and opacity stacks) and the batching of
//   those calls into a PaintList.
// Why: widgets draw through one small, OS- and Vulkan-free interface; the backend only has to
//   execute the resulting PaintList. Everything here is plain CPU code and is unit tested without
//   a GPU.
// Callers: ui-widgets/ui-dock/preview through RenderTarget::painter(); tests construct a Painter
//   directly. Produces: PaintList (PaintList.h) consumed by vulkan/FrameRecorder.cpp.
//
// Coordinates and DPI: all values are physical pixels, origin top-left, y down. There is no hidden
//   scaling; callers that think in logical units multiply with DisplayScale explicitly.
// Colour: Color is straight (non-premultiplied) sRGB-encoded RGBA in 0..1, exactly the values in
//   the design tokens; blending happens in that encoded space (see RenderDevice.h, "Colour").
// Hostile numeric input (NaN/inf in any argument) never reaches the GPU: the draw is skipped and
//   counted in PaintStats::rejected. Empty or fully clipped draws are skipped and counted in
//   PaintStats::culled. API misuse (unbalanced stacks, invalid texture, drawing outside a
//   begin()/end() pair) throws std::logic_error / std::invalid_argument. Exceeding the instance
//   limit never throws: further instances are dropped and counted in PaintStats::dropped, so one
//   huge frame (a very long text view) degrades instead of ending the application.
// Draw order: later calls paint over earlier ones. For a CSS shadow list the caller issues the
//   shadows from last to first so the first listed shadow ends up on top, as in browsers.
#pragma once

#include <cstdint>
#include <vector>

#include "r1ui/render/ClipStack.h"
#include "r1ui/render/PaintList.h"

namespace r1ui::render {

// Cap on instances (shapes plus textured quads, one per glyph) in one frame: a screen full of
// small text needs well over 65536 glyph quads. The GPU ring buffers and the CPU lists grow on
// demand, so a normal frame stays small; this bound only limits the worst case to
// 2^20 * (96 + 64) bytes = 160 MiB per frame slot. Instances past it are dropped and counted.
inline constexpr uint32_t kMaxInstancesPerFrame = 1u << 20;

struct Color {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;

  static Color fromRgba8(uint8_t r8, uint8_t g8, uint8_t b8, uint8_t a8 = 255) {
    return {r8 / 255.0f, g8 / 255.0f, b8 / 255.0f, a8 / 255.0f};
  }
  // 0xRRGGBBAA, the order of the "#rrggbbaa" strings in tokens.json.
  static Color fromHex(uint32_t rrggbbaa) {
    return fromRgba8(static_cast<uint8_t>(rrggbbaa >> 24), static_cast<uint8_t>(rrggbbaa >> 16),
                     static_cast<uint8_t>(rrggbbaa >> 8), static_cast<uint8_t>(rrggbbaa));
  }
};

// Per-corner radii in pixels, clockwise from the top-left.
struct CornerRadii {
  float topLeft = 0.0f;
  float topRight = 0.0f;
  float bottomRight = 0.0f;
  float bottomLeft = 0.0f;

  static CornerRadii uniform(float radius) { return {radius, radius, radius, radius}; }
};

// One CSS box-shadow layer: [offsetX, offsetY, blur, spread, colour] as in tokens.json. `blur` is
// the CSS blur radius (gaussian sigma = blur / 2). A negative spread shrinks the shadow.
struct ShadowSpec {
  float offsetX = 0.0f;
  float offsetY = 0.0f;
  float blur = 0.0f;
  float spread = 0.0f;
  Color color{0.0f, 0.0f, 0.0f, 1.0f};
};

// What kind of data a texture holds; decides how the tint combines with the samples.
enum class TextureKind : uint32_t {
  Coverage,  // single channel coverage (glyphs): output = tint * coverage
  Color      // RGBA, sRGB-encoded, straight alpha (images, icons): output = texel * tint
};

// Identifies a texture to the painter without exposing GPU types. id 0 is invalid.
struct TextureRef {
  uint64_t id = 0;
  TextureKind kind = TextureKind::Color;
  bool valid() const { return id != 0; }
};

// Maps logical (device-independent) units to physical pixels. The painter itself never applies it.
struct DisplayScale {
  float factor = 1.0f;
  float toPhysical(float logical) const { return logical * factor; }
  // Physical size rounded to whole pixels, at least 1 for a positive logical size.
  float snapped(float logical) const;
};

struct PaintStats {
  uint32_t drawCalls = 0;       // batches = GPU draw calls
  uint32_t instances = 0;       // sdf + textured instances
  uint32_t sdfInstances = 0;
  uint32_t texInstances = 0;
  uint32_t culled = 0;          // empty, fully clipped or invisible draws that were skipped
  uint32_t rejected = 0;        // draws skipped because an argument was NaN or infinite
  uint32_t dropped = 0;         // draws skipped because the frame reached its instance limit
};

class Painter {
 public:
  // `maxInstances` is clamped to kMaxInstancesPerFrame; smaller values exist for tests.
  explicit Painter(uint32_t maxInstances = kMaxInstancesPerFrame);

  // Starts a frame for a target of the given size and resets all state. A zero size is legal (every
  // draw is culled).
  void begin(uint32_t width, uint32_t height);
  // Ends the frame; throws std::logic_error if a clip or opacity push is still open.
  void end();

  void fillRect(const Rect& rect, const Color& color);
  // Radii larger than the box are scaled down uniformly (CSS rule); negative radii count as 0.
  void fillRoundedRect(const Rect& rect, const CornerRadii& radii, const Color& color);
  // Border of `width` pixels. inside = true keeps the outer edge at `rect`; false grows the
  // outer edge by `width` (outline-like). Width is limited to half the smaller side when inside.
  void border(const Rect& rect, const CornerRadii& radii, float width, const Color& color,
              bool inside = true);
  // Outer drop shadow of the box (rect, radii), never painted inside the box itself.
  void shadow(const Rect& rect, const CornerRadii& radii, const ShadowSpec& spec);
  // Straight segment with butt caps; coordinates are pixel-space (a 1 px horizontal line on row
  // y is drawn at y + 0.5).
  void line(float x0, float y0, float x1, float y1, float width, const Color& color);
  // Draws the part of the texture given by `uv` into `dst`, multiplied by `tint`. `uv` is a
  // rectangle in 0..1 texture space: x, y = (u0, v0) of the top-left corner, w, h = extent (u1 - u0,
  // v1 - v0), not the opposite corner.
  void drawTexture(const TextureRef& texture, const Rect& dst, const Rect& uv,
                   const Color& tint = {1.0f, 1.0f, 1.0f, 1.0f});

  // Clip stack: nested pushes intersect; the rectangle snaps to whole pixels (ClipStack.h).
  void pushClip(const Rect& rect);
  void popClip();
  // Opacity stack: pushes multiply into every following colour alpha. Value is clamped to 0..1;
  // NaN counts as 0 (draw nothing). The multiplication is per primitive, not per group: children
  // that overlap inside a faded group show through each other (there is no offscreen group layer).
  void pushOpacity(float opacity);
  void popOpacity();

  const PaintList& list() const { return list_; }
  PaintStats stats() const;

 private:
  void requireActive(const char* call) const;
  // Reserves a slot in the right instance array and in a (possibly extended) batch; nullptr (and
  // `dropped_` counted) when the frame is full.
  template <class Instance>
  Instance* append(BatchKind kind, const IRect& scissor, uint64_t textureId);
  bool visible(const Rect& bounds, IRect* scissor);

  PaintList list_;
  ClipStack clip_;
  std::vector<float> opacity_;
  uint32_t maxInstances_;
  uint32_t culled_ = 0;
  uint32_t rejected_ = 0;
  uint32_t dropped_ = 0;
  bool active_ = false;
};

}  // namespace r1ui::render
