// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the sample content of the Editor's brush library (slice 5.23): about forty generic sculpt-style brush
//   definitions in six categories and SampleBrushThumbnails, the thumbnail provider that draws a small
//   picture for each of them by code (soft discs, rings, stamps, strokes, hatching, wedges; nothing copied
//   from anywhere).
// Why: the owner must be able to open the library in the preview and see tiles with pictures, names,
//   letters, favourites and recents the way a real application would present them; the brush data is
//   host-supplied, so the sample lives in the preview, not in the toolkit.
// Callers: EditorApp (brush library wiring), tests/preview.
// Pictures: produce() is synchronous (always Ready) and bounded: the edge is clamped to 32..256 and the
//   result depends only on the brush, so a picture is the same on every request.
#pragma once

#include <vector>

#include "r1ui/commands/brushes/BrushLibraryModel.h"
#include "r1ui/widgets/thumbnailgrid/ThumbnailCache.h"

namespace preview::editor {

// About forty definitions; ids are stable text ("clay-buildup").
std::vector<r1ui::commands::brushes::BrushInfo> sampleBrushes();

class SampleBrushThumbnails final : public r1ui::widgets::thumbs::ThumbnailProvider {
 public:
  explicit SampleBrushThumbnails(const r1ui::commands::brushes::BrushLibraryModel& model) : model_(model) {}
  r1ui::widgets::thumbs::ThumbnailStatus produce(uint64_t itemKey, uint32_t sizePx, r1ui::widgets::thumbs::ThumbnailImage& out) override;

 private:
  const r1ui::commands::brushes::BrushLibraryModel& model_;
};

}  // namespace preview::editor
