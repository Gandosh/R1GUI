// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DockTabStrip, the tab strip of one dock region as ONE widget that lays out, paints and
//   hit-tests its tabs itself: equal-width tabs capped at the strip's maximum (spec 02 rule 9), the
//   60 px minimum with scroll arrows and an all-tabs list when they do not fit (decision D12),
//   activation on press, close button on hovered or front tabs (hidden for locked tabs, rule 12),
//   middle-click close, the right-button context menu request, tab dragging with the live gap for a
//   tab that is hovering this strip, tooltips with the full title, wheel scrolling and keyboard
//   (Left/Right/Home/End activate, Ctrl+W closes, Down moves into the panel, Escape cancels a drag).
// Why: DockHost draws a region's tab strip and decides what a gesture means; the strip only knows
//   its tabs. Tabs are rows of data, not widgets, so a region with hundreds of tabs costs one
//   widget. The strip does not reuse TabBar: TabBar is the 36 px document bar of the reference with
//   intra-bar reordering only, while a dock strip is 25 px (50 for application pages), must hand a
//   drag to the host so it can cross strips and windows, and has lock and gap states.
// Callers: DockAreaView creates and feeds one per stack; tests. Calls: IDockInteraction (intents),
//   FlyoutList (the all-tabs list), PaintContext.
// Drag: a left press on an unlocked tab captures the pointer; when the Router reports a drag
//   (strictly more than the 5 px threshold) the strip asks the host to begin. From then on every move
//   and the release go to the host, which owns the ghost, the drop zones and the result. The strip
//   shows the dragged tab as lifted (hidden) and, when the host says so, a gap at a slot.
// Invariants: all indices are validated against the tab list; a tab list change resets hover and
//   press state; text is only drawn through the text engine; the strip never changes the model.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/dock/DockTypes.h"
#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/dock/DockInteraction.h"
#include "r1ui/widgets/overlay/OverlayManager.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class DockTabStrip : public WidgetObject {
 public:
  static constexpr double kArrowWidth = 20.0;
  static constexpr double kListWidth = 24.0;
  static constexpr double kPadX = 8.0;
  static constexpr double kIconSize = 12.0;
  static constexpr double kGap = 6.0;
  static constexpr double kCloseSize = 16.0;
  static constexpr double kWheelStep = 40.0;
  static constexpr size_t kMaxTabs = 4096;

  static std::span<const theme::StyleRuleEntry> styleRows();

  const char* typeName() const override { return "DockTabStrip"; }
  void onAttached() override;
  void onDetached() override;
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
  void onDoubleClick(Event& e) override;
  void onDragStart(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;

  // ---- wiring (area view) ----
  void bind(IDockInteraction* host, uint32_t area, FloatId window);
  uint32_t area() const { return area_; }
  FloatId window() const { return window_; }

  // ---- model ----
  // Replaces the tabs (at most kMaxTabs; extra tabs are ignored). `active` indexes `tabs`; a bad
  // index clamps. The front tab is scrolled into view.
  void setTabs(std::vector<DockTabInfo> tabs, size_t active, const StripMetrics& metrics);
  size_t tabCount() const { return tabs_.size(); }
  const DockTabInfo& tab(size_t index) const { return tabs_.at(index); }
  std::optional<size_t> indexOf(dock::PanelId panel) const;
  dock::PanelId frontPanel() const { return active_ < tabs_.size() ? tabs_[active_].panel : 0; }

  // ---- drag visuals (set by the host while a tab drag runs) ----
  // The tab that is being dragged is not drawn and takes no room; 0 clears it.
  void setLifted(dock::PanelId panel);
  // Opens a gap among the remaining tabs at `slot` (0..remaining count); nullopt closes it.
  void setGap(std::optional<size_t> slot);
  // Ends a drag that the host cancelled or finished; releases pointer capture if it is ours.
  void clearDrag();
  bool dragging() const { return drag_.active; }

  // ---- geometry (absolute logical px; empty rectangles for stale indices) ----
  dock::Rect tabRect(size_t index) const;
  dock::Rect closeRect(size_t index) const;
  dock::Rect leftArrowRect() const;
  dock::Rect rightArrowRect() const;
  dock::Rect listButtonRect() const;
  // The tab under a point, ignoring the lifted one.
  std::optional<dock::PanelId> tabAt(double x, double y) const;
  // Slot (0..others) at which a dragged tab whose centre is at `centreX` would land among the other
  // tabs, counting as if the dragged tab were added to this strip (spec 02 rule 23).
  size_t slotAt(double centreX) const;
  // The rectangle of slot `slot` (where the dragged tab would land), for the drop preview.
  dock::Rect slotRect(size_t slot) const;
  bool overflowing() const;
  double scrollOffset() const { return scroll_; }
  bool scrollToTab(dock::PanelId panel);
  // Opens the all-tabs list (overflow button); false when there is no overflow.
  bool openTabList();
  double naturalTabWidth() const;

 private:
  enum class Part : uint8_t { None, Tab, Close, Left, Right, List, Empty };
  struct Hit {
    Part part = Part::None;
    size_t index = 0;
    friend bool operator==(const Hit&, const Hit&) = default;
  };
  struct Layout {
    double tabW = 0.0;
    double viewportX = 0.0;  // strip-relative
    double viewportW = 0.0;
    double contentW = 0.0;
    bool overflow = false;
    size_t visible = 0;  // tabs excluding the lifted one
    size_t slots = 0;    // visible plus the gap
  };
  struct Press {
    bool armed = false;
    size_t index = 0;
    double grabX = 0.0, grabY = 0.0;
    double sx = 0.0, sy = 0.0;
  };
  struct Drag {
    bool active = false;
  };

  Layout computeLayout(bool withGap) const;
  const Layout& layout() const;
  void invalidate();
  double tabLeft(size_t index) const;  // absolute x of a visible tab, or NaN-free 0 when hidden
  size_t visiblePosition(size_t index) const;
  Hit hitTest(double x, double y) const;
  void setHover(Hit hit);
  void clampScroll();
  bool isLifted(size_t index) const { return index < tabs_.size() && tabs_[index].panel == lifted_; }
  dock::Rect stripRect() const;

  IDockInteraction* host_ = nullptr;
  uint32_t area_ = 0;
  FloatId window_ = kMainWindow;
  std::vector<DockTabInfo> tabs_;
  size_t active_ = 0;
  StripMetrics metrics_;
  dock::PanelId lifted_ = 0;
  std::optional<size_t> gap_;
  double scroll_ = 0.0;
  Hit hover_;
  Hit pressHit_;
  Press press_;
  Drag drag_;
  std::optional<size_t> middlePress_;
  mutable Layout layout_;
  mutable bool layoutValid_ = false;
  mutable double layoutWidth_ = -1.0;
  mutable std::string tooltipScratch_;
  OverlayId listOverlay_;
};

}  // namespace r1ui::widgets
