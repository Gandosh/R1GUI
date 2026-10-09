// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: IDockInteraction, the narrow interface through which the dock's views (tab strips, area
//   views, bodies) report what the user did to the DockHost, which owns the model and every
//   decision.
// Why: views draw model data and translate pointer and key input into intents; they never edit
//   the layout. Keeping the intents in one interface lets the strip and area view be tested without
//   a host and keeps the host the single writer of the model.
// Callers: DockTabStrip, DockAreaView, DockBody (callers); DockHost (implementor).
// Coordinates: every point is in the logical coordinates of the reporting widget's UiContext (window
//   coordinates of the window the view lives in); the host converts to screen space with the
//   floating backend. Views identify themselves by area id and window id so the host can do that.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "r1ui/core/layout/Geometry.h"
#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/dock/DockLayout.h"
#include "r1ui/dock/DockTypes.h"
#include "r1ui/widgets/dock/FloatingBackend.h"

namespace r1ui::widgets {

// Widget rectangles are rounded to whole logical pixels (core::layout::Rect); the dock works in
// doubles, so everything in the dock widgets that measures or hit-tests uses dock::Rect.
inline dock::Rect toDockRect(const core::layout::Rect& r) {
  return {static_cast<double>(r.x), static_cast<double>(r.y), static_cast<double>(r.w), static_cast<double>(r.h)};
}

class DockTabStrip;
class DockAreaView;
class UiContext;

// What a tab strip shows for one tab.
struct DockTabInfo {
  dock::PanelId panel = 0;
  std::string title;
  std::string icon;
  bool closable = true;  // false hides the close button (not closable or locked)
  bool locked = false;
};

struct StripMetrics {
  double height = 25.0;
  double minTabWidth = 60.0;
  double maxTabWidth = 160.0;
};

// Which edit a keyboard or pointer gesture on a splitter handle asks for.
struct HandleDrag {
  uint32_t area = 0;
  dock::Path path;
  uint32_t index = 0;
  dock::Axis axis = dock::Axis::Row;
};

class IDockInteraction {
 public:
  virtual ~IDockInteraction() = default;

  // ---- tabs ----
  // A tab was pressed (left or right button): make it front and active at once (spec 02 rules 1-2).
  virtual void tabActivated(dock::PanelId panel) = 0;
  // Close button, middle click or Ctrl+W on a tab. The host decides (locked, canClose).
  virtual void tabCloseRequested(dock::PanelId panel) = 0;
  // Right button on a tab or on the empty strip (`panel` 0): open the context menu at window x, y.
  virtual void tabContextMenu(DockTabStrip& strip, dock::PanelId panel, double x, double y) = 0;
  // Tab drag (spec 02 rules 14-25). Begin returns false when the tab cannot be dragged (locked);
  // the strip then does nothing further. Positions are window coordinates of the strip's context.
  virtual bool tabDragBegin(DockTabStrip& strip, dock::PanelId panel, dock::Point pointer, dock::Point grabOffset, dock::Point tabSize) = 0;
  virtual void tabDragMove(DockTabStrip& strip, dock::Point pointer) = 0;
  // `commit` false = the capture was lost without a release (treated as a drop on nothing by the
  // host when focus is lost, see docs); true = left button released.
  virtual void tabDragEnd(DockTabStrip& strip, dock::Point pointer, bool commit) = 0;
  // Escape pressed in a strip: cancel a running tab drag. True when one was running (key used).
  virtual bool cancelDragRequested() = 0;
  // Double click on empty strip space (title bar of a floating window: maximize/restore).
  virtual void stripBackgroundDoubleClick(DockTabStrip& strip) = 0;
  // Down arrow in a strip: move keyboard focus into the front panel.
  virtual void focusPanelOfStrip(DockTabStrip& strip) = 0;
  // A press landed in a region body (any button, not consumed): its front tab becomes active.
  virtual void regionPressed(dock::PanelId frontPanel) = 0;

  // ---- splitter handles (area views) ----
  virtual bool handleDragBegin(const HandleDrag& handle, dock::Point pointer) = 0;
  virtual void handleDragMove(dock::Point pointer) = 0;
  virtual void handleDragEnd(bool commit) = 0;
  // Keyboard resize of a focused handle: positive moves towards the end.
  virtual void handleNudge(const HandleDrag& handle, double delta) = 0;

  // ---- what the views need to know ----
  virtual const dock::DockConfig& dockConfig() const = 0;
  virtual dock::PanelId activePanel() const = 0;
  virtual double handleHitBand() const = 0;
  virtual DockTabInfo tabInfo(dock::PanelId panel) const = 0;
  // The tab being dragged (hidden from its strip), or 0.
  virtual dock::PanelId liftedPanel() const = 0;
  // Screen coordinates of a point in a window's coordinates.
  virtual dock::Point screenPoint(FloatId window, dock::Point local) const = 0;
  // Makes the panel's content a child of `body` and shows (visible) or hides it. A visible panel
  // without content gets it from its factory; a hidden one without content is left alone.
  virtual void mountContent(dock::PanelId panel, UiContext& ui, core::tree::WidgetId body, bool visible) = 0;
  // A body is about to be destroyed: contents still inside it that can be kept are moved out first.
  virtual void releaseBody(UiContext& ui, core::tree::WidgetId body) = 0;
};

}  // namespace r1ui::widgets
