// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ToolbarEditor, the edit display of one toolbar (spec 06 rules 14 to 26 for toolbars, decisions
//   D3 and D4): a header with the size step (small, medium, large) and item gap controls and the
//   strip of the toolbar's items drawn like the toolbar, each with an eye badge; ToolbarEditStrip is
//   the strip.
// Why: in edit mode the user adds, removes (hides), reorders and sizes the buttons of a toolbar:
//   items are dragged inside the strip or dropped from the command palette with a vertical insertion
//   indicator, the eye hides and restores an item, the size step and the gap change every button at
//   once, and locked toolbars refuse every edit with a lock glyph and a tooltip that says why.
// Callers: CustomizableToolbar (switches it in for the real toolbar), the gallery, tests. Calls:
//   CustomizeController, CommandPicker, Segmented, Label.
// Cells: command buttons are size-step squares with the command's icon; a separator is a 4 px line cell;
//   a flyout group is a button with a chevron; a spacer is a flexible gap drawn as a dashed cell. Hidden
//   items stay in the strip, faded, with a crossed eye. Missing commands are drawn with the `danger`
//   colour. Orientation follows the toolbar (vertical toolbars stack the cells).
// Mouse: press on a cell arms a drag (Node payload); past the threshold the hub shows the ghost and asks
//   the strip for the indicator (before/after by the middle of the cell under the pointer, end when
//   past the last cell); click on the eye badge toggles visibility; right-click opens the context menu.
// Keyboard (focus on the strip): arrows move the cursor, Space toggles visibility, Delete removes a user
//   item or hides a built-in one, Alt+arrow moves the item, Insert opens the command picker (the
//   command is inserted after the cursor).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "r1ui/widgets/customize/CustomizeController.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class Segmented;

class ToolbarEditStrip final : public WidgetObject, public DragTarget {
 public:
  struct ItemView {
    std::string id;
    commands::customize::Kind kind = commands::customize::Kind::Command;
    std::string label, icon;
    bool visible = true, locked = false, user = false, missing = false, cursor = false;
    core::layout::RectD rect, eye;  // window coordinates
  };
  struct Indicator {
    bool active = false;
    core::layout::RectD line;
    commands::customize::Placement placement;
  };

  ToolbarEditStrip(CustomizeController& controller, std::string toolbarId) : controller_(controller), toolbarId_(std::move(toolbarId)) {}
  const char* typeName() const override { return "ToolbarEditStrip"; }
  void onAttached() override;
  void onDetached() override;
  void paint(PaintContext& ctx) override;
  Cursor cursor() const override;
  std::string_view tooltipText() const override { return tip_; }
  std::string_view accessibleName() const override { return "Toolbar editor"; }
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onClick(Event& e) override;
  void onDragStart(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;
  void onFocusIn(Event& e) override;
  void onFocusOut(Event& e) override;

  bool dragOver(const DragPayload& payload, double x, double y) override;
  void dragLeave() override;
  bool dragDrop(const DragPayload& payload, double x, double y) override;

  const std::string& toolbarId() const { return toolbarId_; }
  size_t itemCount() const { return cells_.size(); }
  ItemView item(size_t index) const;
  int itemIndex(const std::string& id) const;
  const std::string& cursorId() const { return cursor_; }
  void setCursor(const std::string& id);
  const Indicator& indicator() const { return indicator_; }
  bool locked() const { return locked_; }
  void addCommandAtCursor();
  bool openContextMenu(const std::string& id, double x, double y);
  MenuController& contextMenu() { return *contextMenu_; }
  void rebuild();

 private:
  struct Cell {
    std::string id, label, icon;
    commands::customize::Kind kind = commands::customize::Kind::Command;
    bool visible = true, locked = false, user = false, missing = false;
    double pos = 0.0, size = 0.0;  // along the main axis
  };
  struct Hit {
    bool eye = false;
    int index = -1;
  };
  struct Candidate {
    bool valid = false;
    commands::customize::Placement placement;
    core::layout::RectD line;
  };

  bool vertical() const { return vertical_; }
  core::layout::RectD cellRect(const Cell& c) const;
  core::layout::RectD eyeRect(const Cell& c) const;
  Hit hitAt(double x, double y) const;
  Candidate locate(const DragPayload& payload, double x, double y) const;
  void toggleHidden(const std::string& id);
  void removeOrHide(const std::string& id);
  void moveByKey(const std::string& id, int direction);
  void cancelDrag();

  CustomizeController& controller_;
  std::string toolbarId_;
  CustomizeController::ListenerId listener_ = 0;
  std::vector<Cell> cells_;
  bool vertical_ = false;
  bool locked_ = false;
  double step_ = 32.0;
  double gap_ = 2.0;
  std::string cursor_;
  Indicator indicator_;
  std::string tip_;
  Hit hover_;
  Hit pressed_;
  std::string armedId_;
  bool dragging_ = false;
  std::unique_ptr<MenuController> contextMenu_;
};

class ToolbarEditor final : public WidgetObject {
 public:
  ToolbarEditor(CustomizeController& controller, std::string toolbarId) : controller_(controller), toolbarId_(std::move(toolbarId)) {}
  const char* typeName() const override { return "ToolbarEditor"; }
  void onAttached() override;
  void onDetached() override;

  ToolbarEditStrip& strip() const;
  core::tree::WidgetId sizeControl() const { return size_; }
  core::tree::WidgetId gapControl() const { return gap_; }
  core::tree::WidgetId stripWidget() const { return strip_; }
  const std::string& toolbarId() const { return toolbarId_; }
  // The gap presets of the gap control, in px.
  static const std::vector<double>& gapPresets();

 private:
  void sync();

  CustomizeController& controller_;
  std::string toolbarId_;
  CustomizeController::ListenerId listener_ = 0;
  core::tree::WidgetId strip_;
  core::tree::WidgetId size_;
  core::tree::WidgetId gap_;
  core::tree::WidgetId title_;
};

}  // namespace r1ui::widgets
