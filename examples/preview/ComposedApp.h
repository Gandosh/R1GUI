// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ComposedApp, the Widgets mode: the real widget library composed into one editor screen laid
//   out like the reference screen-rectangle-selected: a layer column (document title, File / Edit /
//   View menu bar, File | Assets switch, pages and layers trees), the canvas area (document tab bar,
//   the canvas, the floating tool bar at the bottom centre, the theme toggle in the corner, a
//   Variables and a toast button) and the properties panel (header, Name, Position, Layout,
//   Appearance, Fill with a colour picker popover, Stroke, Effects, Export) inside a ScrollArea,
//   the three joined by Splitters. Everything is live: fields edit the AppDocument, the canvas
//   paints it, the layer tree names and selects it.
// Why: slice 4.17, the proof that widgets of five builders compose in one UiContext. It is also the
//   workload of the benchmark (Widgets mode) and of tests/preview.
// Callers: PreviewApp, Bench, tests/preview. Calls: ui-widgets only (plus the preview's parts).
// Files: ComposedApp.cpp (structure, document and theme glue), ComposedSide.cpp (layer column and menus),
//   ComposedCanvas.cpp (canvas area), ComposedPanel.cpp (properties panel), ComposedOverlays.cpp
//   (colour picker popover, dialog, context menu, toasts).
// Lifetime: owns only ids and the controllers that must outlive a call (menu controller, toast
//   manager, tooltips registry); widgets are owned by the UiContext. The UiContext must outlive it.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "AppDocument.h"
#include "CanvasView.h"
#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/dialog/Dialog.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/popover/Popover.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/toast/Toast.h"
#include "r1ui/widgets/tooltip/TooltipContent.h"
#include "r1ui/widgets/tree/TreeModel.h"

namespace preview {

// What the application shell lends the screen: the theme and the way out.
struct ComposedHost {
  std::function<void(bool dark)> setDarkTheme;
  std::function<bool()> isDark;
  std::function<void()> quit;
};

class ComposedApp {
 public:
  // Builds the screen as a child of `parent` (a flex container that fills the window below the
  // title bar). Throws what UiContext::create throws.
  ComposedApp(r1ui::widgets::UiContext& ui, r1ui::core::tree::WidgetId parent, ComposedHost host);
  ~ComposedApp();
  ComposedApp(const ComposedApp&) = delete;
  ComposedApp& operator=(const ComposedApp&) = delete;

  r1ui::widgets::UiContext& ui() { return ui_; }
  AppDocument& document() { return doc_; }

  // The host changed the theme (T key): the toggle in the canvas corner follows.
  void syncTheme();
  // Shows a toast at the top of the window.
  void toast(const std::string& text, r1ui::widgets::ToastTone tone = r1ui::widgets::ToastTone::Default);
  void openVariablesDialog();
  // Opens the colour picker popover under the fill swatch (a second call while open closes it).
  void toggleFillPicker();
  void openContextMenuAt(double x, double y);

  // Ids of the parts (tests and the shot renderer).
  struct Ids {
    r1ui::core::tree::WidgetId splitter, leftPane, canvasPane, panelPane, tabBar, canvas, canvasToolbar, themeToolbar;
    r1ui::core::tree::WidgetId layers, pages, panelScroll, panelHeader, nameInput, menuBar, fillSwatch, fillHex, fillOpacity;
    r1ui::core::tree::WidgetId x, y, rotation, width, height, opacity, radius, smoothing;
    r1ui::core::tree::WidgetId blendMode, cornerStyle, clipContent, variablesButton, toastButton;
  };
  const Ids& ids() const { return ids_; }

 private:
  using WidgetId = r1ui::core::tree::WidgetId;

  // ComposedSide.cpp
  void buildLeftColumn(WidgetId pane);
  void buildMenus(WidgetId row);
  void layerSelectionChanged();
  void commandRun(const std::string& id);
  // ComposedCanvas.cpp
  void buildCanvasArea(WidgetId pane);
  void buildFloating(WidgetId canvasHost);
  // ComposedPanel.cpp
  void buildPanel(WidgetId pane);
  void buildPositionSection(WidgetId panel);
  void buildLayoutSection(WidgetId panel);
  void buildAppearanceSection(WidgetId panel);
  void buildFillSection(WidgetId panel);
  void buildEmptySections(WidgetId panel);
  // Writes the document into every field that shows it (no callbacks fire for programmatic values).
  void refreshPanel();
  void rectangleEdited();
  void renameRectangle(const std::string& name);

  r1ui::widgets::UiContext& ui_;
  ComposedHost host_;
  AppDocument doc_;
  Ids ids_;
  std::shared_ptr<r1ui::widgets::SimpleTreeModel> layerModel_;
  std::shared_ptr<r1ui::widgets::RichTooltips> tooltips_;
  std::unique_ptr<r1ui::widgets::MenuController> contextMenu_;
  std::unique_ptr<r1ui::widgets::ToastManager> toasts_;
  r1ui::widgets::PopoverHandle fillPopover_;
  r1ui::widgets::DialogHandle variables_;
  bool snapToGrid_ = true;
  bool showRulers_ = true;
  int zoomPercent_ = 100;
};

// The node id of the document rectangle in the layer tree.
inline constexpr r1ui::widgets::NodeId kRectangleNode = 3;

}  // namespace preview
