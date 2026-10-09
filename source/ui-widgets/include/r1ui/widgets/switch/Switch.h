// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Switch widget: a pill toggle in sizes sm (28 x 16, thumb 12, travel 12) and md (36 x 20,
//   thumb 16, travel 16) with the states off, on, mixed, keyboard focus and disabled.
// Why: boolean settings (grid snap, component properties) use one measured control; see
//   docs/spec/widgets.md 2.7.
// Callers: application code, popovers, property panels. Calls: Pressable (input), PaintContext.
// Look (measured): off = 1 px `border` border over `panel-field`, thumb `muted`; on = `accent`
//   fill and border, white thumb; mixed = `accent` at 20% fill, `accent` at 60% border, `accent`
//   thumb centred between the ends; the thumb has the shadow-sm elevation; keyboard focus draws a 1 px `panel-focus` ring just outside
//   the pill; hover changes nothing (measured: the reference has no hover rule); colours and the
//   thumb move over 150 ms.
// Behaviour: a click or Space/Enter on key-up toggles (mixed becomes on); the callback runs for user
//   changes only, with the new value, and may destroy the switch. Disabled is 50% opacity.
#pragma once

#include <functional>
#include <span>

#include "r1ui/widgets/button/Pressable.h"

namespace r1ui::widgets {

enum class SwitchSize : uint8_t { Sm, Md };

class Switch : public Pressable {
 public:
  explicit Switch(SwitchSize size = SwitchSize::Sm) : size_(size) {}

  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "Switch"; }

  bool checked() const { return hasState(StateFlag::kSelected); }
  bool mixed() const { return hasState(StateFlag::kMixed); }
  // Setting a value clears the mixed state (WidgetObject::setMixed shows it).
  void setChecked(bool checked);
  SwitchSize size() const { return size_; }
  void setSize(SwitchSize size);
  void setOnChange(std::function<void(bool)> callback) { onChange_ = std::move(callback); }

  void onAttached() override;
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;

 protected:
  void activate() override;

 private:
  void applySize();

  SwitchSize size_;
  std::function<void(bool)> onChange_;
};

}  // namespace r1ui::widgets
