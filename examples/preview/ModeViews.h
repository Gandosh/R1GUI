// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the two earlier preview modes re-implemented on the Painter: the design-token swatch grid
//   (hover shows the token name and value as drawn text) and the reference-screen viewer
//   (RGBA textures, letterboxed, stepped with Left/Right or by clicking the window halves).
// Why: Phase 1/2 content must keep working unchanged on the new rendering stack.
// Callers: PreviewApp (events, painting). Calls: Painter, Texture, TextEngine, Tokens, RawImage.
// Units: physical pixels; the grid geometry is scaled by the display scale given to paint().
// Failure behavior: ScreensView throws std::runtime_error naming the problem when a reference
//   image is missing or damaged or when a theme has no screens (the shell shows a message box).
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "TextEngine.h"
#include "r1ui/render/Painter.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/render/Texture.h"
#include "r1ui/theme/Tokens.h"

namespace preview {

class SwatchesView {
 public:
  explicit SwatchesView(const r1ui::theme::Tokens& tokens) : tokens_(tokens) {}

  // Pointer position in window pixels; returns true when the hovered swatch changed.
  bool onPointer(float x, float y, const r1ui::render::Rect& body, float scale);
  void clearHover();
  void paint(r1ui::render::Painter& painter, TextEngine& text, r1ui::theme::ThemeId theme, const r1ui::render::Rect& body, float scale);

 private:
  std::optional<size_t> swatchAt(float x, float y, const r1ui::render::Rect& body, float scale) const;

  const r1ui::theme::Tokens& tokens_;
  std::optional<size_t> hover_;
};

class ScreensView {
 public:
  ScreensView(r1ui::render::RenderDevice& device, const std::filesystem::path& referenceDir);

  void step(int delta);
  // Left half of `body` steps back, right half forward.
  void onClick(float x, const r1ui::render::Rect& body);
  void paint(r1ui::render::Painter& painter, TextEngine& text, r1ui::theme::ThemeId theme, const r1ui::render::Rect& body, float scale);
  size_t index() const { return index_; }
  size_t count(r1ui::theme::ThemeId theme) const { return screens_[static_cast<size_t>(theme)].size(); }

 private:
  const std::vector<std::filesystem::path>& screens(r1ui::theme::ThemeId theme) const { return screens_[static_cast<size_t>(theme)]; }
  void sync(r1ui::theme::ThemeId theme);

  r1ui::render::RenderDevice& device_;
  std::array<std::vector<std::filesystem::path>, 2> screens_;
  size_t index_ = 0;
  std::unique_ptr<r1ui::render::Texture> texture_;
  uint32_t imageWidth_ = 0;
  uint32_t imageHeight_ = 0;
  r1ui::theme::ThemeId loadedTheme_ = r1ui::theme::ThemeId::Dark;
  size_t loadedIndex_ = 0;
};

}  // namespace preview
