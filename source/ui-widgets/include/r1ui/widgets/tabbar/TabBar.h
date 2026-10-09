// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: TabBar, the document tab bar of the reference (36 px bar on `canvas` with a 1 px bottom
//   border; tabs with a file icon, label and close button; active tab on `panel`; a trailing "new
//   tab" button) as ONE widget that lays out, paints and hit-tests its tabs itself: activation on
//   press, close button on hover / active tabs, middle-click close, drag reordering with a live gap
//   preview, keyboard (Left / Right / Home / End / Ctrl+W), tooltips with the full title, and the
//   overflow behaviour of decision D12.
// Why: documents come and go constantly and a tab bar may hold hundreds of them; tabs are rows of
//   data, not widgets, so adding one costs a struct and layout is one pass over widths. The look is
//   measured from the reference (docs/spec/widgets.md 4.1): tab = padding 12 + icon 12 + gap 6 +
//   label + gap 6 + close 16 + padding 12 + 1 px right border (a tab titled "Untitled" is 109 px),
//   max 192 px wide; inactive text `muted` (hover `surface`), active background `panel`.
// Callers: application shells (documents), the gallery. Calls: FlyoutList (the all-tabs dropdown),
//   PaintContext (text, icons, 150 ms colour and close-button opacity transitions).
// Overflow (D12): tabs shrink evenly down to a 60 px minimum; beyond that they keep 60 px and the
//   strip scrolls: scroll arrows at the left and right ends and an "all tabs" button open a list of
//   every tab; the new-tab button stays at the right end. The active tab is always scrolled into
//   view; the wheel scrolls the strip.
// Close: the close button, a middle click released over the same tab and Ctrl+W (focused bar) call
//   the close handler with the tab id; without a handler the tab is removed. Removing the active
//   tab activates its right neighbour, else the left one, and reports it through the activate
//   handler.
// Drag: a press activates the tab at once (spec 02 rule 1); once the Router reports a drag the tab
//   follows the pointer inside the strip while the others slide aside; release drops it at the
//   slot under it (reported through the reorder handler), Escape puts it back. Tearing a tab out of
//   the bar (docking) is not part of this widget.
// Invariants: tab ids are unique and non-zero-length titles are optional; every index and id
//   argument is validated; text is never modified, only drawn through the text engine (invalid
//   UTF-8 is handled there); handlers may destroy the bar.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/overlay/OverlayManager.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

using TabId = uint64_t;

struct TabOptions {
  std::string icon = "file";  // empty = no icon
  bool closable = true;
  std::string tooltip;        // empty = the title
};

struct TabInfo {
  TabId id = 0;
  std::string title;
  std::string icon;
  bool closable = true;
  std::string tooltip;
};

class TabBar : public WidgetObject {
 public:
  static constexpr double kHeight = 36.0;
  static constexpr double kMinTabWidth = 60.0;   // decision D12
  static constexpr double kMaxTabWidth = 192.0;
  static constexpr double kNewButtonWidth = 36.0;
  static constexpr double kOverflowButtonWidth = 24.0;
  static constexpr size_t kMaxTabs = 4096;

  static std::span<const theme::StyleRuleEntry> styleRows();
  TabBar() = default;

  const char* typeName() const override { return "TabBar"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override;
  std::string_view tooltipText() const override;
  std::string_view accessibleName() const override;
  void onPointerDown(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onPointerWheel(Event& e) override;
  void onClick(Event& e) override;
  void onDragStart(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;
  void onStateChanged(uint16_t previous) override;

  // ---- tabs ----
  // Appends or inserts a tab; false for a duplicate id, an out-of-range index or a full bar.
  bool addTab(TabId id, std::string title, TabOptions options = {});
  bool insertTab(size_t index, TabId id, std::string title, TabOptions options = {});
  // Removes the tab; the active tab moves as described in the header. False for an unknown id.
  bool removeTab(TabId id);
  bool moveTab(TabId id, size_t newIndex);
  bool setTabTitle(TabId id, std::string title);
  bool setTabIcon(TabId id, std::string icon);
  size_t tabCount() const { return tabs_.size(); }
  const TabInfo& tab(size_t index) const { return tabs_.at(index); }
  std::optional<size_t> indexOf(TabId id) const;
  // Makes the tab active (no callback); false for an unknown id.
  bool setActiveTab(TabId id);
  std::optional<TabId> activeTab() const { return active_; }

  // ---- configuration and callbacks ----
  void setShowNewButton(bool show);
  void setNewButtonTooltip(std::string text) { newTooltip_ = std::move(text); }
  void setOnActivate(std::function<void(TabId)> callback) { onActivate_ = std::move(callback); }
  void setOnCloseRequested(std::function<void(TabId)> callback) { onClose_ = std::move(callback); }
  void setOnNewTab(std::function<void()> callback) { onNew_ = std::move(callback); }
  void setOnReorder(std::function<void(TabId, size_t from, size_t to)> callback) { onReorder_ = std::move(callback); }
  void setOnContextMenu(std::function<void(TabId, double x, double y)> callback) { onContext_ = std::move(callback); }

  // ---- geometry (logical px, window coordinates; empty rectangles for stale ids) ----
  core::layout::Rect tabRect(TabId id) const;
  core::layout::Rect closeRect(TabId id) const;
  core::layout::Rect newButtonRect() const;
  core::layout::Rect leftArrowRect() const;
  core::layout::Rect rightArrowRect() const;
  core::layout::Rect listButtonRect() const;
  bool overflowing() const;
  double scrollOffset() const { return scroll_; }
  // Scrolls the strip so the tab is fully visible (no-op without overflow); false for an unknown id.
  bool scrollToTab(TabId id);
  // Opens the "all tabs" list (overflow button); returns whether it opened.
  bool openTabList();
  bool dragging() const { return drag_.active; }
  // Natural (unshrunk) width of a tab with this title, for tests and layouts.
  double naturalWidth(std::string_view title, bool closable, bool hasIcon) const;

 private:
  enum class Part : uint8_t { None, Tab, Close, New, Left, Right, List };
  struct Hit {
    Part part = Part::None;
    size_t index = 0;
  };
  struct Layout {
    std::vector<double> x;  // tab left edges in bar coordinates, scroll applied
    std::vector<double> w;
    double barWidth = 0.0;
    double stripX = 0.0, stripW = 0.0;  // the visible strip (clips tabs when overflowing)
    double contentW = 0.0;
    double newX = 0.0, leftX = 0.0, rightX = 0.0, listX = 0.0;
    bool overflow = false;
    bool hasNew = false;
  };
  struct Drag {
    bool armed = false;     // a left press on a tab
    bool active = false;
    size_t index = 0;
    size_t slot = 0;
    double grab = 0.0;      // pointer offset inside the tab at the press
    double pointerX = 0.0;
  };

  const Layout& layout() const;
  void invalidateLayout();
  Hit hitTest(double x, double y) const;
  core::layout::Rect barRect() const;
  void setHover(Hit hit);
  void activateByUser(size_t index);
  void requestClose(size_t index);
  size_t slotFor(double pointerX) const;
  void clampScroll();
  void endDrag(bool commit);
  std::string_view fullTitle(size_t index) const;

  std::vector<TabInfo> tabs_;
  std::optional<TabId> active_;
  bool showNew_ = true;
  std::string newTooltip_ = "New tab";
  mutable double scroll_ = 0.0;  // clamped by layout()
  mutable bool revealPending_ = false;  // scrollToTab was asked before the bar had a width
  Hit hover_;
  Hit pressHit_;
  Drag drag_;
  std::optional<size_t> middlePress_;
  mutable Layout layout_;
  mutable bool layoutValid_ = false;
  mutable double layoutScale_ = 0.0;
  mutable std::string tooltipScratch_;
  std::function<void(TabId)> onActivate_;
  std::function<void(TabId)> onClose_;
  std::function<void()> onNew_;
  std::function<void(TabId, size_t, size_t)> onReorder_;
  std::function<void(TabId, double, double)> onContext_;
  OverlayId listOverlay_;
};

}  // namespace r1ui::widgets
