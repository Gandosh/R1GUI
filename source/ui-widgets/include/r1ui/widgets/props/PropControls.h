// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the three small controls only the generated property panel needs and no other widget offers:
//   PropResetButton (the per-row reset affordance: a 16 px glyph button whose space is always reserved
//   and which draws and takes input only while the value differs from its default, spec 09 rules 45-46),
//   PropSlider (a horizontal slider for range properties with begin/changed/end callbacks so a drag is
//   one undo step, keyboard stepping, a mixed look and Escape to restore) and ColorChip (the 26 px colour
//   swatch of a colour row, drawing the colour over a checkerboard and a mixed look).
// Why: these have no counterpart in the Phase 4 widget set; keeping them private to the props folder
//   avoids adding public widgets for one consumer. They are plain widgets with style rows (props.*).
// Callers: PropertyRowView. Calls: Pressable (base of reset and chip), PaintContext, Router capture.
// Invariants: callbacks are not called by the programmatic setters (setValue, setRange, setColor, ...);
//   every drag is bracketed by exactly one begin and one end.
#pragma once

#include <functional>
#include <span>
#include <string>

#include "r1ui/widgets/button/Pressable.h"
#include "r1ui/widgets/colorpicker/ColorModel.h"

namespace r1ui::widgets {

// ---- reset affordance --------------------------------------------------------------------------------

class PropResetButton final : public Pressable {
 public:
  static constexpr double kSize = 16.0;
  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "PropResetButton"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  std::string_view tooltipText() const override { return shown_ ? std::string_view(tooltip_) : std::string_view(); }
  std::string_view accessibleName() const override { return tooltip_; }
  Cursor cursor() const override { return shown_ ? Cursor::Pointer : Cursor::Default; }

  // Hidden buttons keep their space but draw nothing, take no pointer input and no focus.
  void setShown(bool shown);
  bool shown() const { return shown_; }
  void setReset(std::string tooltip, std::function<void()> onReset);

 protected:
  void activate() override;

 private:
  bool shown_ = false;
  std::string tooltip_;
  std::function<void()> onReset_;
};

// ---- slider ------------------------------------------------------------------------------------------

class PropSlider final : public WidgetObject {
 public:
  static constexpr double kHeight = 26.0;
  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "PropSlider"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override { return enabled() ? Cursor::Pointer : Cursor::Default; }
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;

  double value() const { return value_; }
  // Programmatic: clamps to the range, ignores non-finite input, never calls a callback.
  void setValue(double value);
  // Non-finite bounds or min >= max reset to [0, 1].
  void setRange(double min, double max);
  double minimum() const { return min_; }
  double maximum() const { return max_; }
  bool dragging() const { return dragging_; }

  void setOnBegin(std::function<void()> callback) { onBegin_ = std::move(callback); }
  void setOnChanged(std::function<void(double value, bool interactive)> callback) { onChanged_ = std::move(callback); }
  void setOnEnd(std::function<void(bool cancelled, bool changed)> callback) { onEnd_ = std::move(callback); }

 private:
  double valueAt(double x) const;
  double fraction() const;
  void finish(bool cancelled);

  double value_ = 0.0;
  double min_ = 0.0;
  double max_ = 1.0;
  bool dragging_ = false;
  double startValue_ = 0.0;
  std::function<void()> onBegin_;
  std::function<void(double, bool)> onChanged_;
  std::function<void(bool, bool)> onEnd_;
};

// ---- colour chip -------------------------------------------------------------------------------------

class ColorChip final : public Pressable {
 public:
  static constexpr double kSize = 26.0;
  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "ColorChip"; }
  void onAttached() override;
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  std::string_view accessibleName() const override { return "Colour"; }

  void setColor(const color::Rgba& color);
  const color::Rgba& color() const { return color_; }
  void setOnActivate(std::function<void()> callback) { onActivate_ = std::move(callback); }

 protected:
  void activate() override;

 private:
  color::Rgba color_{{0.5, 0.5, 0.5}, 1.0};
  std::function<void()> onActivate_;
};

}  // namespace r1ui::widgets
