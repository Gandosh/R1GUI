// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the preview's text pipeline glue: the Inter font family (Regular outlines with synthetic
//   bold for heavier weights), a bounded cache of shaped runs, the CPU glyph atlas, its R8 GPU
//   texture (only the dirty rectangle is uploaded) and the drawing of shaped text as tinted
//   coverage quads through the Painter.
// Why: layout needs text widths and drawing needs quads from the same shaper (what is measured is
//   what is drawn); the GPU texture must follow the atlas without re-uploading it every frame.
// Callers: Scene (measure/paint), field widgets, the dock sandbox and swatch/screen views, Bench.
//   Calls: ui-text (FontLibrary, shapeText, GlyphAtlas, buildGlyphQuads), ui-render (Texture).
// Frame protocol: beginFrame() before the first draw of a frame, uploadAtlas() after the last draw
//   and before RenderTarget::endFrame() (Texture::update rides the frame's command buffer, ordered
//   before the draws). If the atlas ran out of room during a frame it is cleared afterwards and
//   consumeAtlasOverflow() reports that a repaint is needed.
// Units: physical pixels everywhere in this class (callers multiply logical sizes by the display
//   scale). Failure behavior: text that cannot be shaped measures as 0 wide and draws nothing.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "r1ui/render/Painter.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/render/Texture.h"
#include "r1ui/text/Font.h"
#include "r1ui/text/GlyphAtlas.h"
#include "r1ui/text/GlyphQuads.h"
#include "r1ui/text/Shaper.h"

namespace preview {

class TextEngine {
 public:
  // Throws std::runtime_error naming the font file when it is missing or damaged.
  TextEngine(r1ui::render::RenderDevice& device, const std::filesystem::path& fontDir);

  // Width of `utf8` in pixels at `pixelSize` and `weight` (400 regular, 500+ synthetic bold).
  float measure(std::string_view utf8, float pixelSize, int weight);
  const r1ui::text::FontMetrics& metrics(float pixelSize);
  // Distance from the top of a line box of `boxHeight` pixels to the baseline of text at `pixelSize`.
  float baselineInBox(float pixelSize, float boxHeight);

  void beginFrame();
  // Draws the text with its pen start at (penX, baselineY). Missing glyphs are skipped.
  void draw(r1ui::render::Painter& painter, std::string_view utf8, float pixelSize, int weight, float penX,
            float baselineY, const r1ui::render::Color& tint);
  void uploadAtlas();
  bool consumeAtlasOverflow();

  // The editor shapes with this font; the face for weight 400.
  const r1ui::text::Font& regular() const { return *regular_; }
  size_t cachedRuns() const { return runs_.size(); }

 private:
  struct Key {
    std::string text;
    float pixelSize;
    friend bool operator==(const Key&, const Key&) = default;
  };
  struct KeyHash {
    size_t operator()(const Key& k) const;
  };
  // Synthetic bold changes outlines, not advances, so runs are shared by all weights.
  const r1ui::text::ShapedRun* shaped(std::string_view utf8, float pixelSize);

  r1ui::render::RenderDevice& device_;
  r1ui::text::FontLibrary library_;
  r1ui::text::FontFamily family_;
  r1ui::text::FontHandle regular_;
  r1ui::text::GlyphAtlas atlas_;
  std::unique_ptr<r1ui::render::Texture> texture_;
  std::unordered_map<Key, r1ui::text::ShapedRun, KeyHash> runs_;
  std::unordered_map<float, r1ui::text::FontMetrics> metrics_;
  std::vector<r1ui::text::GlyphQuad> quads_;
  std::vector<uint8_t> scratch_;
  bool overflow_ = false;
};

}  // namespace preview
