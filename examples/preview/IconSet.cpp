// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of IconSet.h (SVG file loading, shelf placement in the atlas, drawing).
// Callers: Scene paint code, Bench.
#include "IconSet.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "SvgRaster.h"
#include "r1ui/core/CheckedCast.h"

namespace preview {

namespace {

constexpr uint32_t kAtlasSide = 512;
constexpr uint32_t kCellGap = 1;  // keeps linear filtering from mixing neighbours

}  // namespace

IconSet::IconSet(r1ui::render::RenderDevice& device, std::filesystem::path directory)
    : device_(device),
      directory_(std::move(directory)),
      texture_(std::make_unique<r1ui::render::Texture>(device_, kAtlasSide, kAtlasSide, r1ui::render::TextureFormat::R8Coverage)) {}

IconSet::Cell IconSet::allocate(uint32_t side) {
  const uint32_t padded = side + kCellGap;
  if (cursorX_ + padded > kAtlasSide) {
    shelfY_ += shelfHeight_;
    shelfHeight_ = 0;
    cursorX_ = 0;
  }
  if (padded > kAtlasSide || shelfY_ + padded > kAtlasSide) throw std::runtime_error("the icon atlas is full");
  const Cell cell{cursorX_, shelfY_};
  cursorX_ += padded;
  if (padded > shelfHeight_) shelfHeight_ = padded;
  return cell;
}

void IconSet::prepare(std::string_view name, int pixelSize) {
  std::pair<std::string, int> key{std::string(name), pixelSize};
  if (cells_.count(key) != 0) return;
  const std::filesystem::path file = directory_ / (key.first + ".svg");
  std::ifstream in(file, std::ios::binary);
  if (!in) throw std::runtime_error("Cannot open the icon " + file.string());
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const SvgParseResult parsed = parseSvg(text);
  if (!parsed.ok) throw std::runtime_error("Cannot use the icon " + file.string() + ": " + parsed.error);
  const std::vector<uint8_t> coverage = rasterizeIcon(parsed.icon, pixelSize);
  const auto side = r1ui::core::checkedCast<uint32_t>(pixelSize);
  const Cell cell = allocate(side);
  texture_->update(cell.x, cell.y, side, side, coverage);
  cells_.emplace(std::move(key), cell);
}

void IconSet::draw(r1ui::render::Painter& painter, std::string_view name, float x, float y, int pixelSize,
                   const r1ui::render::Color& tint) {
  prepare(name, pixelSize);
  const Cell cell = cells_.at({std::string(name), pixelSize});
  const float side = static_cast<float>(pixelSize);
  const float atlas = static_cast<float>(kAtlasSide);
  const float u0 = static_cast<float>(cell.x) / atlas;
  const float v0 = static_cast<float>(cell.y) / atlas;
  painter.drawTexture(texture_->ref(), {std::round(x), std::round(y), side, side},
                      {u0, v0, side / atlas, side / atlas}, tint);
}

}  // namespace preview
