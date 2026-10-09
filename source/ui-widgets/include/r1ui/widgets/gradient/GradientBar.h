// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GradientBar, the 24 px bar of the gradient editor: the gradient drawn left to right over a
//   checkerboard (radius 4) with the draggable stop handles (14 px squares, radius 4, a 2 px border
//   that is white for the selected stop and white at 60 % for the others, small shadow) centred on
//   their positions, and every edit made directly on the bar.
// Geometry: the widget box is the gradient plus kOverhang px on each side (the end handles are centred
//   on the gradient's edges and must stay inside the box to be hit); the gradient is 24 px high.
// Why: stops are edited where the user sees them; the bar owns the pointer and keyboard rules so the
//   editor only receives "the gradient is now this" with a begin / end bracket per gesture.
// Callers: GradientEditor, tests. Calls: PickerDraw, GradientModel.
// Interaction: pressing a handle selects it and starts dragging it (position follows the pointer,
//   clamped to the bar); pressing the bar between handles adds a stop there (its colour is what the
//   bar shows at that position), selects it and starts dragging it, so add + move is one gesture;
//   dragging a handle more than kRemoveDistance logical px above or below the bar and releasing
//   removes it (never below two stops; the handle is drawn faded while removal is pending); Escape
//   or capture loss restores the gradient the gesture started with. With keyboard focus: Left / Right
//   move the selected stop by 1 % (Shift 10 %), Home / End to 0 / 100 %, Delete / Backspace remove it,
//   PageUp / PageDown select the previous / next stop. Cursor: move over a handle, pointer on the bar.
// Value semantics: the bar keeps a copy of the gradient; every change is reported with the whole
//   new gradient (callbacks may destroy the bar).
#pragma once

#include <functional>

#include "r1ui/widgets/gradient/GradientModel.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class GradientBar : public WidgetObject {
 public:
  static constexpr double kHeight = 24.0;
  static constexpr double kHandle = 14.0;
  static constexpr double kRemoveDistance = 24.0;
  // The end handles overhang the bar by half their width, so the widget is that much wider on each
  // side than the gradient it shows (the host gives it a negative side margin to keep the bar aligned).
  static constexpr double kOverhang = kHandle / 2.0;

  const char* typeName() const override { return "GradientBar"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override;
  std::string_view accessibleName() const override;

  void setGradient(const gradient::Gradient& g);
  const gradient::Gradient& gradient() const { return gradient_; }
  void setSelectedId(uint32_t id);
  uint32_t selectedId() const { return selected_; }
  bool dragging() const { return drag_.active; }
  // Logical x of the centre of a stop's handle relative to the widget's left edge (tests).
  double handleCentreX(uint32_t id) const;

  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;

  std::function<void()> onBegin;
  std::function<void(const gradient::Gradient&)> onEdit;
  std::function<void()> onEnd;
  std::function<void(uint32_t id)> onSelect;

 private:
  struct Drag {
    bool active = false;
    uint32_t id = 0;
    bool removing = false;
    gradient::Gradient start;
    uint32_t startSelected = 0;
  };
  double trackWidth() const;               // width of the gradient itself, logical px
  double positionAt(double localX) const;  // 0..1 along the gradient for a widget-local x
  uint32_t handleAt(double localX, double localY) const;
  void select(uint32_t id);
  void moveTo(double localX, double localY);
  void finish(bool cancel);
  void emit();

  gradient::Gradient gradient_;
  uint32_t selected_ = 0;
  Drag drag_;
};

}  // namespace r1ui::widgets
