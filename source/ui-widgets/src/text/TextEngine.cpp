// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of TextEngine.h.
// Invariants: the shaped-run and fit caches are bounded (cleared when they reach their limit); the
//   atlas texture always has the atlas's dimensions; glyph quads reference atlas pixels that were
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

constexpr size_t kMaxCachedRuns = 4096;
constexpr size_t kMaxCachedFits = 2048;
constexpr int kRegularWeight = 400;
constexpr float kMaxBoldStrength = 8.0f;

r1ui::text::GlyphAtlas makeAtlas() {
  auto atlas = r1ui::text::GlyphAtlas::create();
  if (!atlas.ok()) throw std::runtime_error("cannot create the glyph atlas: " + atlas.error().message);
  return std::move(atlas.value());
}

}  // namespace

size_t TextEngine::KeyHash::operator()(const Key& k) const {
  return std::hash<std::string>{}(k.text) ^ (std::hash<float>{}(k.pixelSize) * 0x9E3779B97F4A7C15ull);
}

size_t TextEngine::FitKeyHash::operator()(const FitKey& k) const {
  return std::hash<std::string>{}(k.text) ^ (std::hash<float>{}(k.pixelSize) * 0x9E3779B97F4A7C15ull) ^
         (std::hash<int32_t>{}(k.widthQ) * 0xC2B2AE3D27D4EB4Full);
}

TextEngine::TextEngine(TextureFactory& textures, const std::filesystem::path& fontDir) : atlas_(makeAtlas()) {
  if (!library_.ready()) throw std::runtime_error("the font library could not be initialised");
  const std::filesystem::path file = fontDir / "Inter-Regular.ttf";
  auto loaded = library_.loadFromFile(file.string(), kRegularWeight);
  if (!loaded.ok()) throw std::runtime_error("Cannot load the font " + file.string() + ": " + loaded.error().message);
  regular_ = loaded.value();
  texture_ = textures.createCoverage(r1ui::core::checkedCast<uint32_t>(atlas_.width()), r1ui::core::checkedCast<uint32_t>(atlas_.height()));
}

void TextEngine::setBoldStrength(float strength) {
  if (!std::isfinite(strength) || strength < 0.0f || strength > kMaxBoldStrength) throw std::invalid_argument("bold strength out of range");
  boldStrength_ = strength;
}

const r1ui::text::ShapedRun* TextEngine::shaped(std::string_view utf8, float pixelSize) {
  if (!r1ui::text::isValidPixelSize(pixelSize)) return nullptr;
  Key key{std::string(utf8), pixelSize};
  const auto found = runs_.find(key);
  if (found != runs_.end()) return &found->second;
  auto run = r1ui::text::shapeText(*regular_, pixelSize, utf8);
  if (!run.ok()) return nullptr;
  if (runs_.size() >= kMaxCachedRuns) runs_.clear();
  return &runs_.emplace(std::move(key), std::move(run.value())).first->second;
}

float TextEngine::measure(std::string_view utf8, float pixelSize, int) {
  const r1ui::text::ShapedRun* run = shaped(utf8, pixelSize);
  return run != nullptr ? run->width : 0.0f;
}

const r1ui::text::FontMetrics& TextEngine::metrics(float pixelSize) {
  const auto found = metrics_.find(pixelSize);
  if (found != metrics_.end()) return found->second;
  const float safe = r1ui::text::isValidPixelSize(pixelSize) ? pixelSize : 12.0f;
  return metrics_.emplace(pixelSize, regular_->metricsAt(safe)).first->second;
}

float TextEngine::baselineInBox(float pixelSize, float boxHeight) { return metrics(pixelSize).baselineInBox(boxHeight); }

const FittedText& TextEngine::fit(std::string_view utf8, float pixelSize, float maxWidth) {
  const float width = measure(utf8, pixelSize);
  if (!(maxWidth >= 0.0f) || width <= maxWidth) {
    lastFit_ = {std::string(utf8), width, false};
    return lastFit_;
  }
  // Quantised so sub-pixel jitter of a layout width does not defeat the cache.
  const int32_t widthQ = static_cast<int32_t>(std::min(maxWidth, 1.0e6f) * 64.0f);
  FitKey key{std::string(utf8), pixelSize, widthQ};
  const auto found = fits_.find(key);
  if (found != fits_.end()) return found->second;
  auto result = r1ui::text::truncateWithEllipsis(*regular_, pixelSize, utf8, maxWidth);
  FittedText fitted;
  if (result.ok()) fitted = {std::move(result.value().text), result.value().width, result.value().truncated};
  if (fits_.size() >= kMaxCachedFits) fits_.clear();
  return fits_.emplace(std::move(key), std::move(fitted)).first->second;
}

void TextEngine::beginFrame() { atlas_.beginFrame(); }

void TextEngine::draw(r1ui::render::Painter& painter, std::string_view utf8, float pixelSize, int weight, float penX,
                      float baselineY, const r1ui::render::Color& tint) {
  if (utf8.empty()) return;
  const r1ui::text::ShapedRun* run = shaped(utf8, pixelSize);
  if (run == nullptr) return;
  quads_.clear();
  r1ui::text::QuadParams params;
  params.pixelSize = pixelSize;
  params.emboldenPx = weight >= kSyntheticBoldFromWeight ? r1ui::text::defaultEmboldenPx(pixelSize) * boldStrength_ : 0.0f;
  const auto stats = r1ui::text::buildGlyphQuads(atlas_, *regular_, *run, params, penX, baselineY, quads_);
  if (!stats.ok()) return;
  if (stats.value().atlasFull) overflow_ = true;
  const r1ui::render::TextureRef ref = texture_->ref();
  for (const r1ui::text::GlyphQuad& q : quads_) {
    painter.drawTexture(ref, {q.x, q.y, q.w, q.h}, {q.u0, q.v0, q.u1 - q.u0, q.v1 - q.v0}, tint);
  }
}

void TextEngine::uploadAtlas() {
  const std::optional<r1ui::text::DirtyRect> dirty = atlas_.takeDirtyRect();
  if (!dirty) return;
  const auto width = r1ui::core::checkedCast<size_t>(dirty->w);
  const auto height = r1ui::core::checkedCast<size_t>(dirty->h);
  const auto stride = r1ui::core::checkedCast<size_t>(atlas_.width());
  scratch_.resize(width * height);
  const uint8_t* source = atlas_.pixels();
  for (size_t row = 0; row < height; ++row) {
    const size_t from = (r1ui::core::checkedCast<size_t>(dirty->y) + row) * stride + r1ui::core::checkedCast<size_t>(dirty->x);
    std::copy_n(source + from, width, scratch_.begin() + static_cast<std::ptrdiff_t>(row * width));
  }
  texture_->update(r1ui::core::checkedCast<uint32_t>(dirty->x), r1ui::core::checkedCast<uint32_t>(dirty->y),
                   r1ui::core::checkedCast<uint32_t>(dirty->w), r1ui::core::checkedCast<uint32_t>(dirty->h), scratch_);
}

bool TextEngine::consumeAtlasOverflow() {
  if (!overflow_) return false;
  overflow_ = false;
  atlas_.clear();
  return true;
}

}  // namespace r1ui::widgets
