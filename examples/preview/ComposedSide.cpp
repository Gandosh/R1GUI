// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the layer column of the composed screen: the document title row with the sidebar toggle,
//   the File / Edit / View menu bar built from the Menu widgets, the File | Assets switch, the
//   pages list and the layers tree (rename, selection, drag reorder, visibility and lock actions),
//   and the command handler the menus share.
// Why: this is where menus, segmented control, tree view, icon buttons and tooltips first meet in
//   one tree; the layer model is a SimpleTreeModel and the drop handlers reorder it for real.
// Callers: ComposedApp's constructor. Invariants: the layer tree selection and the document's
//   `selected` flag agree after every selection change; the rectangle layer's label is the
//   document name.
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "ComposedApp.h"
#include "ComposedUtil.h"
#include "PreviewParts.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/iconbutton/IconButton.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/section/Section.h"
#include "r1ui/widgets/segmented/Segmented.h"
#include "r1ui/widgets/splitter/Splitter.h"
#include "r1ui/widgets/textinput/TextInput.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace preview {

namespace layout = r1ui::core::layout;
using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;

namespace {

NodeFlags plainFlags() { return {}; }

// The layers of the document: a frame holding the rectangle and an ellipse, a group, a loose shape.
std::shared_ptr<SimpleTreeModel> makeLayerModel() {
  auto model = std::make_shared<SimpleTreeModel>();
  model->add(kTreeRoot, 1, "Frame 1", "frame", plainFlags());
  model->add(kTreeRoot, 2, "Group 1", "group", plainFlags());
  model->add(1, kRectangleNode, "Rectangle 1", "square", plainFlags());
  model->add(1, 4, "Ellipse 1", "circle", plainFlags());
  model->add(2, 5, "Heading", "type", plainFlags());
  model->add(2, 6, "Divider", "minus", plainFlags());
  model->add(kTreeRoot, 7, "Rectangle 2", "square", plainFlags());
  return model;
}

std::shared_ptr<SimpleTreeModel> makePageModel() {
  auto model = std::make_shared<SimpleTreeModel>();
  model->add(kTreeRoot, 1, "Page 1", "file", plainFlags());
  model->add(kTreeRoot, 2, "Page 2", "file", plainFlags());
  return model;
}

bool isUnder(const SimpleTreeModel& model, NodeId node, NodeId ancestor) {
  for (NodeId n = node; n != kTreeRoot; n = model.parentOf(n)) {
    if (n == ancestor) return true;
  }
  return false;
}

// Moves the dragged nodes next to or into the target, keeping their order (indices count the
// children after the node was taken out, so a move down inside one parent shifts by one).
void dropNodes(SimpleTreeModel& model, const DropRequest& request) {
  size_t offset = 0;
  for (const NodeId node : request.nodes) {
    NodeId parent = request.target;
    size_t index = static_cast<size_t>(-1);
    if (request.zone != DropZone::Onto) {
      parent = model.parentOf(request.target);
      index = model.indexInParent(request.target) + (request.zone == DropZone::Below ? 1 : 0) + offset;
      if (model.parentOf(node) == parent && model.indexInParent(node) < index) --index;
    }
    if (model.move(node, parent, index)) ++offset;
  }
}

SectionBox& headerRow(UiContext& ui, WidgetId parent, const char* title, const char* addTip, std::function<void()> onAdd) {
  SectionBox& row = build::flex(ui, parent, true, 4);
  row.style().alignItems = layout::Align::Center;
  build::pad(row.style(), 12, 0, 8, 0);
  row.style().height = layout::Length::px(28);
  row.style().flexShrink = 0.0;
  ui.create<Label>(row.id(), title, LabelRole::Caption);
  SectionBox& spacer = build::flex(ui, row.id(), true);
  spacer.style().flexGrow = 1.0;
  IconButton& add = ui.create<IconButton>(row.id(), "plus", IconButtonSize::Sm);
  add.setTooltip(addTip);
  add.setOnClick(std::move(onAdd));
  return row;
}

}  // namespace

// ---- column -------------------------------------------------------------------------------------

void ComposedApp::buildLeftColumn(WidgetId pane) {
  Surface& column = ui_.create<Surface>(pane, "panel", Surface::kRight);
  column.style().direction = layout::FlexDirection::Column;
  build::grow(column.style());

  SectionBox& title = build::flex(ui_, column.id(), true, 8);
  title.style().alignItems = layout::Align::Center;
  title.style().height = layout::Length::px(40);
  title.style().flexShrink = 0.0;
  build::pad(title.style(), 12, 0, 8, 0);
  ui_.create<Label>(title.id(), "Untitled", LabelRole::Heading);
  SectionBox& filler = build::flex(ui_, title.id(), true);
  filler.style().flexGrow = 1.0;
  IconButton& sidebar = ui_.create<IconButton>(title.id(), "panel-left", IconButtonSize::Md);
  sidebar.setTooltip("Toggle sidebar");
  sidebar.setOnClick([this] {
    if (Splitter* s = ui_.objectAs<Splitter>(ids_.splitter)) s->toggleCollapse(0);
  });

  SectionBox& menus = build::flex(ui_, column.id(), true);
  menus.style().alignItems = layout::Align::Center;
  menus.style().height = layout::Length::px(32);
  menus.style().flexShrink = 0.0;
  build::pad(menus.style(), 6, 0, 6, 0);
  buildMenus(menus.id());

  SectionBox& tabs = build::flex(ui_, column.id(), true);
  tabs.style().flexShrink = 0.0;
  build::pad(tabs.style(), 8, 4, 8, 8);
  Segmented& fileAssets = ui_.create<Segmented>(tabs.id(), SegmentedSize::Md);
  fileAssets.setItems({{.text = "File"}, {.text = "Assets", .tooltip = "Shared assets"}});
  fileAssets.setSelectedIndex(0);
  build::grow(fileAssets.style());
  fileAssets.setOnChange([this](int index) { if (index == 1) toast("Assets are not part of this preview"); });

  headerRow(ui_, column.id(), "PAGES", "Add page", [this] { toast("Page added"); });
  SectionBox& pageHolder = build::flex(ui_, column.id(), false);
  pageHolder.style().flexShrink = 0.0;
  build::pad(pageHolder.style(), 4, 0, 4, 0);
  TreeView& pages = ui_.create<TreeView>(pageHolder.id());
  pages.setAppearance(TreeAppearance::List);
  pages.setSelectionMode(TreeSelectionMode::Single);
  pages.style().height = layout::Length::px(56);
  pages.style().flexShrink = 0.0;
  pages.setModel(makePageModel());
  pages.select(1);
  ids_.pages = pages.id();

  headerRow(ui_, column.id(), "LAYERS", "Add layer", [this] { toast("Layer added"); });
  SectionBox& layerHolder = build::flex(ui_, column.id(), false);
  build::grow(layerHolder.style());
  build::pad(layerHolder.style(), 4, 0, 4, 4);
  TreeView& layers = ui_.create<TreeView>(layerHolder.id());
  build::grow(layers.style());
  layerModel_ = makeLayerModel();
  layers.setModel(layerModel_);
  layers.setSelectionMode(TreeSelectionMode::Multi);
  layers.expandAll();
  layers.select(kRectangleNode);
  ids_.layers = layers.id();

  layers.setOnSelectionChanged([this](TreeView&) { layerSelectionChanged(); });
  layers.setOnRename([this](NodeId node, std::string_view name) -> RenameResult {
    if (name.size() > 64) return {false, "The name is too long"};
    if (node == kRectangleNode) renameRectangle(std::string(name));
    else layerModel_->setLabel(node, std::string(name));
    return {};
  });
  layers.setOnAction([this](NodeId node, RowAction action) {
    NodeFlags flags = layerModel_->flags(node);
    (action == RowAction::ToggleVisibility ? flags.hidden : flags.locked) ^= true;
    layerModel_->setFlags(node, flags);
  });
  layers.setDropHandlers(
      [this](const DropRequest& request) {
        if (request.zone == DropZone::Onto && request.target == kTreeRoot) return false;
        return std::none_of(request.nodes.begin(), request.nodes.end(), [&](NodeId n) { return isUnder(*layerModel_, request.target, n); });
      },
      [this](const DropRequest& request) { dropNodes(*layerModel_, request); });
  layers.setOnContextMenu([this](NodeId, double x, double y) { openContextMenuAt(x, y); });
}

void ComposedApp::layerSelectionChanged() {
  TreeView* layers = ui_.objectAs<TreeView>(ids_.layers);
  if (layers == nullptr) return;
  const bool selected = layers->isSelected(kRectangleNode);
  if (doc_.selected == selected) return;
  doc_.selected = selected;
  if (WidgetObject* canvas = ui_.object(ids_.canvas)) canvas->requestPaint();
}

// ---- menus --------------------------------------------------------------------------------------

void ComposedApp::buildMenus(WidgetId row) {
  MenuBar& bar = ui_.create<MenuBar>(row);
  ids_.menuBar = bar.id();
  const auto onCommand = [this](const MenuItemSpec& item) { commandRun(item.id); };

  MenuSpec file;
  file.onCommand = onCommand;
  file.items = {menuAction("file.new", "New", "Ctrl+N"),
                menuAction("file.open", "Open...", "Ctrl+O"),
                menuAction("file.save", "Save", "Ctrl+S"),
                menuSeparator(),
                menuAction("file.variables", "Variables..."),
                menuAction("file.toast", "Show a toast"),
                menuSeparator(),
                menuAction("file.quit", "Quit", "Esc")};
  bar.addMenu("File", std::move(file));

  MenuSpec edit;
  edit.onCommand = onCommand;
  MenuItemSpec undo = menuAction("edit.undo", "Undo", "Ctrl+Z");
  undo.enabled = false;
  edit.items = {undo, menuAction("edit.redo", "Redo", "Ctrl+Shift+Z"), menuSeparator(), menuAction("edit.cut", "Cut", "Ctrl+X"),
                menuAction("edit.copy", "Copy", "Ctrl+C"), menuAction("edit.paste", "Paste", "Ctrl+V"), menuSeparator(),
                menuAction("edit.selectall", "Select all", "Ctrl+A")};
  bar.addMenu("Edit", std::move(edit));

  MenuSpec view;
  view.onCommand = onCommand;
  std::vector<MenuItemSpec> zoom = {menuRadio("view.zoom50", "50%", zoomPercent_ == 50), menuRadio("view.zoom100", "100%", zoomPercent_ == 100),
                                    menuRadio("view.zoom200", "200%", zoomPercent_ == 200)};
  view.items = {menuCheck("view.rulers", "Rulers", showRulers_), menuCheck("view.snap", "Snap to grid", snapToGrid_), menuSeparator(),
                menuSubmenu("Zoom", std::move(zoom)), menuSeparator(), menuAction("view.theme", "Toggle theme", "T")};
  bar.addMenu("View", std::move(view));
}

void ComposedApp::commandRun(const std::string& id) {
  if (id == "file.variables") {
    openVariablesDialog();
  } else if (id == "file.toast") {
    toast("Saved to the cloud");
  } else if (id == "file.quit") {
    if (host_.quit) host_.quit();
  } else if (id == "view.theme") {
    if (host_.setDarkTheme && host_.isDark) host_.setDarkTheme(!host_.isDark());
    syncTheme();
  } else if (id == "view.rulers") {
    showRulers_ = !showRulers_;
  } else if (id == "view.snap") {
    snapToGrid_ = !snapToGrid_;
  } else if (id.rfind("view.zoom", 0) == 0) {
    zoomPercent_ = std::atoi(id.c_str() + 9);
    toast("Zoom " + std::to_string(zoomPercent_) + "%");
  } else {
    toast("Command: " + id);
  }
}

}  // namespace preview
