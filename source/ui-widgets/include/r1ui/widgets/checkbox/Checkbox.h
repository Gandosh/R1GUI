// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Checkbox widget: a 13 x 13 box with an optional 12 px label, states unchecked, checked
//   and mixed (indeterminate), hover, keyboard focus and disabled.
// Why: the reference uses the browser's native checkbox ("Clip content", menu checkbox items) with
//   `accent-color: accent`; the toolkit needs one widget that reproduces that look and the toggle
//   behaviour (a click or Space/Enter on key-up toggles; mixed becomes checked).
// Callers: application code, panels, dialogs. Calls: Pressable (input), PaintContext (drawing).
// Look (measured, tests/reference widget-checkbox-clip-content-*): the box has radius 2; unchecked
//   it is filled and has a 1 px border that brightens on hover; checked / mixed it is filled with
//   `accent` (lighter on hover in the dark theme, darker in the light one: native constants) and draws a check / dash mark whose colour is the browser's: dark #3b3b3b in the dark
//   theme, white in the light theme. The unchecked fill and border are Chrome's native colours and
//   are not design tokens, so they are constants of this file per theme (documented deviation from
//   "colours come from rows"). The label is 12 px `surface` with an 8 px gap; disabled is 50%
//   opacity; keyboard focus draws the 1 px focus ring around the box.
// Behaviour: the change callback runs for user changes only, with the new checked value (never for
//   setChecked); it may destroy the checkbox. The label is part of the hit area.
#pragma once

#include <functional>
#include <span>
#include <string>

#include "r1ui/widgets/button/Pressable.h"

namespace r1ui::widgets {

class Checkbox : public Pressable {
 public:
  explicit Checkbox(std::string label = {}) : label_(std::move(label)) {}

  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "Checkbox"; }

  bool checked() const { return hasState(StateFlag::kSelected); }
  bool mixed() const { return hasState(StateFlag::kMixed); }
  // Setting a value clears the mixed state (WidgetObject::setMixed shows it).
  void setChecked(bool checked);
  const std::string& label() const { return label_; }
  void setLabel(std::string label);
  void setOnChange(std::function<void(bool)> callback) { onChange_ = std::move(callback); }

  static constexpr double kBoxSize = 13.0;

  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  std::string_view accessibleName() const override;

 protected:
  void activate() override;

 private:
  std::string label_;
  std::function<void(bool)> onChange_;
};

}  // namespace r1ui::widgets
