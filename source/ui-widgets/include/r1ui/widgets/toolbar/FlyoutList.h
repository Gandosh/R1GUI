// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: FlyoutList, a compact popup list of command rows (optional leading icon, 12 px label,
//   optional right-aligned 11 px shortcut text, optional check mark, separators) with hover and
//   keyboard highlight, and openFlyout(), which shows it in an overlay next to an anchor.
// Why: the toolbar's tool-group flyout and the tab bar's "all tabs" dropdown are the same control:
//   a column of 28 px rows inside the menu surface (measured: row 28 px high, padding 8 x 6,
//   radius 6, gap 24 between icon, label and shortcut, hover fill `hover`). The toolkit-wide Menu
//   widget is built elsewhere; this list is private to the toolbar and tab bar and has no
//   submenus, type-to-search or customization.
// Callers: Toolbar, TabBar. Calls: OverlayManager (the surface, placement, dismissal and focus
//   restore come from there).
// Behaviour: rows are not drawn as widgets (one widget, any number of rows). Up / Down move the
//   highlight without wrapping and skip separators and disabled rows, Home / End jump, Enter and
//   Space pick, a click picks the row under the pointer; a pick closes the overlay first and then
//   calls the callback (spec 01 rule 42). Escape and outside presses are handled by the overlay.
// Style rows: flyout.item (label colours, radius, hover fill), flyout.shortcut, flyout.separator.
// Invariants: item counts are bounded (kMaxItems); text is drawn through the text engine, so invalid
//   UTF-8 or control characters cannot break layout.
#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/overlay/OverlayManager.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

struct FlyoutItem {
  std::string label;
  std::string icon;       // icon name drawn 12 px before the label; empty = none
  std::string shortcut;   // right-aligned hint text
  bool checked = false;   // draws a check mark in the icon column when there is no icon
  bool enabled = true;
  bool separator = false; // a 1 px line; the other fields are ignored
};

class FlyoutList : public WidgetObject {
 public:
  static constexpr size_t kMaxItems = 4096;
  static constexpr double kRowHeight = 28.0;
  static constexpr double kPadX = 8.0;
  static constexpr double kGap = 24.0;
  static constexpr double kSeparatorHeight = 9.0;  // 1 px line with 4 px above and below

  static std::span<const theme::StyleRuleEntry> styleRows();

  FlyoutList(std::vector<FlyoutItem> items, std::function<void(size_t)> onPick);
  const char* typeName() const override { return "FlyoutList"; }
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  void paint(PaintContext& ctx) override;
  Cursor cursor() const override { return Cursor::Default; }
  void onPointerMove(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;

  size_t itemCount() const { return items_.size(); }
  int highlighted() const { return highlight_; }
  void setHighlighted(int index);
  // Row rectangle in the widget's own coordinates (logical px); empty for a bad index.
  core::layout::Rect rowRect(size_t index) const;
  // Picks `index` as a click would; false for a separator, a disabled row or a bad index.
  bool pick(size_t index);
  // Called when the list is closed by a pick (set by openFlyout to close the overlay first).
  void setCloseHook(std::function<void()> hook) { close_ = std::move(hook); }

 private:
  int rowAt(double localY) const;
  int step(int from, int direction) const;
  double rowTop(size_t index) const;

  std::vector<FlyoutItem> items_;
  std::function<void(size_t)> onPick_;
  std::function<void()> close_;
  int highlight_ = -1;
};

struct FlyoutOpenOptions {
  core::layout::Rect anchor;  // logical window coordinates
  Placement placement = Placement::BelowStart;
  double gap = 4.0;
  core::tree::WidgetId anchorWidget;
  int initialHighlight = -1;
  std::function<void(DismissReason)> onClosed;
};

// Shows `items` in a menu-surface overlay; returns its handle (invalid when the list is empty).
OverlayHandle openFlyout(UiContext& ui, std::vector<FlyoutItem> items, std::function<void(size_t)> onPick, const FlyoutOpenOptions& options);

}  // namespace r1ui::widgets
