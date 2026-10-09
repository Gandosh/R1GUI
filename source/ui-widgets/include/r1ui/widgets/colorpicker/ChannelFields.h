// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ChannelFields, the numeric / hex entry box under the colour sliders: one 26 px box with a
//   1 px border (fill `input`, radius 4) holding three cells separated by dividers (R, G, B 0..255;
//   or H 0..360, S %, L %) or one cell (the hex digits), selected by a mode.
// Why: the measured picker shows the same colour through RGB, HSL or HEX text. The conversions and
//   the rules for what a typed value means (clamping, hex shorthand, the hue of a grey) live here
//   so the picker only sees "the user committed this colour state".
// Callers: ColorPicker, tests. Calls: ColorModel, NumberText, PickerEntry.
// Behaviour: every cell commit applies on its own (Enter, Tab or focus loss) as one gesture
//   (onBegin, onChange, onEnd); invalid text reverts the cell. Up / Down arrows step the focused
//   cell by 1 (10 with Shift). Values never leave their range: channels clamp, hue wraps. The hex
//   cell accepts #rgb, #rgba, #rrggbb, #rrggbbaa (alpha digits also set the alpha) with or without
//   '#'; it shows six digits without '#'.
#pragma once

#include <array>
#include <functional>
#include <span>

#include "r1ui/widgets/colorpicker/ColorModel.h"
#include "r1ui/widgets/colorpicker/PickerEntry.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

enum class ComponentMode : uint8_t { Rgb, Hsl, Hex };

class ChannelFields : public WidgetObject {
 public:
  const char* typeName() const override { return "ChannelFields"; }
  static std::span<const theme::StyleRuleEntry> styleRows();
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;

  void setMode(ComponentMode mode);
  ComponentMode mode() const { return mode_; }
  // Display state (no callbacks).
  void setColorState(const color::ColorState& state);
  // The cell entries (3 in Rgb / Hsl, the first in Hex), for tests.
  PickerEntry& cell(int index) const;
  int visibleCells() const { return mode_ == ComponentMode::Hex ? 1 : 3; }

  std::function<void()> onBegin;
  std::function<void(const color::ColorState&)> onChange;
  std::function<void()> onEnd;

 private:
  void refresh();
  bool commit(int index, std::string_view text);
  void step(int index, int steps);
  void apply(const color::ColorState& next);

  ComponentMode mode_ = ComponentMode::Rgb;
  color::ColorState state_;
  std::array<core::tree::WidgetId, 3> cells_;
};

}  // namespace r1ui::widgets
