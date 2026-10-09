// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PickerButton, the minimal icon button of the pickers (mode tabs of the fill picker,
//   eyedropper slot, add / save swatch actions, add-stop button), with the measured looks: ghost
//   tab (24 px, transparent, hover and selected fill `hover`), field button (panel-field fill) and
//   plain icon (transparent, colour change only).
// Why: the shared IconButton is being built by another group at the same time; the pickers must not
//   depend on it. After the merge an integrator may replace this private helper (the pickers use
//   only the constructor arguments, setActive, setIcon and onActivate).
// Callers: ColorPicker, GradientEditor, tests. Calls: PaintContext (icons, animated colours).
// Behaviour: a left click or Enter / Space activates; a disabled button gets no events. Hover and
//   selected state colours fade over 150 ms (instant when no frame loop runs). A keyboard focus
//   shows the focus ring (focus.ring row). Pointer cursor. Accessible name = tooltip text.
#pragma once

#include <functional>
#include <span>
#include <string>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class PickerButton : public WidgetObject {
 public:
  enum class Look : uint8_t { Tab, Field, Plain };

  PickerButton(std::string icon, Look look, double sizeLogical, double iconLogical = 14.0);
  static std::span<const theme::StyleRuleEntry> styleRows();

  const char* typeName() const override { return "PickerButton"; }
  void onAttached() override;
  float paintOpacity() const override;
  Cursor cursor() const override { return Cursor::Pointer; }
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  std::string_view accessibleName() const override;

  void setIcon(std::string icon);
  const std::string& icon() const { return icon_; }
  // The tab that is on (selected look).
  void setActive(bool active) { setSelected(active); }
  bool active() const { return hasState(StateFlag::kSelected); }
  void setTooltipAndName(std::string text);

  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;

  std::function<void()> onActivate;

 private:
  const char* rowKey() const;
  void activate();

  std::string icon_;
  Look look_;
  double size_;
  double iconSize_;
};

}  // namespace r1ui::widgets
