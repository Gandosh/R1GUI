// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Segmented control widget: a row of mutually exclusive items (text, icon or both) with
//   one selected item, in the sizes sm (11 px text, 6 px item padding) and md (12 px, 8 px) of
//   docs/spec/widgets.md 2.5.
// Why: view switchers (File / Assets), alignment and mode pickers need one single-select control
//   with keyboard navigation; the whole control is one widget (one tab stop, roving selection)
//   rather than a group of buttons, so focus and state live in one place.
// Callers: application code, panels. Calls: WidgetObject (input), PaintContext (drawing).
// Look (measured): container = field fill `panel-field`, radius 4, 2 px padding and gap; items are
//   22 px high with radius 4, `muted` text; hover (and pressed) fill `hover` with `surface` text; the
//   selected item fills `panel-selected-muted` with `surface` text; disabled items are 50% opaque.
//   Fills change over 150 ms. Items share the width equally (as the reference's flex-1 items do); the
//   natural width is n x the widest item. Text that does not fit is shortened with an ellipsis.
// Behaviour: a click selects the item under the pointer when the press and release are on the same
//   enabled item. With keyboard focus Left/Up and Right/Down select the previous / next enabled item
//   (no wrap), Home / End the first / last; selection follows focus as in a radio group. Tab visits
//   the control once. The change callback receives the new index and runs for user changes only; it
//   may destroy the control. Per-item tooltips replace the control's tooltip over that item.
// Limits: at most kMaxItems items (boundary check); an invalid index is rejected and the previous
//   selection kept; selecting a disabled item programmatically is allowed (the app decides), the
//   user cannot.
#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

enum class SegmentedSize : uint8_t { Sm, Md };

struct SegmentItem {
  std::string text;
  std::string icon;     // optional Lucide / toolkit icon name, drawn before the text
  std::string tooltip;  // optional
  bool enabled = true;
};

class Segmented : public WidgetObject {
 public:
  static constexpr size_t kMaxItems = 64;

  explicit Segmented(SegmentedSize size = SegmentedSize::Md) : size_(size) {}

  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "Segmented"; }

  // ---- items and value ----
  // Replaces the items; false (nothing changed) when there are more than kMaxItems or an icon name
  // is not a plain file name. The selection is kept when still in range, otherwise cleared.
  bool setItems(std::vector<SegmentItem> items);
  const std::vector<SegmentItem>& items() const { return items_; }
  bool setItemEnabled(int index, bool enabled);
  int selectedIndex() const { return selected_; }  // -1 = none
  // Programmatic selection (no callback). -1 clears; out of range is rejected.
  bool setSelectedIndex(int index);
  void setOnChange(std::function<void(int)> callback) { onChange_ = std::move(callback); }
  void setSize(SegmentedSize size);
  SegmentedSize size() const { return size_; }
  // Index of the item at a logical x relative to the control's left edge, or -1.
  int itemAt(double localX) const;

  // ---- WidgetObject ----
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override;
  std::string_view tooltipText() const override;
  std::string_view accessibleName() const override;
  uint8_t styleState() const override;
  void onPointerMove(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onPointerDown(Event& e) override;
  void onPointerUp(Event& e) override;
  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;
  void onStateChanged(uint16_t previous) override;

 private:
  struct Layout {
    double padding = 2.0, gap = 2.0, itemH = 22.0, itemW = 0.0;
  };
  Layout layoutFor(double width) const;
  void selectByUser(int index);
  int stepEnabled(int from, int dir) const;

  std::vector<SegmentItem> items_;
  std::function<void(int)> onChange_;
  SegmentedSize size_;
  int selected_ = -1;
  int hover_ = -1;
  int pressedItem_ = -1;
};

}  // namespace r1ui::widgets
