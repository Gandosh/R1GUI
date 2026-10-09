// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the icon cache of the preview: Lucide SVG files rasterised on first use (SvgRaster) at the
//   requested pixel size into one R8 coverage texture, drawn as tinted quads.
// Why: docs/spec/icons.md decides on coverage atlases tinted by the draw call (hover colour changes
//   cost nothing); this is the preview's small implementation of it.
// Callers: Scene paint code, Bench. Calls: SvgRaster, ui-render Texture/Painter.
// Failure behavior: a missing or unparsable SVG throws std::runtime_error naming the file (the
//   shell turns it into a message box); prepare() lets startup surface that before the first frame.
// Limits: one 512x512 atlas; an icon that no longer fits throws instead of overwriting another.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "r1ui/render/Painter.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/render/Texture.h"

namespace preview {

class IconSet {
 public:
  IconSet(r1ui::render::RenderDevice& device, std::filesystem::path directory);

  // Loads and rasterises `name` (file name without .svg) at `pixelSize` if not cached yet.
  void prepare(std::string_view name, int pixelSize);
  // Draws the icon in a square of `pixelSize` whose top-left is (x, y), snapped to whole pixels.
  void draw(r1ui::render::Painter& painter, std::string_view name, float x, float y, int pixelSize,
            const r1ui::render::Color& tint);

 private:
  struct Cell {
    uint32_t x = 0;
    uint32_t y = 0;
  };
  Cell allocate(uint32_t side);

  r1ui::render::RenderDevice& device_;
  std::filesystem::path directory_;
  std::unique_ptr<r1ui::render::Texture> texture_;
  std::map<std::pair<std::string, int>, Cell> cells_;
  uint32_t shelfY_ = 0;
  uint32_t shelfHeight_ = 0;
  uint32_t cursorX_ = 0;
};

}  // namespace preview
