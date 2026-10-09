// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DockHost, the widget that shows a DockLayout (the ui-dock model) as real widgets: one
//   DockAreaView per window (the main area inside the host, floating areas inside the windows an
//   IFloatingBackend creates), panel content created lazily from a PanelRegistry and kept alive
//   while the panel is open, tab dragging with ghost and drop zones across windows, splitter
//   resizing, the tab context menu, the editing commands and the change observer.
// Why: one object owns the model and every decision about it (guard register: the model has a
//   single writer), while the views only draw and report intents (IDockInteraction). This is the
//   toolkit's equivalent of the editor's docking manager and the piece the layout manager, the
//   command layer and the application talk to.
// Callers: the application (builds it, registers panels, sets the layout, binds commands to the
//   public methods), dock::LayoutManager (through ILayoutTarget), tests and the gallery. Calls:
//   dock::DockLayout, PanelRegistry, IFloatingBackend, MenuController, UiContext.
// Placement: create it as a child of the area where the dock should appear and let it fill that
//   area (flexGrow 1). Its rectangle is the main window's content rectangle; it registers itself
//   with the backend (setMainContent) when attached.
// Panels: contents are children of the region bodies. A panel moved between regions or windows of
//   the same UiContext keeps its content widget (reparented); across contexts (native windows) the
//   content is recreated from its factory. A closed panel's content is destroyed; reopening creates
//   it again.
// Threading: UI thread only. Re-entrancy: every public mutator may be called from event handlers,
//   from the change observer and from timers; none holds state across a callback it makes.
// Failure behavior: an operation the model refuses changes nothing and sets lastError(); a factory
//   that throws leaves that panel with an empty body and sets lastError(); a backend that cannot
//   create a window folds the panels back into closed memory.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "r1ui/dock/DockLayout.h"
#include "r1ui/dock/LayoutManager.h"
#include "r1ui/widgets/dock/DockAreaView.h"
#include "r1ui/widgets/dock/DockDragOverlay.h"
#include "r1ui/widgets/dock/DockInteraction.h"
#include "r1ui/widgets/dock/FloatingBackend.h"
#include "r1ui/widgets/dock/PanelRegistry.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

struct DockHostOptions {
  dock::DockConfig config;
  bool touch = false;               // decision D22: splitter hit bands are 9 px
  bool closeGroupsAnyKind = true;   // close others/left/right apply to every kind (spec 02 rule 40 limits them to documents and application pages)
  bool allowDocking = true;         // false: tabs only reorder inside their own strip
  bool floatWhenUnplaced = false;   // opening a panel with no slot or suggestion floats it (the reference editor) instead of using the default region
};

// What the change observer is told (all of them mean "the layout may need saving" except where noted).
enum class DockChange : uint8_t {
  Arrangement,  // tabs, regions or sizes changed
  Active,       // the front or active tab changed
  Window,       // a floating window moved, resized, maximized or changed monitor
  Lock,         // a lock flag changed
  DragStarted,  // a tab drag began: suspend deferred saving (spec 04 rule 9); not a layout change
  DragEnded     // the tab drag ended (the result is reported separately as Arrangement)
};

class DockHost : public WidgetObject, public dock::ILayoutTarget, private IDockInteraction, private IFloatingListener, private IPointerSink {
 public:
  DockHost(PanelRegistry& registry, IFloatingBackend& backend, DockHostOptions options = {});
  ~DockHost() override;

  const char* typeName() const override { return "DockHost"; }
  void onAttached() override;
  void onDetached() override;
  void onLayout() override;

  // ---- the layout ----
  // Replaces the layout. Panels that stay open keep their content; windows are rebuilt. A layout
  // built for other panels than the registry's is refused. Does not notify the observer.
  dock::Status setLayout(dock::DockLayout layout);
  // The registry gained, lost or renamed panels: rebuilds the model over the new panel list keeping
  // the arrangement (panels the layout never saw appear at their suggested position).
  dock::Status refreshPanels();
  const dock::DockLayout& layout() const { return *layout_; }
  // The default arrangement resetLayout() applies.
  void setDefaultLayout(std::function<dock::DockLayout()> provider) { defaultLayout_ = std::move(provider); }
  // The main window's saved state (maximized, monitor, scale) travels with the layout; the host
  // adds the main content rectangle.
  void setMainWindowState(const dock::WindowState& state);
  // Called after every change of the arrangement (see DockChange). May destroy the host.
  void setOnChanged(std::function<void(DockChange)> observer) { onChanged_ = std::move(observer); }

  // ---- commands (plain methods the command layer binds) ----
  bool closeActiveTab();
  bool nextTab();
  bool previousTab();
  bool floatActiveTab();
  // Applies the default layout (setDefaultLayout) without asking; LayoutManager::resetToDefault
  // asks first.
  bool resetLayout();
  bool closePanel(dock::PanelId panel);
  // Spec 02 rule 55 (see DockLayout::openPanel); makes the panel active.
  bool openPanel(dock::PanelId panel);
  bool activatePanel(dock::PanelId panel);
  // Return the number of tabs closed (tabs that refuse are skipped, rule 40).
  size_t closeOthers(dock::PanelId keep);
  size_t closeToLeft(dock::PanelId of);
  size_t closeToRight(dock::PanelId of);
  bool floatPanel(dock::PanelId panel);
  // Moves the panel's whole stack into one new floating window.
  bool moveStackToNewWindow(dock::PanelId panel);
  bool setPanelLocked(dock::PanelId panel, bool locked);
  bool togglePinned(dock::PanelId panel);
  bool toggleCollapsed(dock::PanelId panel);
  // Keyboard navigation between tab strips (spec 01); false when there is nothing to move to.
  bool focusNextRegion(bool backwards);
  bool focusActivePanel();

  // ---- state ----
  dock::PanelId activePanel() const override { return activePanel_; }
  bool dragging() const { return drag_.active; }
  const std::string& lastError() const { return lastError_; }
  // The content widget of an open panel (invalid before it was first shown).
  core::tree::WidgetId contentOf(dock::PanelId panel) const;
  // The view of an area and its window id (kMainWindow for the main area).
  DockAreaView* areaView(uint32_t area) const;
  std::optional<FloatId> windowOfArea(uint32_t area) const;
  // The tab strip showing `panel` as a tab, if any.
  DockTabStrip* stripOf(dock::PanelId panel) const;
  const DockHostOptions& options() const { return options_; }
  // The main content rectangle in screen coordinates (the host's own rectangle).
  dock::Rect mainContentRect() const { return mainRect(); }

  // ---- dock::ILayoutTarget ----
  const dock::DockLayout& currentLayout() const override { return *layout_; }
  dock::Status applyLayout(dock::DockLayout layout) override { return setLayout(std::move(layout)); }
  std::vector<dock::PanelInfo> panels() const override { return registry_.infos(); }
  dock::DockConfig config() const override { return options_.config; }

 private:
  // ---- IDockInteraction ----
  void tabActivated(dock::PanelId panel) override;
  void tabCloseRequested(dock::PanelId panel) override { closePanel(panel); }
  void tabContextMenu(DockTabStrip& strip, dock::PanelId panel, double x, double y) override;
  bool tabDragBegin(DockTabStrip& strip, dock::PanelId panel, dock::Point pointer, dock::Point grabOffset, dock::Point tabSize) override;
  void tabDragMove(DockTabStrip& strip, dock::Point pointer) override;
  void tabDragEnd(DockTabStrip& strip, dock::Point pointer, bool commit) override;
  bool cancelDragRequested() override;
  void stripBackgroundDoubleClick(DockTabStrip& strip) override;
  void focusPanelOfStrip(DockTabStrip& strip) override;
  void regionPressed(dock::PanelId frontPanel) override;
  bool handleDragBegin(const HandleDrag& handle, dock::Point pointer) override;
  void handleDragMove(dock::Point pointer) override;
  void handleDragEnd(bool commit) override;
  void handleNudge(const HandleDrag& handle, double delta) override;
  const dock::DockConfig& dockConfig() const override { return options_.config; }
  double handleHitBand() const override { return options_.touch ? options_.config.touchHandleBand : options_.config.handleThickness; }
  DockTabInfo tabInfo(dock::PanelId panel) const override;
  dock::PanelId liftedPanel() const override { return drag_.active ? drag_.panel : 0; }
  dock::Point screenPoint(FloatId window, dock::Point local) const override { return backend_.toScreen(window, local); }
  void mountContent(dock::PanelId panel, UiContext& ui, core::tree::WidgetId body, bool visible) override;
  void releaseBody(UiContext& ui, core::tree::WidgetId body) override;

  // ---- IFloatingListener ----
  void onFloatMoved(FloatId window, const dock::Rect& contentRect) override;
  void onFloatCloseRequested(FloatId window) override;
  void onFloatActivated(FloatId window) override;
  void onFloatLost(FloatId window) override;
  void onFloatScaleChanged(FloatId window, double scale) override;
  void onFloatMaximizedChanged(FloatId window, bool maximized) override;

  // ---- IPointerSink ----
  void onTrackedPointer(dock::Point screen, bool leftDown) override;

  // ---- model flow (DockHost.cpp) ----
  struct AreaRec {
    uint32_t area = 0;
    FloatId window = kMainWindow;
    UiContext* ui = nullptr;
    core::tree::WidgetId view;
  };
  struct ContentRec {
    UiContext* ui = nullptr;
    core::tree::WidgetId widget;
  };
  dock::Rect mainRect() const;
  void afterModelChanged(DockChange kind, bool fromBackend = false, bool notifyObserver = true);
  void createWindowsAndViews(bool fromBackend);
  void destroyObsoleteWindows();
  void enforceStacking();
  void relayout();
  void pruneContents();
  void captureWindowStates();
  void notify(DockChange kind);
  AreaRec* areaRecFor(uint32_t area);
  const AreaRec* areaRecForWindow(FloatId window) const;
  void setActivePanel(dock::PanelId panel);
  dock::PanelId frontOfStackHolding(dock::PanelId panel) const;
  void adoptContentsForRebuild();
  MenuController& menuFor(UiContext& ui);
  void closeMenus();
  bool commitOp(const dock::Status& status);
  size_t closeGroup(const std::vector<dock::PanelId>& victims, dock::PanelId survivor);
  dock::DropZone floatZoneNear(dock::PanelId panel) const;
  dock::Rect contentRectForFloat(const dock::Rect& ghost) const;
  bool closeWindowOfArea(uint32_t area);
  void teardownViews();

  // ---- drag (DockHostDrag.cpp) ----
  struct DragState {
    bool active = false;
    dock::PanelId panel = 0;
    core::tree::WidgetId sourceStrip;
    UiContext* sourceUi = nullptr;
    FloatId sourceWindow = kMainWindow;
    uint32_t sourceArea = 0;
    dock::Point grabOffset;
    dock::Point tabSize;
    dock::Point screen;
    std::optional<dock::DropZone> zone;
    FloatId hiddenWindow = 0;
    bool windowHidden = false;
    bool tracking = false;
    dock::PanelId hoverPanel = 0;
    UiContext* hoverUi = nullptr;
    uint32_t hoverTimer = 0;
    std::vector<dock::PanelId> activated;  // fronts changed by hovering, restored on Escape
    core::tree::WidgetId savedFocus;
    UiContext* overlayUi = nullptr;
    core::tree::WidgetId overlay;
  };
  struct ResizeState {
    bool active = false;
    HandleDrag handle;
    double grab = 0.0;
    std::optional<dock::DockLayout> snapshot;
  };
  size_t panelsInWindow(FloatId window) const;
  void refineJoinSlot(dock::DropZone& zone, dock::Point screen) const;
  void updateDrag(dock::Point screen);
  std::optional<dock::DropZone> zoneAt(dock::Point screen, std::optional<FloatId>& windowUnder);
  bool zoneAllowed(const dock::DropZone& zone) const;
  void refreshStripVisuals();
  void showVisual(const dock::DropZone* zone, dock::Point screen, std::optional<FloatId> windowUnder);
  DockDragOverlay* overlayFor(UiContext& ui);
  void clearVisuals();
  void updateHoverActivation(dock::Point screen, std::optional<FloatId> windowUnder);
  void cancelHoverTimer();
  void finishDrag(bool dropNow, dock::Point screen);
  void endDragState();

  PanelRegistry& registry_;
  IFloatingBackend& backend_;
  DockHostOptions options_;
  std::optional<dock::DockLayout> layout_;
  dock::LayoutResult result_;
  dock::Rect lastMain_;
  std::vector<AreaRec> areas_;
  std::vector<FloatId> obsolete_;
  std::unordered_map<dock::PanelId, ContentRec> contents_;
  std::vector<std::pair<UiContext*, std::unique_ptr<MenuController>>> menus_;
  core::tree::WidgetId holding_;
  dock::PanelId activePanel_ = 0;
  DragState drag_;
  ResizeState resize_;
  std::string lastError_;
  std::function<void(DockChange)> onChanged_;
  std::function<dock::DockLayout()> defaultLayout_;
  uint32_t registryRevision_ = 0;
  bool inRelayout_ = false;
  bool detached_ = false;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace r1ui::widgets
