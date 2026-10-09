// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ComposedApp construction (the three-pane split of the Widgets mode), the document / theme
//   glue and the destructor that closes what is open. The panes' contents are built in
//   ComposedSide.cpp, ComposedCanvas.cpp and ComposedPanel.cpp; transient surfaces are in
//   ComposedOverlays.cpp.
// Callers: PreviewApp, tests/preview.
#include "ComposedApp.h"

#include "ComposedUtil.h"
#include "r1ui/widgets/splitter/Splitter.h"
#include "r1ui/widgets/toolbar/Toolbar.h"

namespace preview {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;

ComposedApp::ComposedApp(UiContext& ui, WidgetId parent, ComposedHost host) : ui_(ui), host_(std::move(host)) {
  tooltips_ = RichTooltips::install(ui_);
  contextMenu_ = std::make_unique<MenuController>(ui_);
  toasts_ = std::make_unique<ToastManager>(ui_);

  // Pane weights are the reference screen's widths at 1440 px (259 | 913 | 258 of the 1430 px left
  // after the two 5 px handles); the splitter keeps the proportions when the window resizes.
  Splitter& split = ui_.create<Splitter>(parent);
  build::grow(split.style());
  ids_.splitter = split.id();
  ids_.leftPane = split.addPane({.weight = 259.0, .minSize = 160.0, .collapsible = true});
  ids_.canvasPane = split.addPane({.weight = 913.0, .minSize = 240.0});
  ids_.panelPane = split.addPane({.weight = 258.0, .minSize = 220.0});
  buildLeftColumn(ids_.leftPane);
  buildCanvasArea(ids_.canvasPane);
  buildPanel(ids_.panelPane);
  refreshPanel();
}

ComposedApp::~ComposedApp() {
  // Nothing may stay open on a context that is about to lose the widgets the callbacks refer to.
  contextMenu_->close();
  toasts_->dismissAll();
  // The result callback of the dialog clears variables_, so close through copies of the handles.
  const PopoverHandle popover = fillPopover_;
  if (popover.valid()) closePopover(ui_, popover);
  const DialogHandle dialog = variables_;
  if (dialog.valid()) closeDialog(ui_, dialog);
}

void ComposedApp::syncTheme() {
  if (!host_.isDark) return;
  if (Toolbar* bar = ui_.objectAs<Toolbar>(ids_.themeToolbar)) bar->setActiveTool(host_.isDark() ? "dark" : "light");
}

}  // namespace preview
