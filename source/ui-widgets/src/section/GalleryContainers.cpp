// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GalleryContainers.h.
// Invariants: every instance sits in a Cell (a caption above the widget); the page is one wrapping row
//   of cells, so adding a variant is one function call; nothing here keeps state outside the widgets.
// Callers: the gallery preview, the gallery test.
#include "r1ui/widgets/section/GalleryContainers.h"

#include <memory>
#include <string>
#include <vector>

#include "r1ui/widgets/scroll/ScrollArea.h"
#include "r1ui/widgets/section/Section.h"
#include "r1ui/widgets/splitter/Splitter.h"
#include "r1ui/widgets/tabbar/TabBar.h"
#include "r1ui/widgets/toolbar/Toolbar.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace r1ui::widgets {

namespace layout = core::layout;
using core::tree::WidgetId;

namespace {

// A stand-in for a field: the `panel-field` fill, 26 px high.
class GalleryField : public WidgetObject {
 public:
  explicit GalleryField(double height = 26.0) : height_(height) {}
  const char* typeName() const override { return "GalleryField"; }
  void onAttached() override {
    style().height = layout::Length::px(height_);
    style().flexShrink = 0.0;
    node().flags.hitTestTransparent = true;
  }
  void paint(PaintContext& ctx) override { ctx.painter().fillRoundedRect(ctx.box(), render::CornerRadii::uniform(ctx.px(4.0)), ctx.color("panel-field")); }

 private:
  double height_;
};

// A pane or scroll row: a label inside a bordered area.
Label& note(UiContext& ui, WidgetId parent, std::string text) {
  Label& l = ui.create<Label>(parent, std::move(text), LabelRole::Muted);
  l.style().padding[layout::kLeft] = 8;
  l.style().padding[layout::kTop] = 6;
  return l;
}

// A caption above a widget; returns the container the widget goes into.
WidgetId cell(UiContext& ui, WidgetId page, const std::string& caption) {
  SectionBox& c = ui.create<SectionBox>(page);
  c.style().gapRow = 8;
  c.style().flexShrink = 0.0;
  ui.create<Label>(c.id(), caption, LabelRole::Caption);
  return c.id();
}

void sizeIt(core::layout::Style& s, double w, double h) {
  s.width = layout::Length::px(w);
  s.height = layout::Length::px(h);
  s.flexShrink = 0.0;
}

// ---- sections and panel parts -----------------------------------------------------------------

void buildSections(UiContext& ui, WidgetId page) {
  {
    const WidgetId c = cell(ui, page, "PanelHeader");
    PanelHeader& h = ui.create<PanelHeader>(c, "Rectangle", "square");
    h.style().width = layout::Length::px(258);
    h.addAction("shapes", "Create component", [](ActionButton&) {});
  }
  {
    const WidgetId c = cell(ui, page, "PropertySection with action, FieldGrid (two columns, then two and a rail)");
    PropertySection& s = ui.create<PropertySection>(c, "Appearance");
    s.style().width = layout::Length::px(258);
    s.addAction("eye", "Hide", [](ActionButton&) {});
    FieldGrid& grid = ui.create<FieldGrid>(s.content());
    const FieldGrid::Row row = grid.addRow(2, false);
    ui.create<GalleryField>(ui.create<FieldGroup>(row.first, "Blend mode").control());
    ui.create<GalleryField>(ui.create<FieldGroup>(row.second, "Opacity").control());
    const FieldGrid::Row railRow = grid.addRow(2, true);
    ui.create<GalleryField>(ui.create<FieldGroup>(railRow.first, "Radius").control());
    ui.create<GalleryField>(ui.create<FieldGroup>(railRow.second, "Smoothing").control());
    ActionButton& rail = ui.create<ActionButton>(railRow.rail, "square-round-corner", "section.action");
    rail.setSize(26, 26);
    rail.setTooltip("Independent corners");
  }
  {
    const WidgetId c = cell(ui, page, "PropertySection, collapsible: open and collapsed");
    for (const bool collapsed : {false, true}) {
      PropertySection& s = ui.create<PropertySection>(c, collapsed ? "Collapsed section" : "Open section", SectionOptions{.collapsible = true});
      s.style().width = layout::Length::px(258);
      ui.create<GalleryField>(s.content());
      s.setCollapsed(collapsed);
    }
  }
  {
    const WidgetId c = cell(ui, page, "Action buttons: idle, active, disabled, keyboard focus");
    SectionBox& row = ui.create<SectionBox>(c);
    row.style().direction = layout::FlexDirection::Row;
    row.style().gapColumn = 8;
    ActionButton& idle = ui.create<ActionButton>(row.id(), "plus", "section.action");
    idle.setSize(26, 26);
    ActionButton& active = ui.create<ActionButton>(row.id(), "eye", "section.action");
    active.setSize(26, 26);
    active.setActive(true);
    ActionButton& disabled = ui.create<ActionButton>(row.id(), "trash-2", "section.action");
    disabled.setSize(26, 26);
    disabled.setEnabled(false);
    ActionButton& focused = ui.create<ActionButton>(row.id(), "lock", "section.action");
    focused.setSize(26, 26);
    ui.router().focus(focused.id(), core::events::FocusReason::Keyboard);
  }
}

// ---- scroll ----------------------------------------------------------------------------------------

void buildScroll(UiContext& ui, WidgetId page) {
  {
    const WidgetId c = cell(ui, page, "ScrollArea: vertical, thin gutter scrollbar");
    ScrollArea& area = ui.create<ScrollArea>(c);
    sizeIt(area.style(), 220, 150);
    for (int i = 1; i <= 20; ++i) note(ui, area.content(), "Row " + std::to_string(i));
  }
  {
    const WidgetId c = cell(ui, page, "ScrollArea: both axes, overlay scrollbars");
    ScrollArea& area = ui.create<ScrollArea>(c, ScrollAxes::Both);
    sizeIt(area.style(), 220, 150);
    area.setScrollbarStyle(ScrollbarStyle::Overlay);
    area.setContentMinWidth(420);
    for (int i = 1; i <= 14; ++i) note(ui, area.content(), "A line that is wider than the viewport " + std::to_string(i));
  }
  {
    const WidgetId c = cell(ui, page, "Nested ScrollAreas (the wheel chains outwards)");
    ScrollArea& outer = ui.create<ScrollArea>(c);
    sizeIt(outer.style(), 220, 150);
    note(ui, outer.content(), "Outer area");
    ScrollArea& inner = ui.create<ScrollArea>(outer.content());
    sizeIt(inner.style(), 190, 70);
    for (int i = 1; i <= 6; ++i) note(ui, inner.content(), "Inner " + std::to_string(i));
    for (int i = 1; i <= 8; ++i) note(ui, outer.content(), "Outer " + std::to_string(i));
  }
}

// ---- splitter ---------------------------------------------------------------------------------------

void fillPane(UiContext& ui, WidgetId pane, const std::string& text) { note(ui, pane, text); }

void buildSplitters(UiContext& ui, WidgetId page) {
  {
    const WidgetId c = cell(ui, page, "Splitter, row: weights 1 : 2, a fixed pane, drag the handles");
    Splitter& s = ui.create<Splitter>(c);
    sizeIt(s.style(), 320, 90);
    fillPane(ui, s.addPane({.weight = 1}), "Pane A");
    fillPane(ui, s.addPane({.weight = 2}), "Pane B");
    fillPane(ui, s.addPane({.fixedSize = 60}), "Fixed");
  }
  {
    const WidgetId c = cell(ui, page, "Splitter, column, with a collapsed pane (explicit collapse / expand)");
    Splitter& s = ui.create<Splitter>(c, SplitOrientation::Column);
    sizeIt(s.style(), 320, 140);
    fillPane(ui, s.addPane({.collapsible = true, .startCollapsed = true}), "Collapsible");
    fillPane(ui, s.addPane(), "Main");
  }
  {
    const WidgetId c = cell(ui, page, "Splitter with keyboard resize (focused: arrows, PageUp / PageDown pick the handle)");
    Splitter& s = ui.create<Splitter>(c);
    sizeIt(s.style(), 320, 90);
    s.setKeyboardResize(true);
    fillPane(ui, s.addPane(), "Left");
    fillPane(ui, s.addPane(), "Middle");
    fillPane(ui, s.addPane(), "Right");
    ui.router().focus(s.id(), core::events::FocusReason::Keyboard);
  }
}

// ---- tab bar ----------------------------------------------------------------------------------------

void buildTabBars(UiContext& ui, WidgetId page) {
  {
    const WidgetId c = cell(ui, page, "TabBar: inactive, active, a long title and a tab without a close button");
    TabBar& bar = ui.create<TabBar>(c);
    bar.style().width = layout::Length::px(620);
    bar.addTab(1, "Untitled");
    bar.addTab(2, "Untitled");
    bar.addTab(3, "A document with a very long name that is cut off");
    bar.addTab(4, "Pinned", TabOptions{.icon = "pin", .closable = false});
    bar.setActiveTab(2);
  }
  {
    const WidgetId c = cell(ui, page, "TabBar overflow (60 px tabs, scroll arrows, all-tabs list)");
    TabBar& bar = ui.create<TabBar>(c);
    bar.style().width = layout::Length::px(420);
    for (TabId i = 1; i <= 14; ++i) bar.addTab(i, "Document " + std::to_string(i));
    bar.setActiveTab(5);
  }
}

// ---- toolbar ----------------------------------------------------------------------------------------

void fillTools(Toolbar& bar) {
  bar.addTool("select", "mouse-pointer", "Select (V)");
  bar.addToolGroup({{"frame", "frame", "Frame", "F"}, {"section", "layout-grid", "Section", "S"}});
  bar.addToolGroup({{"rect", "square", "Rectangle", "R"}, {"ellipse", "circle", "Ellipse", "O"}});
  bar.addTool("pen", "pen-tool", "Pen (P)");
  bar.addTool("text", "type", "Text (T)");
  bar.addTool("hand", "hand", "Hand (H)");
  bar.addSeparator();
  bar.addAction("components", "blocks", "Components", [](ToolbarButton&) {});
  bar.addAction("grid", "grid-3x3", "Grid", [](ToolbarButton&) {});
  bar.addAction("mcp", "server-cog", "MCP server", [](ToolbarButton&) {}).setBadge(true);
  bar.setActiveTool("select");
}

void buildToolbars(UiContext& ui, WidgetId page) {
  {
    const WidgetId c = cell(ui, page, "Toolbar, horizontal: active tool, flyout groups, separator, badge");
    fillTools(ui.create<Toolbar>(c));
  }
  {
    const WidgetId c = cell(ui, page, "Toolbar, vertical");
    Toolbar& bar = ui.create<Toolbar>(c, ToolbarOrientation::Vertical);
    bar.addTool("a", "mouse-pointer", "Select");
    bar.addToolGroup({{"b", "square", "Rectangle", "R"}, {"c", "circle", "Ellipse", "O"}});
    bar.addSeparator();
    bar.addAction("d", "plus", "Add", [](ToolbarButton&) {});
    bar.setActiveTool("a");
  }
  {
    const WidgetId c = cell(ui, page, "Toolbar: toggle on, toggle off, disabled button, keyboard focus");
    Toolbar& bar = ui.create<Toolbar>(c);
    bar.setKeyboardNavigation(true);
    bar.addToggle("t1", "moon", "Dark theme", [](ToolbarButton&) {}).setActive(true);
    bar.addToggle("t2", "sun", "Light theme", [](ToolbarButton&) {});
    bar.addAction("d", "trash-2", "Delete", [](ToolbarButton&) {}).setEnabled(false);
    ToolbarButton& focused = bar.addAction("f", "lock", "Lock", [](ToolbarButton&) {});
    ui.router().focus(focused.id(), core::events::FocusReason::Keyboard);
  }
}

// ---- tree -------------------------------------------------------------------------------------------

std::shared_ptr<SimpleTreeModel> layerModel() {
  auto m = std::make_shared<SimpleTreeModel>();
  NodeFlags component;
  component.component = true;
  NodeFlags hidden;
  hidden.hidden = true;
  NodeFlags locked;
  locked.locked = true;
  m->add(kTreeRoot, 1, "Frame", "frame");
  m->add(1, 2, "Rectangle", "square");
  m->add(1, 3, "Hidden rectangle", "square", hidden);
  m->add(1, 4, "Locked ellipse", "circle", locked);
  m->add(1, 5, "Inner frame", "frame");
  m->add(5, 6, "Rectangle", "square");
  m->add(5, 7, "Button component", "component", component);
  m->add(kTreeRoot, 8, "Group", "group");
  m->add(8, 9, "Text", "type");
  m->add(kTreeRoot, 10, "A layer with a very long name that does not fit the width of the tree", "square");
  return m;
}

void buildTrees(UiContext& ui, WidgetId page) {
  {
    const WidgetId c = cell(ui, page, "TreeView: selection without tree focus (muted), hidden, locked, component, truncated label");
    TreeView& tree = ui.create<TreeView>(c);
    sizeIt(tree.style(), 258, 250);
    tree.setModel(layerModel());
    tree.expandAll();
    tree.select(6);
    tree.setScrollbarPolicy(ScrollbarPolicy::Never);
  }
  {
    const WidgetId c = cell(ui, page, "TreeView with tree focus: multi selection (blue)");
    TreeView& tree = ui.create<TreeView>(c);
    sizeIt(tree.style(), 258, 250);
    tree.setModel(layerModel());
    tree.expandAll();
    tree.setSelection({2, 3, 9});
    tree.setScrollbarPolicy(ScrollbarPolicy::Never);
    ui.router().focus(tree.id(), core::events::FocusReason::Program);
  }
  {
    const WidgetId c = cell(ui, page, "TreeView: inline rename open (F2 or a slow second click)");
    TreeView& tree = ui.create<TreeView>(c);
    sizeIt(tree.style(), 258, 130);
    tree.setModel(layerModel());
    tree.setExpanded(1, true);
    tree.setScrollbarPolicy(ScrollbarPolicy::Never);
    tree.beginRename(2);
  }
  {
    const WidgetId c = cell(ui, page, "TreeView, list appearance, 2000 rows with the thin scrollbar");
    auto m = std::make_shared<SimpleTreeModel>();
    for (NodeId i = 1; i <= 2000; ++i) m->add(kTreeRoot, i, "Asset " + std::to_string(i), "file");
    TreeView& tree = ui.create<TreeView>(c);
    sizeIt(tree.style(), 258, 160);
    tree.setAppearance(TreeAppearance::List);
    tree.setModel(m);
    tree.select(3);
  }
  {
    const WidgetId c = cell(ui, page, "TreeView: empty, and disabled");
    TreeView& empty = ui.create<TreeView>(c);
    sizeIt(empty.style(), 258, 40);
    empty.setModel(std::make_shared<SimpleTreeModel>());
    TreeView& disabled = ui.create<TreeView>(c);
    sizeIt(disabled.style(), 258, 72);
    disabled.setModel(layerModel());
    disabled.select(1);
    disabled.setEnabled(false);
  }
}

}  // namespace

void buildGalleryContainers(UiContext& ui, WidgetId parent) {
  SectionBox& page = ui.create<SectionBox>(parent);
  page.style().direction = layout::FlexDirection::Row;
  page.style().wrap = layout::FlexWrap::Wrap;
  page.style().gapColumn = 24;
  page.style().gapRow = 24;
  page.style().padding[layout::kLeft] = page.style().padding[layout::kTop] = 16;
  page.style().padding[layout::kRight] = page.style().padding[layout::kBottom] = 16;
  page.style().alignItems = layout::Align::Start;
  page.style().alignContent = layout::AlignContent::Start;
  buildSections(ui, page.id());
  buildScroll(ui, page.id());
  buildSplitters(ui, page.id());
  buildTabBars(ui, page.id());
  buildToolbars(ui, page.id());
  buildTrees(ui, page.id());
}

}  // namespace r1ui::widgets
