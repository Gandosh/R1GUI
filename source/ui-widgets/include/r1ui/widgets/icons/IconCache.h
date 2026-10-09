// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the shared icon service: SVG files rasterised on first use (SvgRaster) at the requested
//   pixel size into one coverage atlas texture, drawn as tinted quads.
// Why: docs/spec/icons.md decides on coverage atlases tinted by the draw call (hover colour
//   changes cost nothing); one cache is shared by every window (Services owns it).
// Callers: widgets (draw), UiContext (overflow handling), the preview, tests. Calls: SvgRaster and
//   the TextureFactory interface.
// Lookup: `name` is the file name without ".svg", searched in the directories given at
//   construction in order (Lucide first, then toolkit-specific icons), so a toolkit icon never
//   needs to be added to the Lucide folder.
// Failure behavior: a missing or unparsable SVG throws std::runtime_error naming the file (the
//   shell turns it into a message box); prepare() lets startup surface that before the first frame.
// Limits: one kAtlasSide x kAtlasSide atlas. When it is full the atlas is reset and
//   consumeOverflow() reports that the frame must be painted again (quads built earlier in that
//   frame may point at replaced cells); a single icon larger than the atlas throws.
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
#include "r1ui/widgets/icons/SvgRaster.h"
#include "r1ui/widgets/text/TextureFactory.h"

namespace r1ui::widgets {

class IconCache {
 public:
  static constexpr uint32_t kAtlasSide = 1024;

  IconCache(TextureFactory& textures, std::vector<std::filesystem::path> directories);

  // Loads and rasterises `name` at `pixelSize` if not cached yet. Throws as described above.
  void prepare(std::string_view name, int pixelSize);
  // Draws the icon in a square of `pixelSize` whose top-left is (x, y), snapped to whole pixels.
  void draw(r1ui::render::Painter& painter, std::string_view name, float x, float y, int pixelSize,
            const r1ui::render::Color& tint);
  // Rasterisation anti-aliasing; changing it drops every cached icon (they are rasterised again on use).
  void setAntiAlias(AntiAlias aa);
  AntiAlias antiAlias() const { return aa_; }
  // True (once) after the atlas was reset because it was full.
  bool consumeOverflow();
  size_t cachedCount() const { return cells_.size(); }

 private:
  struct Cell {
    uint32_t x = 0;
    uint32_t y = 0;
  };
  bool allocate(uint32_t side, Cell& out);
  std::filesystem::path locate(std::string_view name) const;

  std::vector<std::filesystem::path> directories_;
  std::unique_ptr<AtlasTexture> texture_;
  std::map<std::pair<std::string, int>, Cell> cells_;
  uint32_t shelfY_ = 0;
  uint32_t shelfHeight_ = 0;
  uint32_t cursorX_ = 0;
  AntiAlias aa_ = AntiAlias::Smooth;
  bool overflow_ = false;
};

}  // namespace r1ui::widgets
