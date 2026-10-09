// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the canvas area of the composed screen: the document tab bar (two documents), the canvas
//   and the controls floating over it: the tool bar at the bottom centre (tools, flyout groups,
//   an action), the theme toggle in the bottom-right corner and the Variables / toast buttons in
//   the top-right corner.
// Why: tab bar, toolbar, buttons, popups and the canvas widget meet here; the floating controls
//   are absolutely placed pass-through boxes, so the canvas below keeps its pointer input.
// Callers: ComposedApp's constructor.
#include <string>
#include <vector>

#include "ComposedApp.h"
#include "ComposedUtil.h"
#include "PreviewParts.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/tabbar/TabBar.h"
#include "r1ui/widgets/toolbar/Toolbar.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace preview {

namespace layout = r1ui::core::layout;
using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;

void ComposedApp::buildCanvasArea(WidgetId pane) {
  TabBar& tabs = ui_.create<TabBar>(pane);
  ids_.tabBar = tabs.id();
  tabs.addTab(1, "Untitled");
  tabs.addTab(2, "Landing page");
  tabs.setActiveTab(1);
  tabs.setOnNewTab([this] {
    if (TabBar* bar = ui_.objectAs<TabBar>(ids_.tabBar)) {
      const TabId next = bar->tabCount() == 0 ? 1 : bar->tab(bar->tabCount() - 1).id + 1;
      bar->addTab(next, "Untitled " + std::to_string(next));
      bar->setActiveTab(next);
    }
  });

  SectionBox& host = build::flex(ui_, pane, false);
  build::grow(host.style());
  CanvasView& canvas = ui_.create<CanvasView>(host.id(), doc_);
  ids_.canvas = canvas.id();
  canvas.setOnSelect([this](bool selected) {
    if (TreeView* layers = ui_.objectAs<TreeView>(ids_.layers)) {
      if (selected) layers->select(kRectangleNode);
      else layers->clearSelection();
    }
  });
  canvas.setOnMoved([this] { refreshPanel(); });
  canvas.setOnContextMenu([this](double x, double y) { openContextMenuAt(x, y); });
  buildFloating(host.id());
}

void ComposedApp::buildFloating(WidgetId canvasHost) {
  // Bottom centre: the tool bar.
  PassThrough& bottom = ui_.create<PassThrough>(canvasHost);
  bottom.style().justifyContent = layout::Justify::End;
  bottom.style().alignItems = layout::Align::Center;
  build::pad(bottom.style(), 0, 0, 0, 16);
  Toolbar& tools = ui_.create<Toolbar>(bottom.id());
  tools.style().alignSelf = layout::Align::Center;
  ids_.canvasToolbar = tools.id();
  tools.addTool("move", "mouse-pointer", "Move");
  tools.addToolGroup({{"frame", "frame", "Frame", "F"}, {"group", "group", "Group", "Ctrl+G"}});
  tools.addToolGroup({{"rectangle", "square", "Rectangle", "R"}, {"ellipse", "circle", "Ellipse", "E"}, {"triangle", "triangle", "Triangle", ""}});
  tools.addTool("pen", "pen-tool", "Pen");
  tools.addTool("text", "type", "Text");
  tools.addTool("hand", "hand", "Hand");
  tools.addSeparator();
  tools.addAction("components", "blocks", "Components", [this](ToolbarButton&) { toast("Components are not part of this preview"); });
  tools.setActiveTool("move");
  tools.setOnTool([this](const std::string& id) { toast("Tool: " + id); });

  // Bottom right: the theme toggle.
  PassThrough& corner = ui_.create<PassThrough>(canvasHost);
  corner.style().justifyContent = layout::Justify::End;
  corner.style().alignItems = layout::Align::End;
  build::pad(corner.style(), 0, 0, 16, 16);
  Toolbar& theme = ui_.create<Toolbar>(corner.id());
  theme.style().alignSelf = layout::Align::End;
  ids_.themeToolbar = theme.id();
  theme.addTool("light", "sun", "Light theme");
  theme.addTool("dark", "moon", "Dark theme");
  theme.setOnTool([this](const std::string& id) {
    if (host_.setDarkTheme) host_.setDarkTheme(id == "dark");
  });
  syncTheme();

  // Top right: the dialog and toast triggers.
  PassThrough& top = ui_.create<PassThrough>(canvasHost);
  top.style().alignItems = layout::Align::End;
  build::pad(top.style(), 0, 12, 12, 0);
  SectionBox& row = build::flex(ui_, top.id(), true, 8);
  Button& variables = ui_.create<Button>(row.id(), "Variables", ButtonTone::Panel, ButtonSize::Sm);
  variables.setIcon("blocks");
  variables.setTooltip("Open the variables dialog");
  variables.setOnClick([this] { openVariablesDialog(); });
  ids_.variablesButton = variables.id();
  Button& toastButton = ui_.create<Button>(row.id(), "Show toast", ButtonTone::Panel, ButtonSize::Sm);
  toastButton.setTooltip("Show a notification");
  toastButton.setOnClick([this] { toast("Copied as node ID"); });
  ids_.toastButton = toastButton.id();
}

}  // namespace preview
