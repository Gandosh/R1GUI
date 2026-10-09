// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the few widgets of the preview that are not toolkit widgets: Surface (a flat token-coloured
//   region with optional hairline edges: the left column and the page backdrop), PassThrough (an
//   absolutely placed layout box that never takes pointer input, used to float controls over the
//   canvas) and ColorSwatch (the fill swatch of the paint field).
// Why: the composed screen needs plain coloured regions and a swatch; the toolkit's widgets are the
//   controls, these are only the backgrounds around them. All colours come from tokens.
// Callers: ComposedApp*.cpp, GalleryApp.cpp. Calls: PaintContext, Pressable.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "r1ui/widgets/button/Pressable.h"
#include "r1ui/widgets/colorpicker/ColorModel.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace preview {

// A flex box filled with a theme colour; `edges` draws 1 px `border` lines along the chosen sides.
class Surface final : public r1ui::widgets::WidgetObject {
 public:
  static constexpr uint8_t kLeft = 1, kRight = 2, kTop = 4, kBottom = 8;
  explicit Surface(std::string colorToken, uint8_t edges = 0) : token_(std::move(colorToken)), edges_(edges) {}
  const char* typeName() const override { return "Surface"; }
  void paint(r1ui::widgets::PaintContext& ctx) override;
  void paintOver(r1ui::widgets::PaintContext& ctx) override;

 private:
  std::string token_;
  uint8_t edges_;
};

// A layout box over its parent's area (absolute, all insets 0) that does not take pointer input;
// only its children are hit. Column, justified to the end unless the caller changes the style.
class PassThrough final : public r1ui::widgets::WidgetObject {
 public:
  const char* typeName() const override { return "PassThrough"; }
  void onAttached() override;
};

// The colour chip of a paint field: the colour over a checkerboard-free panel, 1 px border, opens
// the colour picker when activated.
class ColorSwatch final : public r1ui::widgets::Pressable {
 public:
  const char* typeName() const override { return "ColorSwatch"; }
  void onAttached() override;
  void paint(r1ui::widgets::PaintContext& ctx) override;
  void paintOver(r1ui::widgets::PaintContext& ctx) override;
  std::string_view accessibleName() const override { return "Fill colour"; }
  void setColor(const r1ui::widgets::color::Rgba& color);
  void setOnClick(std::function<void()> callback) { onClick_ = std::move(callback); }

 protected:
  void activate() override;

 private:
  r1ui::widgets::color::Rgba color_{{0.83, 0.83, 0.83}, 1.0};
  std::function<void()> onClick_;
};

}  // namespace preview
