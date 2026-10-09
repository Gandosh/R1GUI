// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the "Color swatches" section of the colour picker: a 1 px top divider, a header row with the
//   muted title and two icon buttons (add the current colour, save the list), and a wrapping row of
//   swatches (20 px squares, radius 4, 1 px border, drawn over a checkerboard so translucent colours
//   show), plus PickerBox, the plain flex container the picker parts share.
// Why: the swatch list is user-ordered state the host owns (D18): the widget shows it, lets the user
//   apply, add and remove entries, and reports every change through callbacks; persisting the list is
//   the host's job (the save button only calls onSave).
// Callers: ColorPicker, GradientEditor (through the picker), tests. Calls: PickerButton, PickerDraw.
// Behaviour: a click or Enter / Space on a swatch applies it (onApply); Delete / Backspace on a
//   focused swatch, or Alt + click, removes it; the add button appends the current colour unless an
//   equal one exists (then that swatch is just highlighted). At most kMaxSwatches entries; longer
//   lists passed to setSwatches are cut. The swatch equal to the current colour (8-bit RGBA) shows
//   the accent border.
#pragma once

#include <functional>
#include <span>
#include <vector>

#include "r1ui/widgets/colorpicker/ColorModel.h"
#include "r1ui/widgets/colorpicker/PickerButton.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

// A flex container with no look of its own.
class PickerBox : public WidgetObject {
 public:
  explicit PickerBox(const char* name = "PickerBox") : name_(name) {}
  const char* typeName() const override { return name_; }

 private:
  const char* name_;
};

class SwatchRow : public WidgetObject {
 public:
  static constexpr size_t kMaxSwatches = 256;

  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "SwatchRow"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;

  void setSwatches(std::vector<color::Rgba> swatches);
  const std::vector<color::Rgba>& swatches() const { return swatches_; }
  void setCurrent(const color::Rgba& current);
  // Adds the current colour (the add button); returns false when an equal swatch exists or the list is full.
  bool addCurrent();
  bool removeAt(size_t index);
  PickerButton& addButton() const;
  PickerButton& saveButton() const;

  std::function<void(const color::Rgba&)> onApply;
  std::function<void(const std::vector<color::Rgba>&)> onSwatchesChanged;
  std::function<void()> onSave;

 private:
  void rebuild();
  void changed();

  std::vector<color::Rgba> swatches_;
  color::Rgba current_;
  core::tree::WidgetId header_;
  core::tree::WidgetId flow_;
  core::tree::WidgetId add_;
  core::tree::WidgetId save_;
};

}  // namespace r1ui::widgets
