// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the three pointer-and-keyboard colour controls of the picker: SvSquare (the saturation /
//   value square with its round handle), ColorSliderTrack (the 12 px hue or alpha track with its
//   14 px thumb) and ColorSliderRow (label + track + numeric entry, the 222 x 26 row measured in the
//   reference).
// Why: the three views of a colour (square, sliders, entries) share one interaction contract:
//   begin / change / end callbacks (so the host can group one gesture into one undo step), pointer
//   capture while dragging, keyboard nudging (arrows, Shift = ten times) and Escape to cancel a
//   drag by restoring the value the gesture started from.
// Callers: ColorPicker, GradientEditor (through the picker), tests. Calls: PickerDraw, PickerEntry.
// Geometry (logical px, measured in screen-color-picker-open and the widget-color-slider crops): the
//   square is as wide as its parent and 140 high with radius 4; a slider row is 26 high: a 16 px
//   label (10 px, weight 500, `muted`; wider text overflows under the track) + 8 gap + track
//   (12 px high, radius 6, fills the rest) + 8 gap + a 96 px value field. The thumb is a 14 px
//   circle with a 2 px white border and a small shadow; its centre travels over the track minus one
//   thumb radius at each end (thumb left = track left + t * (track width - 14)); the thumb overshoots
//   the track by 1 px above and below, so the track widget is 14 px high.
// Hue track: red, yellow, green, cyan, blue, magenta, red. Alpha track: the checkerboard (first
//   square `checkerboard`, second #cccccc, 4 px squares) under the colour at alpha 0 -> 1.
// Failure behavior: NaN or out-of-range values are clamped (hue wraps); a zero-size widget draws
//   nothing and ignores input.
#pragma once

#include <functional>
#include <string>

#include "r1ui/widgets/colorpicker/ColorModel.h"
#include "r1ui/widgets/colorpicker/PickerEntry.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

// ---- saturation / value square ----
class SvSquare : public WidgetObject {
 public:
  const char* typeName() const override { return "SvSquare"; }
  void onAttached() override;
  void onDetached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override { return Cursor::Pointer; }
  std::string_view accessibleName() const override;

  // Display state (no callbacks). Hue in degrees; s, v, alpha in 0..1; `fill` is the handle colour.
  void setDisplay(double hueDegrees, double s, double v, const color::Rgb& fill);
  double saturation() const { return s_; }
  double value() const { return v_; }

  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;

  std::function<void()> onBegin;
  std::function<void(double s, double v)> onChange;
  std::function<void()> onEnd;

 private:
  void update(double localX, double localY);
  void finish(bool cancel);

  double hue_ = 0.0;
  double s_ = 0.0;
  double v_ = 0.0;
  color::Rgb fill_;
  bool dragging_ = false;
  bool gestureOpen_ = false;  // onBegin was called and onEnd has not been (the widget may die in between)
  double startS_ = 0.0;
  double startV_ = 0.0;
};

// ---- hue / alpha track ----
class ColorSliderTrack : public WidgetObject {
 public:
  enum class Kind : uint8_t { Hue, Alpha };
  explicit ColorSliderTrack(Kind kind) : kind_(kind) {}

  const char* typeName() const override { return "ColorSliderTrack"; }
  void onAttached() override;
  void onDetached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override { return Cursor::Pointer; }
  std::string_view accessibleName() const override;

  Kind kind() const { return kind_; }
  // Position in 0..1 (hue / 360 for the hue track); NaN counts as 0. `base` is the colour the alpha
  // ramp fades in and the alpha thumb shows; the hue thumb shows the pure hue.
  void setPosition(double t);
  void setBase(const color::Rgb& base);
  double position() const { return t_; }

  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;

  std::function<void()> onBegin;
  std::function<void(double t)> onChange;
  std::function<void()> onEnd;

 private:
  double positionAt(double localX) const;
  double thumbCentreX(double widthLogical) const;
  void finish(bool cancel);

  Kind kind_;
  double t_ = 0.0;
  color::Rgb base_;
  bool dragging_ = false;
  bool gestureOpen_ = false;  // onBegin was called and onEnd has not been (the widget may die in between)
  double startT_ = 0.0;
};

// ---- label + track + entry ----
class ColorSliderRow : public WidgetObject {
 public:
  explicit ColorSliderRow(ColorSliderTrack::Kind kind) : kind_(kind) {}

  const char* typeName() const override { return "ColorSliderRow"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;

  // Display state (no callbacks): hue in degrees or alpha in 0..1, and the base colour.
  void setHue(double degrees);
  void setAlpha(double alpha);
  void setBase(const color::Rgb& base);
  ColorSliderTrack& track() const;
  PickerEntry& entry() const;

  // Hue in degrees [0, 360) or alpha 0..1, matching the kind.
  std::function<void()> onBegin;
  std::function<void(double value)> onChange;
  std::function<void()> onEnd;

 private:
  void refreshEntry();
  bool commitText(std::string_view text);

  ColorSliderTrack::Kind kind_;
  core::tree::WidgetId track_;
  core::tree::WidgetId entry_;
  double value_ = 0.0;  // hue degrees or alpha
};

}  // namespace r1ui::widgets
