// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ColorPicker, the content of the colour picker popover measured in screen-color-picker-open:
//   an optional mode strip (Solid / Gradient / Image, 24 px icon tabs), the saturation / value square,
//   the hue and alpha slider rows, the component mode select (RGB / HSL / HEX) with the eyedropper
//   slot, the channel entry box and the swatch section; plus the colour state they all edit.
// Why: one widget that every consumer (fill popover, stop editor of the gradient, a property panel)
//   mounts, with one data contract: setColor in, onChanged out, bracketed by begin / end callbacks so
//   a drag, a keystroke or a typed value becomes exactly one undo step.
// Callers: application code (inside a Popover or any container), GradientEditor (without chrome and
//   tabs, editing the selected stop), tests, the gallery. Calls: the parts in this folder.
// Colour contract: setColor adopts an RGBA without firing callbacks and keeps the hue of a grey
//   (ColorState). The user's edits fire onBeginInteraction (once per gesture, however many parts
//   take part), onChanged(ColorChange) for every change including the intermediate ones of a drag
//   (interactive = true while a gesture is open), then onEndInteraction. Programmatic setters never
//   call back. Escape during a pointer drag restores the colour the gesture started with and still
//   closes the bracket.
// Eyedropper: the button only calls onEyedropper (the host picks the pixel and calls setColor). The
//   save button calls onSaveSwatches; the swatch list changes call onSwatchesChanged.
// Geometry (logical px): content 222 wide; with chrome the box is 240 wide: padding 8, gap 8
//   between sections, 1 px border `border` inside the padding (so 9 px from the outer edge), radius
//   8, fill `panel`, shadow xl (it is a popover surface); without chrome the widget is a plain column
//   that fills its parent's width.
// Failure behavior: hostile values are clamped by the model; mode strip callbacks may destroy the
//   picker; a destroyed picker leaves no overlay open.
#pragma once

#include <functional>
#include <span>
#include <vector>

#include "r1ui/widgets/colorpicker/ChannelFields.h"
#include "r1ui/widgets/colorpicker/ColorControls.h"
#include "r1ui/widgets/colorpicker/ColorModel.h"
#include "r1ui/widgets/colorpicker/PickerButton.h"
#include "r1ui/widgets/colorpicker/PickerDropdown.h"
#include "r1ui/widgets/colorpicker/Swatches.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

enum class PickerMode : uint8_t { Solid, Gradient, Image };

struct ColorChange {
  color::Rgba color;
  bool interactive = false;  // true while a drag or entry gesture is still open
};

class ColorPicker : public WidgetObject {
 public:
  static constexpr double kContentWidth = 222.0;

  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "ColorPicker"; }
  void onAttached() override;
  void onDetached() override;
  void paint(PaintContext& ctx) override;
  std::string_view accessibleName() const override;

  // ---- colour ----
  void setColor(const color::Rgba& color);
  color::Rgba color() const { return state_.rgba(); }
  const color::ColorState& state() const { return state_; }

  // ---- look ----
  void setChrome(bool chrome);        // border, fill, radius and shadow of a popover surface (default on)
  void setShowModeTabs(bool show);    // the Solid / Gradient / Image strip (default on)
  void setMode(PickerMode mode);      // which tab shows as active (no callback)
  PickerMode mode() const { return mode_; }
  void setComponentMode(ComponentMode mode);
  ComponentMode componentMode() const { return component_; }
  void setSwatches(std::vector<color::Rgba> swatches);
  const std::vector<color::Rgba>& swatches() const;

  // ---- callbacks ----
  std::function<void()> onBeginInteraction;
  std::function<void(const ColorChange&)> onChanged;
  std::function<void()> onEndInteraction;
  std::function<void(PickerMode)> onModeChanged;
  std::function<void()> onEyedropper;
  std::function<void(const std::vector<color::Rgba>&)> onSwatchesChanged;
  std::function<void()> onSaveSwatches;

  // ---- parts (tests, gallery) ----
  SvSquare& square() const;
  ColorSliderRow& hueRow() const;
  ColorSliderRow& alphaRow() const;
  ChannelFields& channels() const;
  PickerDropdown& modeSelect() const;
  PickerButton& eyedropper() const;
  SwatchRow& swatchRow() const;
  PickerButton& modeTab(PickerMode mode) const;
  bool gestureOpen() const { return gestureDepth_ > 0; }

 private:
  void beginGesture();
  void endGesture();
  void commit(const color::ColorState& next);
  void refreshParts();

  color::ColorState state_;
  PickerMode mode_ = PickerMode::Solid;
  ComponentMode component_ = ComponentMode::Rgb;
  bool chrome_ = true;
  int gestureDepth_ = 0;
  core::tree::WidgetId tabs_, square_, hue_, alpha_, modeRow_, modeSelect_, eyedropper_, channels_, swatches_;
  core::tree::WidgetId tabButtons_[3];
};

}  // namespace r1ui::widgets
