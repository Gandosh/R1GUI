// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ModeViews.h.
// Invariants: at most one reference texture lives at a time (the previous one is destroyed after the
//   frame that used it ended, i.e. before the next frame is recorded); hover indices are always
//   below the token count.
// Callers: PreviewApp.cpp.
#include "ModeViews.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "RawImage.h"
#include "r1ui/core/CheckedCast.h"

namespace preview {

namespace {

namespace fs = std::filesystem;
using r1ui::render::Color;
using r1ui::render::Rect;
using r1ui::theme::ThemeId;

constexpr int kColumns = 8;
constexpr float kSwatch = 48.0f;
constexpr float kGap = 8.0f;
constexpr float kMargin = 24.0f;
constexpr float kStatusPixels = 12.0f;

Color toPaint(const r1ui::theme::Color& c) { return Color::fromRgba8(c.r, c.g, c.b, c.a); }

// Lists reference screens for one theme, sorted by file name; names are never hard-coded.
std::vector<fs::path> listScreens(const fs::path& referenceDir, ThemeId theme) {
  std::vector<fs::path> screens;
  std::error_code error;
  for (const auto& entry : fs::directory_iterator(referenceDir / r1ui::theme::themeName(theme), error)) {
    const fs::path& path = entry.path();
    if (path.extension() == ".r1img" && path.filename().string().rfind("screen-", 0) == 0) screens.push_back(path);
  }
  std::sort(screens.begin(), screens.end());
  return screens;
}

}  // namespace

// ---- Swatches -------------------------------------------------------------------------------

std::optional<size_t> SwatchesView::swatchAt(float px, float py, const Rect& body, float scale) const {
  const float pitch = (kSwatch + kGap) * scale;
  const float x = px - body.x - kMargin * scale;
  const float y = py - body.y - kMargin * scale;
  if (x < 0 || y < 0) return std::nullopt;
  const auto col = static_cast<int>(x / pitch);
  const auto row = static_cast<int>(y / pitch);
  if (col >= kColumns || std::fmod(x, pitch) >= kSwatch * scale || std::fmod(y, pitch) >= kSwatch * scale) return std::nullopt;
  const size_t index = static_cast<size_t>(row) * kColumns + static_cast<size_t>(col);
  if (index >= tokens_.colorCount()) return std::nullopt;
  return index;
}

bool SwatchesView::onPointer(float x, float y, const Rect& body, float scale) {
  const std::optional<size_t> hover = swatchAt(x, y, body, scale);
  if (hover == hover_) return false;
  hover_ = hover;
  return true;
}

void SwatchesView::clearHover() { hover_.reset(); }

void SwatchesView::paint(r1ui::render::Painter& painter, TextEngine& text, ThemeId theme, const Rect& body, float scale) {
  const Color accent = toPaint(*tokens_.color(theme, "accent"));
  const float pitch = (kSwatch + kGap) * scale;
  for (size_t i = 0; i < tokens_.colorCount(); ++i) {
    const float x = body.x + kMargin * scale + static_cast<float>(i % kColumns) * pitch;
    const float y = body.y + kMargin * scale + static_cast<float>(i / kColumns) * pitch;
    const float size = kSwatch * scale;
    const bool hovered = hover_ && *hover_ == i;
    // The outline behind the swatch keeps colours equal to the canvas visible; hover thickens it.
    const float outline = (hovered ? 3.0f : 1.0f) * scale;
    painter.fillRect({x - outline, y - outline, size + 2 * outline, size + 2 * outline}, hovered ? accent : Color::fromRgba8(128, 128, 128));
    painter.fillRect({x, y, size, size}, toPaint(*tokens_.colorAt(theme, i)));
  }
  const float statusY = body.y + body.h - 30.0f * scale;
  std::string status;
  if (hover_) {
    status = std::string(r1ui::theme::themeName(theme)) + ": " + tokens_.colorNames()[*hover_] + " " +
             r1ui::theme::toHex(*tokens_.colorAt(theme, *hover_));
  } else {
    status = std::string("R1GUI Preview - tokens (") + r1ui::theme::themeName(theme) + ")  Tab: next mode, T: theme";
  }
  const float px = kStatusPixels * scale;
  text.draw(painter, status, px, 400, body.x + kMargin * scale, statusY + text.baselineInBox(px, 20.0f * scale),
            toPaint(*tokens_.color(theme, "muted")));
}

// ---- Screens --------------------------------------------------------------------------------

ScreensView::ScreensView(r1ui::render::RenderDevice& device, const fs::path& referenceDir) : device_(device) {
  for (ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    screens_[static_cast<size_t>(theme)] = listScreens(referenceDir, theme);
    if (screens_[static_cast<size_t>(theme)].empty()) {
      throw std::runtime_error("no reference screens found for the " + std::string(r1ui::theme::themeName(theme)) +
                               " theme in " + (referenceDir / r1ui::theme::themeName(theme)).string() +
                               "\n\nRebuild the r1gui-preview target to convert them (needs Python 3).");
    }
  }
}

void ScreensView::step(int delta) {
  const auto n = r1ui::core::checkedCast<int>(screens_[0].size());
  index_ = static_cast<size_t>((r1ui::core::checkedCast<int>(index_) + delta + n) % n);
}

void ScreensView::onClick(float x, const Rect& body) { step(x < body.x + body.w * 0.5f ? -1 : +1); }

// Keeps the uploaded texture in step with the selected theme and index; one texture lives at a time.
void ScreensView::sync(ThemeId theme) {
  index_ = std::min(index_, screens(theme).size() - 1);  // a theme switch may shrink the list
  if (texture_ && loadedTheme_ == theme && loadedIndex_ == index_) return;
  RawImageResult raw = loadRawImage(screens(theme)[index_]);
  if (!raw.image) throw std::runtime_error(raw.error);
  // The raw file holds BGRA; textures take RGBA.
  std::vector<uint8_t>& pixels = raw.image->bgra;
  for (size_t i = 0; i + 3 < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
  texture_ = std::make_unique<r1ui::render::Texture>(device_, raw.image->width, raw.image->height,
                                                     r1ui::render::TextureFormat::Rgba8Srgb, pixels);
  imageWidth_ = raw.image->width;
  imageHeight_ = raw.image->height;
  loadedTheme_ = theme;
  loadedIndex_ = index_;
}

void ScreensView::paint(r1ui::render::Painter& painter, TextEngine& text, ThemeId theme, const Rect& body, float scale) {
  sync(theme);
  if (body.w >= 1.0f && body.h >= 1.0f && imageWidth_ > 0 && imageHeight_ > 0) {
    // Letterbox: fit the whole image, keep the aspect, centre it.
    const float fit = std::min(body.w / static_cast<float>(imageWidth_), body.h / static_cast<float>(imageHeight_));
    const float w = static_cast<float>(imageWidth_) * fit;
    const float h = static_cast<float>(imageHeight_) * fit;
    painter.drawTexture(texture_->ref(), {body.x + (body.w - w) * 0.5f, body.y + (body.h - h) * 0.5f, w, h}, {0, 0, 1, 1});
  }
  const std::string caption = std::string(r1ui::theme::themeName(theme)) + " " + screens(theme)[index_].stem().string() + " (" +
                              std::to_string(index_ + 1) + "/" + std::to_string(screens(theme).size()) + ")  Left/Right or click: step";
  const float px = kStatusPixels * scale;
  const float top = body.y + body.h - 26.0f * scale;
  painter.fillRect({body.x, top, body.w, 26.0f * scale}, Color::fromRgba8(0, 0, 0, 160));
  text.draw(painter, caption, px, 400, body.x + 12.0f * scale, top + text.baselineInBox(px, 26.0f * scale), Color::fromRgba8(240, 240, 240));
}

}  // namespace preview
