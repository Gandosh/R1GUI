// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: SelectList, the popup content of a Select: rows (items with a check column, group labels,
//   separators), the optional filter row, hover and keyboard highlight, type-ahead, wheel and thumb
//   scrolling, and the thin scrollbar. It is one widget that paints every visible row itself (a list
//   of 100000 entries costs one widget and the visible rows).
// Why: the measured list (docs/spec/widgets.md 2.6, reference screen "select open"): rows 28 px high,
//   radius 4, text at 24 px from the row's left edge with the check at 6 px, hover and highlight
//   `hover`, the whole list scrolling above 224 px.
// Callers: Select creates it inside the overlay host and tells it its owner; tests drive it through
//   input. Calls: SelectModel (through the owner), LineEditor (filter row), FieldChrome rows.
// Units: logical pixels. Layout: the widget measures its natural size (widest label + 36 px, all rows
//   plus the filter row) and caps its height at 214 px (224 minus the host's padding and border); rows
//   scroll inside what remains below the filter row.
// Lifetime: choosing a row destroys this widget (the popup closes) from inside its own handler;
//   nothing touches the node after the owner is asked to choose.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/runtime/WidgetObject.h"
#include "r1ui/widgets/textinput/LineEditor.h"

namespace r1ui::widgets {

class Select;

class SelectList : public WidgetObject {
 public:
  explicit SelectList(core::tree::WidgetId owner);

  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "SelectList"; }

  // The highlighted entry (keyboard or hover), if any.
  std::optional<size_t> highlighted() const { return highlight_; }
  double scrollOffset() const { return scroll_; }
  // Absolute logical rectangle of the row of `entryIndex`, empty when it is not visible.
  core::layout::Rect rowRect(size_t entryIndex) const;
  double viewportHeight() const;
  // Feeds one typed character to type-ahead (used by the trigger when it opens on a keystroke).
  void typeAhead(char32_t codePoint);
  const std::string& filterText() const { return filter_.text(); }

  void onAttached() override;
  void onLayout() override;
  bool wantsContinuousFrames() const override { return focused() && searchable(); }
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  void paint(PaintContext& ctx) override;
  Cursor cursor() const override;
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerWheel(Event& e) override;
  void onClick(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;
  bool wantsTextInput() const override { return true; }
  void onTextInput(Event& e) override;
  void onFocusIn(Event& e) override;

 private:
  struct Metrics {
    double item = 28.0;
    double group = 24.0;
    double separator = 9.0;
    double search = 26.0;
  };

  Select* owner() const;
  bool searchable() const;
  Metrics metrics() const;
  void rebuildRows();
  double contentHeight() const { return rowTop_.empty() ? 0.0 : rowTop_.back(); }
  double maxScroll() const;
  void setScroll(double value);
  void ensureVisible(size_t entryIndex);
  std::optional<size_t> entryAt(double localY) const;  // y relative to the list top
  void applyFilterFromEditor();
  void moveHighlight(std::optional<size_t> next);
  void chooseHighlighted();
  bool thumbRect(core::layout::Rect& out) const;

  core::tree::WidgetId owner_;
  std::vector<double> rowTop_;  // prefix sums over visible rows, size visible + 1
  std::optional<size_t> highlight_;
  double scroll_ = 0.0;
  bool needsReveal_ = true;
  bool thumbDrag_ = false;
  bool thumbHover_ = false;
  double thumbGrab_ = 0.0;
  LineEditor filter_;
  bool filterDragging_ = false;
  std::string typeBuffer_;
  uint64_t typeAtMs_ = 0;
};

}  // namespace r1ui::widgets
