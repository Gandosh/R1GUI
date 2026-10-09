// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the shared text service: the Inter font family (Regular outlines with synthetic bold for
//   heavier weights), a bounded cache of shaped runs, a bounded cache of ellipsis fits, the CPU
//   glyph atlas, its coverage texture (only the dirty rectangle is uploaded) and the drawing of
//   shaped text as tinted coverage quads through the Painter. The run cache is bounded by entry count
//   and by bytes of text, and is cleared as a whole when either bound is reached.
// Why: layout needs text widths and drawing needs quads from the same shaper (what is measured is
//   what is drawn); the GPU texture must follow the atlas without re-uploading it every frame; one
//   engine is shared by every window (Services owns it).
// Callers: widgets (measure / fit / draw), UiContext (beginFrame, uploadAtlas), the preview, the
//   weight calibration test. Calls: ui-text (FontLibrary, shapeText, GlyphAtlas, buildGlyphQuads).
// Frame protocol: beginFrame(consumer) before the first draw of a frame, uploadAtlas(consumer) after
//   the last draw and before the target's endFrame, consumeAtlasOverflow(consumer) after it. If the
//   atlas ran out of room during a frame it is cleared afterwards and consumeAtlasOverflow() reports
//   that a repaint is needed.
// Consumers (multi-window): one AtlasConsumer per window (or per UiContext) tracks what that window
//   has not uploaded yet, so a window that finishes its frame first does not consume the dirty region
//   of the others; each consumer also learns whether the atlas was cleared, or another window's frame
//   was interleaved with its own (the shared eviction pins are per engine, not per window), during
//   its own frame and must then paint again. The overloads without a consumer use one built-in
//   consumer, for single-window code and tests.
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
#include <optional>
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

class TextEngine;

// What one window needs to know about the shared atlas between its beginFrame and its next frame.
// Register by construction; it must be destroyed before the engine.
class AtlasConsumer {
 public:
  explicit AtlasConsumer(TextEngine& engine);
  ~AtlasConsumer();
  AtlasConsumer(const AtlasConsumer&) = delete;
  AtlasConsumer& operator=(const AtlasConsumer&) = delete;

 private:
  friend class TextEngine;
  TextEngine& engine_;
  std::optional<r1ui::text::DirtyRect> pending_;  // changed since this consumer last uploaded
  bool inFlight_ = false;                         // between beginFrame and consumeAtlasOverflow
  bool tainted_ = false;                          // another consumer's frame overlapped this one
  uint64_t epochSeen_ = 0;                        // TextEngine::clearEpoch_ at the start of the frame
};

class TextEngine {
 public:
  // Throws std::runtime_error naming the font file when it is missing or damaged. The factory must
  // outlive the engine.
  TextEngine(TextureFactory& textures, const std::filesystem::path& fontDir);

  // Width of `utf8` in pixels at `pixelSize` (advances do not depend on the weight).
  // `tabular` shapes digits with the OpenType tnum feature (equal advances) like number fields do.
  float measure(std::string_view utf8, float pixelSize, int weight = 400, bool tabular = false);
  // Like measure(), but a run that is not cached already is shaped without being cached: for probing
  // many throw-away substrings (bisection while wrapping) that would otherwise evict the runs that are drawn.
  float measureTransient(std::string_view utf8, float pixelSize, bool tabular = false);
  const r1ui::text::FontMetrics& metrics(float pixelSize);
  // Distance from the top of a line box of `boxHeight` pixels to the baseline of text at `pixelSize`,
  // computed the way the reference browser does (whole-pixel ascent and descent, floored half-leading).
  float baselineInBox(float pixelSize, float boxHeight);
  // `utf8` shortened with an ellipsis to fit `maxWidth` pixels (unchanged when it fits). The result
  // is cached; the reference stays valid until the next fit() call.
  const FittedText& fit(std::string_view utf8, float pixelSize, float maxWidth, bool tabular = false);

  void beginFrame();
  void beginFrame(AtlasConsumer& consumer);
  // Draws the text with its pen start at (penX, baselineY). Missing glyphs are skipped.
  void draw(r1ui::render::Painter& painter, std::string_view utf8, float pixelSize, int weight, float penX,
            float baselineY, const r1ui::render::Color& tint, bool tabular = false);
  // Uploads what `consumer` has not uploaded yet to the atlas texture.
  void uploadAtlas();
  void uploadAtlas(AtlasConsumer& consumer);
  // Ends the consumer's frame. If the atlas was full during any frame it is cleared now. True when
  // this consumer's frame must be painted again: the atlas was cleared, or another consumer's frame
  // overlapped with it, since its beginFrame.
  bool consumeAtlasOverflow();
  bool consumeAtlasOverflow(AtlasConsumer& consumer);

  const WeightModel& weightModel() const { return model_; }
  // Throws std::invalid_argument for a negative, non-finite or absurd (> 8) strength; the previous
  // model stays in force then.
  void setWeightModel(const WeightModel& model);

  // The editor shapes with this font; the face for weight 400.
  const r1ui::text::Font& regular() const { return *regular_; }
  size_t cachedRuns() const { return runs_.size(); }
  // Bounds of the shaped-run cache (defaults 4096 entries, 4 MiB of text); the cache is cleared when it
  // holds more than the new bounds allow. Zero values are replaced by 1.
  void setRunCacheLimits(size_t maxEntries, size_t maxTextBytes);
  // CPU atlas image (width * height R8), for calibration tests.
  const r1ui::text::GlyphAtlas& atlas() const { return atlas_; }

 private:
  struct Key {
    std::string text;
    float pixelSize;
    bool tabular;
  };
  // The same key without owning the text, so a cache hit does not allocate a std::string.
  struct KeyView {
    std::string_view text;
    float pixelSize;
    bool tabular;
  };
  struct KeyHash {
    using is_transparent = void;
    size_t operator()(const Key& k) const { return (*this)(KeyView{k.text, k.pixelSize, k.tabular}); }
    size_t operator()(const KeyView& k) const;
  };
  struct KeyEqual {
    using is_transparent = void;
    bool operator()(const Key& a, const Key& b) const { return (*this)(KeyView{a.text, a.pixelSize, a.tabular}, b); }
    bool operator()(const Key& a, const KeyView& b) const { return (*this)(KeyView{a.text, a.pixelSize, a.tabular}, b); }
    bool operator()(const KeyView& a, const Key& b) const { return (*this)(a, KeyView{b.text, b.pixelSize, b.tabular}); }
    bool operator()(const KeyView& a, const KeyView& b) const { return a.text == b.text && a.pixelSize == b.pixelSize && a.tabular == b.tabular; }
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
  std::unordered_map<Key, r1ui::text::ShapedRun, KeyHash, KeyEqual> runs_;
  size_t runBytes_ = 0;  // text bytes held by runs_ (bounded together with the entry count)
  size_t maxRuns_ = 4096;
  size_t maxRunBytes_ = size_t{4} << 20;
  std::unordered_map<FitKey, FittedText, FitKeyHash> fits_;
  FittedText lastFit_;
  std::unordered_map<float, r1ui::text::FontMetrics> metrics_;
  std::vector<r1ui::text::GlyphQuad> quads_;
  std::vector<uint8_t> scratch_;
  WeightModel model_ = kCalibratedWeights;
  bool overflow_ = false;
  uint64_t clearEpoch_ = 0;                   // counts atlas clears
  std::vector<AtlasConsumer*> consumers_;     // registered in AtlasConsumer's constructor
  AtlasConsumer defaultConsumer_{*this};      // for the overloads without a consumer
  friend class AtlasConsumer;
  void distributeDirty();
};

}  // namespace r1ui::widgets
