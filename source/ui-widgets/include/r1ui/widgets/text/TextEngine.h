// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the shared text service: the Inter font family (Regular outlines with synthetic bold for
//   heavier weights), a bounded cache of shaped runs, a bounded cache of ellipsis fits, the CPU
//   glyph atlas, its coverage texture (only the dirty rectangle is uploaded) and the drawing of
//   shaped text as tinted coverage quads through the Painter.
// Why: layout needs text widths and drawing needs quads from the same shaper (what is measured is
//   what is drawn); the GPU texture must follow the atlas without re-uploading it every frame; one
//   engine is shared by every window (Services owns it).
// Callers: widgets (measure / fit / draw), UiContext (beginFrame, uploadAtlas), the preview, the
//   weight calibration test. Calls: ui-text (FontLibrary, shapeText, GlyphAtlas, buildGlyphQuads).
// Frame protocol: beginFrame() before the first draw of a frame, uploadAtlas() after the last draw
//   and before the target's endFrame. If the atlas ran out of room during a frame it is cleared
//   afterwards and consumeAtlasOverflow() reports that a repaint is needed.
// Units: physical pixels everywhere in this class (callers multiply logical sizes by the display
//   scale). Failure behavior: text that cannot be shaped measures as 0 wide and draws nothing.
// Weight: faces are Regular only. Every weight thickens the outline by defaultEmboldenPx(size) *
//   emboldenStrength(model, luminance of the tint, weight): a multiplier that is a smooth function of
//   the text colour's luminance and of the weight (see WeightModel). The constants are fitted against
//   the reference crops by tests/ui-widgets/text/text_calibration_visual_test.cpp
//   (Goal/evidence/P4_calibration_notes.md, docs/dev/widgets.md "Text weight").
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "r1ui/render/Painter.h"
#include "r1ui/text/Font.h"
#include "r1ui/text/GlyphAtlas.h"
#include "r1ui/text/GlyphQuads.h"
#include "r1ui/text/Shaper.h"
#include "r1ui/widgets/text/TextureFactory.h"

namespace r1ui::widgets {

// Multiplier of ui-text's default synthetic bold strength at the two ends of the text colour's
// luminance range: `dark` for black text, `light` for white text. Browsers render light text on a dark
// surface heavier and dark text on a light surface lighter than the plain outline, by an amount that
// grows with the text's brightness, so the strength between the ends is linear in the luminance. An
// end may be negative (the line is extrapolated); the strength actually used never is.
struct WeightAnchor {
  float dark;
  float light;
};
// Anchors at the weights the reference UI uses: 400 (regular), 500 (medium) and 600 (semibold, which
// the browser synthesises from Regular). Weights in between interpolate linearly; lighter than 400
// and heavier than 600 use the nearest anchor.
// There is no size term: the fit over 11, 12 and 14 px samples found a slope of 1% per px, below the
// noise of the measurement (see the calibration test), because the multiplier is applied to
// defaultEmboldenPx(size), which already scales with the size.
struct WeightModel {
  WeightAnchor regular;
  WeightAnchor medium;
  WeightAnchor bold;
};
// The fitted model (see the calibration notes).
inline constexpr WeightModel kCalibratedWeights{{-0.66f, 1.07f}, {-0.13f, 0.83f}, {0.83f, 2.31f}};
// No thickening at any weight, for tests that measure the bare rasteriser.
inline constexpr WeightModel kNoThickening{{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}};

// The multiplier for text of straight-sRGB `luminance` (0.2126 R + 0.7152 G + 0.0722 B, clamped to
// 0..1) and CSS `weight`. Never negative.
float emboldenStrength(const WeightModel& model, float luminance, int weight);

struct FittedText {
  std::string text;      // possibly shortened, ends with U+2026 when truncated
  float width = 0.0f;    // measured width in pixels
  bool truncated = false;
};

class TextEngine {
 public:
  // Throws std::runtime_error naming the font file when it is missing or damaged. The factory must
  // outlive the engine.
  TextEngine(TextureFactory& textures, const std::filesystem::path& fontDir);

  // Width of `utf8` in pixels at `pixelSize` (advances do not depend on the weight).
  // `tabular` shapes digits with the OpenType tnum feature (equal advances) like number fields do.
  float measure(std::string_view utf8, float pixelSize, int weight = 400, bool tabular = false);
  const r1ui::text::FontMetrics& metrics(float pixelSize);
  // Distance from the top of a line box of `boxHeight` pixels to the baseline of text at `pixelSize`,
  // computed the way the reference browser does (whole-pixel ascent and descent, floored half-leading).
  float baselineInBox(float pixelSize, float boxHeight);
  // `utf8` shortened with an ellipsis to fit `maxWidth` pixels (unchanged when it fits). The result
  // is cached; the reference stays valid until the next fit() call.
  const FittedText& fit(std::string_view utf8, float pixelSize, float maxWidth, bool tabular = false);

  void beginFrame();
  // Draws the text with its pen start at (penX, baselineY). Missing glyphs are skipped.
  void draw(r1ui::render::Painter& painter, std::string_view utf8, float pixelSize, int weight, float penX,
            float baselineY, const r1ui::render::Color& tint, bool tabular = false);
  void uploadAtlas();
  bool consumeAtlasOverflow();

  const WeightModel& weightModel() const { return model_; }
  // Throws std::invalid_argument for a negative, non-finite or absurd (> 8) strength; the previous
  // model stays in force then.
  void setWeightModel(const WeightModel& model);

  // The editor shapes with this font; the face for weight 400.
  const r1ui::text::Font& regular() const { return *regular_; }
  size_t cachedRuns() const { return runs_.size(); }
  // CPU atlas image (width * height R8), for calibration tests.
  const r1ui::text::GlyphAtlas& atlas() const { return atlas_; }

 private:
  struct Key {
    std::string text;
    float pixelSize;
    bool tabular;
    friend bool operator==(const Key&, const Key&) = default;
  };
  struct KeyHash {
    size_t operator()(const Key& k) const;
  };
  struct FitKey {
    std::string text;
    float pixelSize;
    int32_t widthQ;  // maxWidth in 1/64 px
    bool tabular;
    friend bool operator==(const FitKey&, const FitKey&) = default;
  };
  struct FitKeyHash {
    size_t operator()(const FitKey& k) const;
  };
  // Synthetic bold changes outlines, not advances, so runs are shared by all weights.
  const r1ui::text::ShapedRun* shaped(std::string_view utf8, float pixelSize, bool tabular);

  r1ui::text::FontLibrary library_;
  r1ui::text::FontHandle regular_;
  r1ui::text::GlyphAtlas atlas_;
  std::unique_ptr<AtlasTexture> texture_;
  std::unordered_map<Key, r1ui::text::ShapedRun, KeyHash> runs_;
  std::unordered_map<FitKey, FittedText, FitKeyHash> fits_;
  FittedText lastFit_;
  std::unordered_map<float, r1ui::text::FontMetrics> metrics_;
  std::vector<r1ui::text::GlyphQuad> quads_;
  std::vector<uint8_t> scratch_;
  WeightModel model_ = kCalibratedWeights;
  bool overflow_ = false;
};

}  // namespace r1ui::widgets
