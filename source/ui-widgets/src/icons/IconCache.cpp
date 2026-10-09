// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of IconCache.h (SVG file loading, shelf placement in the atlas, drawing).
// Callers: widgets, UiContext, the preview.
#include "r1ui/widgets/icons/IconCache.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "r1ui/core/CheckedCast.h"
#include "r1ui/widgets/icons/SvgRaster.h"

namespace r1ui::widgets {

namespace {

constexpr uint32_t kCellGap = 1;  // keeps linear filtering from mixing neighbours

bool safeName(std::string_view name) {
  if (name.empty() || name.size() > 128) return false;
  for (char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

}  // namespace

IconCache::IconCache(TextureFactory& textures, std::vector<std::filesystem::path> directories)
    : directories_(std::move(directories)), texture_(textures.createCoverage(kAtlasSide, kAtlasSide)) {}

std::filesystem::path IconCache::locate(std::string_view name) const {
  // Names are file stems only: a path separator or dot would let a caller read outside the icon folders.
  if (!safeName(name)) throw std::runtime_error("Invalid icon name \"" + std::string(name) + "\"");
  for (const auto& dir : directories_) {
    std::filesystem::path file = dir / (std::string(name) + ".svg");
    std::error_code ec;
    if (std::filesystem::is_regular_file(file, ec)) return file;
  }
  throw std::runtime_error("Cannot open the icon " + std::string(name) + ".svg");
}

bool IconCache::allocate(uint32_t side, Cell& out) {
  const uint32_t padded = side + kCellGap;
  if (padded > kAtlasSide) throw std::runtime_error("the icon is larger than the icon atlas");
  if (cursorX_ + padded > kAtlasSide) {
    shelfY_ += shelfHeight_;
    shelfHeight_ = 0;
    cursorX_ = 0;
  }
  if (shelfY_ + padded > kAtlasSide) return false;
  out = {cursorX_, shelfY_};
  cursorX_ += padded;
  if (padded > shelfHeight_) shelfHeight_ = padded;
  return true;
}

void IconCache::prepare(std::string_view name, int pixelSize) {
  std::pair<std::string, int> key{std::string(name), pixelSize};
  if (cells_.count(key) != 0) return;
  const std::filesystem::path file = locate(name);
  std::ifstream in(file, std::ios::binary);
  if (!in) throw std::runtime_error("Cannot open the icon " + file.string());
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const SvgParseResult parsed = parseSvg(text);
  if (!parsed.ok) throw std::runtime_error("Cannot use the icon " + file.string() + ": " + parsed.error);
  const std::vector<uint8_t> coverage = rasterizeIcon(parsed.icon, pixelSize);
  const auto side = r1ui::core::checkedCast<uint32_t>(pixelSize);
  Cell cell;
  if (!allocate(side, cell)) {
    cells_.clear();
    shelfY_ = shelfHeight_ = cursorX_ = 0;
    overflow_ = true;
    if (!allocate(side, cell)) throw std::runtime_error("the icon atlas cannot hold the icon");
  }
  texture_->update(cell.x, cell.y, side, side, coverage);
  cells_.emplace(std::move(key), cell);
}

void IconCache::draw(r1ui::render::Painter& painter, std::string_view name, float x, float y, int pixelSize,
                     const r1ui::render::Color& tint) {
  prepare(name, pixelSize);
  const Cell cell = cells_.at({std::string(name), pixelSize});
  const float side = static_cast<float>(pixelSize);
  const float atlas = static_cast<float>(kAtlasSide);
  const float u0 = static_cast<float>(cell.x) / atlas;
  const float v0 = static_cast<float>(cell.y) / atlas;
  painter.drawTexture(texture_->ref(), {std::round(x), std::round(y), side, side}, {u0, v0, side / atlas, side / atlas}, tint);
}

bool IconCache::consumeOverflow() {
  const bool was = overflow_;
  overflow_ = false;
  return was;
}

}  // namespace r1ui::widgets
