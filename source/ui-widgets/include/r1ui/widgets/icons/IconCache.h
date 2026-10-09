// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the shared icon service: SVG files rasterised on first use (SvgRaster) at the requested
//   pixel size into one coverage atlas texture, drawn as tinted quads.
// Why: docs/spec/icons.md decides on coverage atlases tinted by the draw call (hover colour
//   changes cost nothing); one cache is shared by every window (Services owns it).
// Callers: widgets (draw), UiContext (reset epoch), the preview, tests. Calls: SvgRaster and
//   the TextureFactory interface.
// Lookup: `name` is the file name without ".svg", searched in the directories given at
//   construction in order (Lucide first, then toolkit-specific icons), so a toolkit icon never
//   needs to be added to the Lucide folder.
// Failure behavior: prepare() throws std::runtime_error naming the file for a missing or unparsable
//   SVG, so startup can surface it before the first frame. draw() never throws for such an icon: it
//   remembers the failure (one disk lookup per name and size, not one per frame), counts it
//   (failedLoads) and draws a hollow square of the icon's size in the tint, so a typo in an icon name
//   is visible and cannot unbalance the painter's clip and opacity stacks during paint.
// Limits: one kAtlasSide x kAtlasSide atlas. When it is full the atlas is reset and resetEpoch()
//   advances; a window that sees a different epoch at the end of its frame than at its start must paint
//   again (quads built earlier in that frame may point at replaced cells). The epoch is a counter, not a
//   consumed flag, so every window that was painting during the reset notices it, not just the first
//   one to ask. A single icon larger than the atlas throws.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
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
  // Draws the icon in a square of `pixelSize` whose top-left is (x, y), snapped to whole pixels. When
  // `name` cannot be loaded the icon `fallback` is drawn instead (if given and loadable), else a hollow square.
  void draw(r1ui::render::Painter& painter, std::string_view name, float x, float y, int pixelSize,
            const r1ui::render::Color& tint, std::string_view fallback = {});
  // Rasterisation anti-aliasing; changing it drops every cached icon (they are rasterised again on use).
  void setAntiAlias(AntiAlias aa);
  AntiAlias antiAlias() const { return aa_; }
  // Counts atlas resets (full atlas, anti-aliasing change). Compare the value at the start and at the
  // end of a frame.
  uint64_t resetEpoch() const { return resetEpoch_; }
  size_t cachedCount() const { return cells_.size(); }
  // Number of (name, size) pairs that failed to load and are drawn as the placeholder.
  size_t failedLoads() const { return failed_.size(); }

 private:
  struct Cell {
    uint32_t x = 0;
    uint32_t y = 0;
  };
  bool allocate(uint32_t side, Cell& out);
  std::filesystem::path locate(std::string_view name) const;
  bool tryPrepare(const std::pair<std::string, int>& key);

  std::vector<std::filesystem::path> directories_;
  std::unique_ptr<AtlasTexture> texture_;
  std::map<std::pair<std::string, int>, Cell> cells_;
  std::set<std::pair<std::string, int>> failed_;
  uint32_t shelfY_ = 0;
  uint32_t shelfHeight_ = 0;
  uint32_t cursorX_ = 0;
  AntiAlias aa_ = AntiAlias::Smooth;
  uint64_t resetEpoch_ = 0;
};

}  // namespace r1ui::widgets
