// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of TextEngine.h.
// Invariants: the shaped-run and fit caches are bounded (cleared when they reach their limit); the
//   atlas texture always has the atlas's dimensions; every registered AtlasConsumer holds the union of
//   the atlas regions changed since its last upload; glyph quads reference atlas pixels that were
//   uploaded no later than the frame that draws them.
// Callers: widgets, UiContext, the preview, calibration tests.
#include "r1ui/widgets/text/TextEngine.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>

#include "r1ui/core/CheckedCast.h"

namespace r1ui::widgets {

namespace {

constexpr size_t kMaxCachedFits = 2048;
constexpr int kRegularWeight = 400;
constexpr int kMediumWeight = 500;
constexpr int kBoldWeight = 600;
constexpr float kMaxBoldStrength = 8.0f;

float strengthAt(const WeightAnchor& anchor, float luminance) { return anchor.dark + (anchor.light - anchor.dark) * luminance; }

r1ui::text::GlyphAtlas makeAtlas() {
  auto atlas = r1ui::text::GlyphAtlas::create();
  if (!atlas.ok()) throw std::runtime_error("cannot create the glyph atlas: " + atlas.error().message);
  return std::move(atlas.value());
}

}  // namespace

size_t TextEngine::KeyHash::operator()(const KeyView& k) const {
  return std::hash<std::string_view>{}(k.text) ^ (std::hash<float>{}(k.pixelSize) * 0x9E3779B97F4A7C15ull) ^ (k.tabular ? 0x51ED270B1u : 0u);
}

size_t TextEngine::FitKeyHash::operator()(const FitKey& k) const {
  return std::hash<std::string>{}(k.text) ^ (std::hash<float>{}(k.pixelSize) * 0x9E3779B97F4A7C15ull) ^
         (std::hash<int32_t>{}(k.widthQ) * 0xC2B2AE3D27D4EB4Full) ^ (k.tabular ? 0x51ED270B1u : 0u);
}

TextEngine::TextEngine(TextureFactory& textures, const std::filesystem::path& fontDir) : atlas_(makeAtlas()) {
  if (!library_.ready()) throw std::runtime_error("the font library could not be initialised");
  const std::filesystem::path file = fontDir / "Inter-Regular.ttf";
  auto loaded = library_.loadFromFile(file.string(), kRegularWeight);
  if (!loaded.ok()) throw std::runtime_error("Cannot load the font " + file.string() + ": " + loaded.error().message);
  regular_ = loaded.value();
  texture_ = textures.createCoverage(r1ui::core::checkedCast<uint32_t>(atlas_.width()), r1ui::core::checkedCast<uint32_t>(atlas_.height()));
}

float emboldenStrength(const WeightModel& model, float luminance, int weight) {
  const float l = std::isnan(luminance) ? 0.0f : std::clamp(luminance, 0.0f, 1.0f);
  const float regular = strengthAt(model.regular, l);
  const float medium = strengthAt(model.medium, l);
  const float bold = strengthAt(model.bold, l);
  float strength = regular;
  if (weight >= kBoldWeight) {
    strength = bold;
  } else if (weight >= kMediumWeight) {
    strength = medium + (bold - medium) * static_cast<float>(weight - kMediumWeight) / static_cast<float>(kBoldWeight - kMediumWeight);
  } else if (weight > kRegularWeight) {
    strength = regular + (medium - regular) * static_cast<float>(weight - kRegularWeight) / static_cast<float>(kMediumWeight - kRegularWeight);
  }
  return std::max(strength, 0.0f);
}

void TextEngine::setWeightModel(const WeightModel& model) {
  for (const WeightAnchor& anchor : {model.regular, model.medium, model.bold}) {
    for (const float v : {anchor.dark, anchor.light}) {
      if (!std::isfinite(v) || std::abs(v) > kMaxBoldStrength) throw std::invalid_argument("text weight strength out of range");
    }
  }
  model_ = model;
}

const r1ui::text::ShapedRun* TextEngine::shaped(std::string_view utf8, float pixelSize, bool tabular) {
  if (!r1ui::text::isValidPixelSize(pixelSize)) return nullptr;
  const auto found = runs_.find(KeyView{utf8, pixelSize, tabular});
  if (found != runs_.end()) return &found->second;
  r1ui::text::ShapeOptions options;
  options.tabularNumbers = tabular;
  auto run = r1ui::text::shapeText(*regular_, pixelSize, utf8, options);
  if (!run.ok()) return nullptr;
  if (runs_.size() >= maxRuns_ || runBytes_ + utf8.size() > maxRunBytes_) {
    runs_.clear();
    runBytes_ = 0;
  }
  runBytes_ += utf8.size();
  return &runs_.emplace(Key{std::string(utf8), pixelSize, tabular}, std::move(run.value())).first->second;
}

void TextEngine::setRunCacheLimits(size_t maxEntries, size_t maxTextBytes) {
  maxRuns_ = std::max<size_t>(maxEntries, 1);
  maxRunBytes_ = std::max<size_t>(maxTextBytes, 1);  // a few huge strings must not pin megabytes
  if (runs_.size() > maxRuns_ || runBytes_ > maxRunBytes_) {
    runs_.clear();
    runBytes_ = 0;
  }
}

float TextEngine::measureTransient(std::string_view utf8, float pixelSize, bool tabular) {
  if (!r1ui::text::isValidPixelSize(pixelSize)) return 0.0f;
  const auto found = runs_.find(KeyView{utf8, pixelSize, tabular});
  if (found != runs_.end()) return found->second.width;
  r1ui::text::ShapeOptions options;
  options.tabularNumbers = tabular;
  const auto run = r1ui::text::shapeText(*regular_, pixelSize, utf8, options);
  return run.ok() ? run.value().width : 0.0f;
}

float TextEngine::measure(std::string_view utf8, float pixelSize, int, bool tabular) {
  const r1ui::text::ShapedRun* run = shaped(utf8, pixelSize, tabular);
  return run != nullptr ? run->width : 0.0f;
}

const r1ui::text::FontMetrics& TextEngine::metrics(float pixelSize) {
  const auto found = metrics_.find(pixelSize);
  if (found != metrics_.end()) return found->second;
  const float safe = r1ui::text::isValidPixelSize(pixelSize) ? pixelSize : 12.0f;
  return metrics_.emplace(pixelSize, regular_->metricsAt(safe)).first->second;
}

float TextEngine::baselineInBox(float pixelSize, float boxHeight) {
  // The browser rounds ascent and descent to whole pixels and floors the half-leading, which puts
  // the baseline of 11 px text in an 11 px line one pixel higher than the unrounded CSS formula.
  const r1ui::text::FontMetrics& m = metrics(pixelSize);
  const float ascent = std::round(m.ascent);
  const float descent = std::round(m.descent);
  return ascent + std::floor((boxHeight - (ascent + descent)) * 0.5f);
}

const FittedText& TextEngine::fit(std::string_view utf8, float pixelSize, float maxWidth, bool tabular) {
  const float width = measure(utf8, pixelSize, kRegularWeight, tabular);
  if (!(maxWidth >= 0.0f) || width <= maxWidth) {
    lastFit_ = {std::string(utf8), width, false};
    return lastFit_;
  }
  // Quantised so sub-pixel jitter of a layout width does not defeat the cache.
  const int32_t widthQ = static_cast<int32_t>(std::min(maxWidth, 1.0e6f) * 64.0f);
  FitKey key{std::string(utf8), pixelSize, widthQ, tabular};
  const auto found = fits_.find(key);
  if (found != fits_.end()) return found->second;
  r1ui::text::ShapeOptions options;
  options.tabularNumbers = tabular;
  auto result = r1ui::text::truncateWithEllipsis(*regular_, pixelSize, utf8, maxWidth, options);
  FittedText fitted;
  if (result.ok()) fitted = {std::move(result.value().text), result.value().width, result.value().truncated};
  if (fits_.size() >= kMaxCachedFits) fits_.clear();
  return fits_.emplace(std::move(key), std::move(fitted)).first->second;
}

// ---- atlas consumers ----------------------------------------------------------------------------

AtlasConsumer::AtlasConsumer(TextEngine& engine) : engine_(engine) {
  // A window created after glyphs were rasterised has uploaded none of them: its first upload carries
  // the whole atlas. (Before the first glyph the atlas image is all zero, which is the texture's state.)
  if (engine.atlas_.stats().entries > 0) pending_ = r1ui::text::DirtyRect{0, 0, engine.atlas_.width(), engine.atlas_.height()};
  engine_.consumers_.push_back(this);
}

AtlasConsumer::~AtlasConsumer() {
  auto& list = engine_.consumers_;
  list.erase(std::remove(list.begin(), list.end(), this), list.end());
}

void TextEngine::beginFrame() { beginFrame(defaultConsumer_); }

void TextEngine::beginFrame(AtlasConsumer& consumer) {
  atlas_.beginFrame();
  // The eviction pins belong to the engine: a frame that begins while another consumer's frame is still
  // open may evict glyphs that frame's quads use. Both are marked and paint again.
  for (AtlasConsumer* other : consumers_) {
    if (other != &consumer && other->inFlight_) {
      other->tainted_ = true;
      consumer.tainted_ = true;
    }
  }
  consumer.inFlight_ = true;
  consumer.epochSeen_ = clearEpoch_;
}

// Merges what the atlas changed since the last call into the pending region of every consumer.
void TextEngine::distributeDirty() {
  const std::optional<r1ui::text::DirtyRect> dirty = atlas_.takeDirtyRect();
  if (!dirty) return;
  for (AtlasConsumer* c : consumers_) {
    if (!c->pending_) {
      c->pending_ = dirty;
      continue;
    }
    const int x0 = std::min(c->pending_->x, dirty->x);
    const int y0 = std::min(c->pending_->y, dirty->y);
    const int x1 = std::max(c->pending_->x + c->pending_->w, dirty->x + dirty->w);
    const int y1 = std::max(c->pending_->y + c->pending_->h, dirty->y + dirty->h);
    c->pending_ = r1ui::text::DirtyRect{x0, y0, x1 - x0, y1 - y0};
  }
}

void TextEngine::draw(r1ui::render::Painter& painter, std::string_view utf8, float pixelSize, int weight, float penX,
                      float baselineY, const r1ui::render::Color& tint, bool tabular) {
  if (utf8.empty()) return;
  const r1ui::text::ShapedRun* run = shaped(utf8, pixelSize, tabular);
  if (run == nullptr) return;
  quads_.clear();
  r1ui::text::QuadParams params;
  params.pixelSize = pixelSize;
  // The strength follows the luminance of the straight sRGB tint (bright text on a dark surface is
  // rendered heavier than dark text on a light one) and the weight.
  const float luminance = 0.2126f * tint.r + 0.7152f * tint.g + 0.0722f * tint.b;
  params.emboldenPx = r1ui::text::defaultEmboldenPx(pixelSize) * emboldenStrength(model_, luminance, weight);
  const auto stats = r1ui::text::buildGlyphQuads(atlas_, *regular_, *run, params, penX, baselineY, quads_);
  if (!stats.ok()) return;
  if (stats.value().atlasFull) overflow_ = true;
  const r1ui::render::TextureRef ref = texture_->ref();
  for (const r1ui::text::GlyphQuad& q : quads_) {
    painter.drawTexture(ref, {q.x, q.y, q.w, q.h}, {q.u0, q.v0, q.u1 - q.u0, q.v1 - q.v0}, tint);
  }
}

void TextEngine::uploadAtlas() { uploadAtlas(defaultConsumer_); }

void TextEngine::uploadAtlas(AtlasConsumer& consumer) {
  distributeDirty();
  if (!consumer.pending_) return;
  const r1ui::text::DirtyRect dirty = *consumer.pending_;
  consumer.pending_.reset();
  const auto width = r1ui::core::checkedCast<size_t>(dirty.w);
  const auto height = r1ui::core::checkedCast<size_t>(dirty.h);
  const auto stride = r1ui::core::checkedCast<size_t>(atlas_.width());
  scratch_.resize(width * height);
  const uint8_t* source = atlas_.pixels();
  for (size_t row = 0; row < height; ++row) {
    const size_t from = (r1ui::core::checkedCast<size_t>(dirty.y) + row) * stride + r1ui::core::checkedCast<size_t>(dirty.x);
    std::copy_n(source + from, width, scratch_.begin() + static_cast<std::ptrdiff_t>(row * width));
  }
  texture_->update(r1ui::core::checkedCast<uint32_t>(dirty.x), r1ui::core::checkedCast<uint32_t>(dirty.y),
                   r1ui::core::checkedCast<uint32_t>(dirty.w), r1ui::core::checkedCast<uint32_t>(dirty.h), scratch_);
}

bool TextEngine::consumeAtlasOverflow() { return consumeAtlasOverflow(defaultConsumer_); }

bool TextEngine::consumeAtlasOverflow(AtlasConsumer& consumer) {
  if (overflow_) {
    overflow_ = false;
    atlas_.clear();
    ++clearEpoch_;
    distributeDirty();  // every consumer must upload the cleared image before its next frame
  }
  const bool repaint = consumer.tainted_ || consumer.epochSeen_ != clearEpoch_;
  consumer.tainted_ = false;
  consumer.inFlight_ = false;
  consumer.epochSeen_ = clearEpoch_;
  return repaint;
}

}  // namespace r1ui::widgets
