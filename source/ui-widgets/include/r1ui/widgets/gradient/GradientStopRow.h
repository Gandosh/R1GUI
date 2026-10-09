// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GradientStopRow, one line of the stop list of the gradient editor (222 x 30): the position
//   field with `%`, a 16 px colour swatch, the hex field and the opacity field with `%`, and the
//   selected look (fill `hover` at 50 %, radius 4).
// Why: the list is the precise way to edit a stop (the bar is the quick one); each field validates
//   its own text and reports a parsed value, the editor applies it to the gradient.
// Callers: GradientEditor, tests. Calls: PickerEntry, NumberText, ColorModel.
// Geometry (logical px, from the gradient-stop-row crops): padding 2 vertical, gap 4: position field
//   62 wide, swatch 16, hex field 70 wide and 22 high (11 px text, fill `input`, 1 px border),
//   opacity field 62 wide; fields 26 high.
// Behaviour: a press anywhere in the row (also on its fields) or focusing a field selects the stop
//   (onSelect, the row does not consume the press); a field commit (Enter, focus loss) parses the
//   text: position and opacity as percentages clamped to 0..100, hex as #rgb / #rrggbb (an 8-digit
//   value also sets the opacity); invalid text reverts the field. Delete on the focused row (not in
//   a field) asks for the stop to be removed.
#pragma once

#include <functional>
#include <span>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/gradient/GradientModel.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class PickerEntry;

class GradientStopRow : public WidgetObject {
 public:
  explicit GradientStopRow(uint32_t stopId) : stopId_(stopId) {}
  static std::span<const theme::StyleRuleEntry> styleRows();

  const char* typeName() const override { return "GradientStopRow"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  uint8_t phases() const override { return core::events::kListenCapture | core::events::kListenTarget | core::events::kListenBubble; }
  std::string_view accessibleName() const override;

  uint32_t stopId() const { return stopId_; }
  // Display state (no callbacks).
  void setStop(const gradient::Stop& stop, bool selected);
  PickerEntry& positionEntry() const;
  PickerEntry& hexEntry() const;
  PickerEntry& opacityEntry() const;

  void onPointerDown(Event& e) override;
  void onKeyDown(Event& e) override;

  std::function<void(uint32_t id)> onSelect;
  std::function<void()> onBegin;
  std::function<void(uint32_t id, double position)> onPosition;
  std::function<void(uint32_t id, const color::Rgba& colour)> onColour;
  std::function<void()> onEnd;
  std::function<void(uint32_t id)> onRemove;

 private:
  bool commitPosition(std::string_view text);
  bool commitHex(std::string_view text);
  bool commitOpacity(std::string_view text);
  void runEdit(const std::function<void()>& edit);

  uint32_t stopId_;
  gradient::Stop stop_;
  core::tree::WidgetId position_, swatch_, hex_, opacity_;
};

}  // namespace r1ui::widgets
