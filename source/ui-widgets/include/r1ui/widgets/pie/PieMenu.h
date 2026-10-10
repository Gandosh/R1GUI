// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PieMenu, the widget that draws a pie menu: a round backdrop, up to eight slots on a ring around
//   its centre (icon and label in a pill, or a hollow dot for an empty slot), the dead-zone ring at the
//   centre and, for the highlighted slot, a pill in the accent colour with a line from the centre.
// Why: owner requirement 2026-10-10: a radial menu of up to 8 slots centred on the pointer, the slot
//   under the pointer highlighted, empty slots dim. The widget only draws and answers geometry questions;
//   which slot is highlighted is decided by PieGesture, what a slot does by the command layer.
// Callers: PieTrigger (puts it in an overlay while the right button is held), the gallery and visual
//   tests (a static pie). Calls: PaintContext, the style rows pie.* below.
// Geometry: the widget is a square of extent() logical pixels centred on the pie; slot i sits at
//   pieSlotOffset(slotCount, i) from the centre (slot 0 up, clockwise). Pointer events pass through (the
//   widget is hit-test transparent): the press owner keeps the pointer.
// Look: all colours come from style rows (pie.backdrop, pie.slot with the selected/disabled states,
//   pie.empty, pie.center); the same rows serve both themes.
// Failure behavior: the slot list is cut to eight; a bad highlight index means "none"; nothing throws.
#pragma once

#include <span>
#include <string>
#include <vector>

#include "r1ui/core/layout/Geometry.h"
#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/pie/PieGesture.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

// What one slot looks like. `filled` = the slot holds a command; `selectable` = it can be chosen right
// now (the command exists and is enabled). A filled slot that is not selectable is drawn dim.
struct PieSlotView {
  std::string label;
  std::string icon;
  bool filled = false;
  bool selectable = false;
  bool checked = false;  // a toggle that is on, drawn with an accent outline
  bool missing = false;  // the command does not exist (any more)
};

class PieMenu : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();

  static constexpr double kSlotWidth = 100.0;
  static constexpr double kSlotHeight = 30.0;
  static constexpr double kBackdropRadius = kPieSlotRadius + 58.0;

  explicit PieMenu(std::vector<PieSlotView> slots, double deadZone = PieGestureConfig{}.deadZone);

  const char* typeName() const override { return "PieMenu"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;

  // Square side of the widget in logical pixels.
  static double extent() { return 2.0 * kBackdropRadius; }

  int slotCount() const { return static_cast<int>(slots_.size()); }
  const std::vector<PieSlotView>& slots() const { return slots_; }
  // -1 = none; an index outside the slots also means none.
  void setHighlight(int slot);
  int highlight() const { return highlight_; }
  // Rectangle of slot `index` in widget-local logical pixels (empty for a bad index).
  core::layout::RectD slotRect(int index) const;

 private:
  std::vector<PieSlotView> slots_;
  double deadZone_;
  int highlight_ = -1;
};

}  // namespace r1ui::widgets
