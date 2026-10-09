// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DockAreaView, the widgets of one dock area (the main area or one floating window's area):
//   for every stack of the area a DockTabStrip and a DockBody (the clipped region that holds the
//   front panel's content), the splitter handles between regions (drawn and hit-tested here, with
//   the 9 px touch band, the hover and drag colours of the Splitter widget, capture for the whole
//   drag and keyboard resize), the placeholder shown when the area is empty and the drop-zone
//   target rectangles the host asks about.
// Why: the model computes where everything goes (DockLayout::computeLayout); this widget turns that
//   result into real widgets inside whatever parent a window provides. It is the "nested dock host"
//   a floating window mounts in its content area: the DockHost owns the single model and every
//   decision, one DockAreaView per window shows its part of it.
// Callers: DockHost creates one per area (main: child of the host; floating: child of the content
//   widget the floating backend supplies). Calls: IDockInteraction (intents and queries),
//   DockTabStrip, UiContext.
// Positioning: children are absolutely positioned from the model's rectangles, which are in screen
//   coordinates; the view subtracts its area's origin. apply() is called with a fresh LayoutResult
//   after every model change and every size change; it reuses widgets by position, so a strip keeps
//   its scroll and hover while the layout settles. Panel content widgets are children of bodies
//   (created and moved by the host through mountContent), never of the view directly.
// Invariants: widget ids are re-checked before use; a handle drag owns the pointer capture and is
//   ended on every exit path (release, capture loss, view destruction).
#pragma once

#include <optional>
#include <span>
#include <vector>

#include "r1ui/dock/DockLayout.h"
#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/dock/DockInteraction.h"
#include "r1ui/widgets/dock/DockTabStrip.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

// The clipped region under a tab strip: it paints the panel background, clips its content and tells
// the host that a press landed in the region (spec 02 rule 3) without consuming it.
class DockBody : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "DockBody"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  uint8_t phases() const override;
  void onPointerDown(Event& e) override;
  void bind(IDockInteraction* host) { host_ = host; }
  void setFront(dock::PanelId panel) { front_ = panel; }
  dock::PanelId front() const { return front_; }

 private:
  IDockInteraction* host_ = nullptr;
  dock::PanelId front_ = 0;
};

class DockAreaView : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();

  DockAreaView(IDockInteraction& host, uint32_t area, FloatId window) : host_(&host), area_(area), window_(window) {}

  const char* typeName() const override { return "DockAreaView"; }
  void onAttached() override;
  void onDetached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override;
  uint8_t phases() const override;
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;

  uint32_t area() const { return area_; }
  FloatId window() const { return window_; }

  // Rebuilds the stack widgets and handles of this area from `layout` (see header comment).
  void apply(const dock::LayoutResult& layout);

  // One stack of the area.
  struct Unit {
    core::tree::WidgetId strip;
    core::tree::WidgetId body;
    dock::Path path;
    dock::PanelId front = 0;  // the panel shown in the body
    dock::Rect bodyRect;      // screen coordinates
    dock::Rect stripRect;
  };
  const std::vector<Unit>& units() const { return units_; }
  DockTabStrip* stripOf(const Unit& unit) const;
  // The strip showing `panel` as a tab, and its unit.
  const Unit* unitHolding(dock::PanelId panel) const;
  const Unit* unitFront(dock::PanelId panel) const;
  // The strip whose rectangle contains the window point, if any.
  const Unit* unitStripAt(double x, double y) const;
  const std::vector<dock::HandleLayout>& handles() const { return handles_; }
  bool showsEmptyHint() const { return emptyHint_; }
  // Handle currently dragged or hovered (index into handles()), or -1.
  int draggedHandle() const { return dragHandle_; }
  int hoveredHandle() const { return hoverHandle_; }
  // Index of the handle the keyboard operates on.
  size_t keyboardHandle() const { return keyHandle_; }

 private:
  struct HandleHit {
    int index = -1;
  };
  HandleHit hitHandle(double x, double y) const;
  HandleDrag dragOf(const dock::HandleLayout& h) const { return {h.handle.area, h.handle.path, h.handle.index, h.handle.axis}; }
  dock::Rect handleLocalRect(const dock::HandleLayout& h) const;
  void setHover(int index);

  IDockInteraction* host_;
  uint32_t area_;
  FloatId window_;
  std::vector<Unit> units_;
  std::vector<dock::HandleLayout> handles_;
  dock::Point origin_;  // the area's top-left in screen coordinates
  bool emptyHint_ = false;
  bool wasEmpty_ = false;
  int hoverHandle_ = -1;
  int dragHandle_ = -1;
  size_t keyHandle_ = 0;
};

}  // namespace r1ui::widgets
