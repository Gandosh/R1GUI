// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: FreeFormPanel (a header and a canvas), FreeFormCanvas (the explicit panel kind of decision D4:
//   command buttons that live at pixel positions with their own sizes) and FreeFormButton (one such
//   command button).
// Why: decision D4: free pixel placement exists ONLY inside an explicit free-form panel, never in menus
//   or standard toolbars. Outside edit mode the buttons are ordinary command buttons: the command's
//   icon and label, enabled and checked state follow the command live, the tooltip is the command's
//   with the chord, a click runs the same guarded execution as the chord. In edit mode the user drags
//   a command in from the palette, moves buttons by dragging (a multi-selection moves together), resizes
//   them with eight handles, nudges with the arrow keys (Shift = 10 px), deletes with Delete, selects
//   several with a marquee, aligns them from the context menu, and toggles snap-to-grid (default off,
//   8 px grid); everything is clamped inside the panel and never smaller than 16 px.
// Callers: hosts and the gallery (one panel per free-form layout), tests. Calls: CustomizeController
//   (model, drag hub, sync), CommandPicker, Switch, Label, Button, MenuController.
// Model: the layout is Customization's FreeFormPanelLayout with this panel's id; geometry is changed only
//   through Customization (placeButton / setButtonRect / deleteButton / bringToFront / setPanelSnap), so
//   persistence, session revert and the "locked" rule apply. During a drag the canvas shows a provisional
//   rectangle (fitted by Customization::fitRect) and commits on release; Escape restores.
// Edit display: the canvas paints the edit view itself (hidden buttons stay, faded), selection outlines
//   and eight 8 px handles on the selected buttons, the marquee, and a dashed outline for a palette
//   drop. Outside edit mode the canvas has one FreeFormButton child per visible button.
// Selection (rules of spec 08): a press on an unselected button selects it alone; a press on a selected
//   one keeps the group (so it can be dragged) and collapses to it on release without a drag; Ctrl or
//   Shift toggles; a press on empty space starts a marquee (clears the selection unless Ctrl or Shift).
// Keyboard (canvas focused): arrows nudge, Shift+arrows nudge by 10 px (on a grid: by cells), Delete
//   deletes, Ctrl+A selects all, Escape clears the selection or cancels a drag, Insert opens the picker.
#pragma once

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "r1ui/widgets/button/Pressable.h"
#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/customize/CustomizeController.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/runtime/WidgetObject.h"
#include "r1ui/widgets/toolbar/Toolbar.h"

namespace r1ui::widgets {

class FreeFormCanvas;

// A command button of the panel (normal mode).
class FreeFormButton final : public Pressable {
 public:
  FreeFormButton(CustomizeController& controller, std::string nodeId, std::string commandId, std::string userLabel)
      : controller_(controller), nodeId_(std::move(nodeId)), commandId_(std::move(commandId)), userLabel_(std::move(userLabel)) {}
  // The button uses the toolbar's style rows.
  static std::span<const theme::StyleRuleEntry> styleRows() { return Toolbar::styleRows(); }
  const char* typeName() const override { return "FreeFormButton"; }
  void onAttached() override;
  float paintOpacity() const override;
  uint8_t styleState() const override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  std::string_view accessibleName() const override;
  const std::string& nodeId() const { return nodeId_; }
  const std::string& commandId() const { return commandId_; }
  // Re-reads the command: enabled, checked, visible, tooltip. Returns false when the command is gone.
  void refresh();

 protected:
  void activate() override;

 private:
  CustomizeController& controller_;
  std::string nodeId_, commandId_, userLabel_;
  bool toggle_ = false;
  std::string label_, icon_;
};

class FreeFormCanvas final : public WidgetObject, public DragTarget {
 public:
  static constexpr double kHandle = 8.0;
  static constexpr double kDefaultWidth = 72.0;
  static constexpr double kDefaultHeight = 32.0;

  struct ButtonView {
    std::string id, commandId, label;
    bool visible = true, selected = false, locked = false, missing = false, user = false;
    core::layout::RectD rect;  // window coordinates, including a drag in progress
  };

  FreeFormCanvas(CustomizeController& controller, std::string panelId) : controller_(controller), panelId_(std::move(panelId)) {}
  const char* typeName() const override { return "FreeFormCanvas"; }
  void onAttached() override;
  void onDetached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override;
  std::string_view tooltipText() const override { return tip_; }
  std::string_view accessibleName() const override { return "Free-form panel"; }
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;
  void onFocusIn(Event& e) override;
  void onFocusOut(Event& e) override;

  bool dragOver(const DragPayload& payload, double x, double y) override;
  void dragLeave() override;
  bool dragDrop(const DragPayload& payload, double x, double y) override;

  const std::string& panelId() const { return panelId_; }
  bool editing() const { return controller_.editMode(); }
  size_t buttonCount() const { return buttons_.size(); }
  ButtonView button(size_t index) const;
  int buttonIndex(const std::string& id) const;
  const std::vector<std::string>& selection() const { return selected_; }
  void select(const std::string& id, bool additive = false);
  void clearSelection();
  void selectAll();
  // The eight handles of a button in window coordinates, clockwise from the top-left corner.
  std::array<core::layout::RectD, 8> handles(const std::string& id) const;
  const std::optional<core::layout::RectD>& marquee() const { return marquee_; }
  const std::optional<core::layout::RectD>& dropPreview() const { return dropPreview_; }
  // Moves the selected buttons by (dx, dy) px (on a grid: by cells); the arrow keys call it.
  void nudge(int dx, int dy, bool large);
  void deleteSelection();
  // Aligns the selected buttons ('l' left, 'r' right, 't' top, 'b' bottom, 'h' centre horizontally,
  // 'v' centre vertically) to the selection's bounds.
  void alignSelection(char how);
  void addCommandAtCursor();
  bool openContextMenu(double x, double y);
  MenuController& contextMenu() { return *contextMenu_; }
  FreeFormButton* buttonWidget(const std::string& nodeId) const;
  void rebuild();

 private:
  enum class Mode : uint8_t { None, Move, Resize, Marquee };
  struct Btn {
    std::string id, commandId, label;
    bool visible = true, locked = false, missing = false, user = false;
    core::layout::RectD rect;  // panel coordinates
  };

  core::layout::RectD toWindow(const core::layout::RectD& r) const;
  core::layout::RectD current(const Btn& b) const;
  int hitButton(double lx, double ly) const;
  int hitHandle(double lx, double ly, std::string& id) const;
  bool isSelected(const std::string& id) const;
  void beginDrag(Mode mode, double lx, double ly);
  void updateDrag(double lx, double ly);
  void commitDrag();
  void abortDrag();
  void rebuildChildren();
  void refreshStates();
  void paintButton(PaintContext& ctx, const core::layout::RectD& rect, const Btn& b, bool hovered);

  CustomizeController& controller_;
  std::string panelId_;
  CustomizeController::ListenerId listener_ = 0;
  CommandUiSync::Attachment attachment_;
  double width_ = 0.0, height_ = 0.0;
  bool snap_ = false;
  double grid_ = 8.0;
  bool locked_ = false;
  std::vector<Btn> buttons_;
  std::vector<std::string> selected_;
  std::string tip_;
  int hover_ = -1;
  int hoverHandle_ = -1;
  Mode mode_ = Mode::None;
  int handle_ = -1;
  std::string pressedId_;
  bool collapseOnRelease_ = false;
  bool moved_ = false;
  double startX_ = 0.0, startY_ = 0.0;
  std::map<std::string, core::layout::RectD> original_;
  std::map<std::string, core::layout::RectD> preview_;
  std::optional<core::layout::RectD> marquee_;
  std::vector<std::string> marqueeBase_;
  std::optional<core::layout::RectD> dropPreview_;
  std::unique_ptr<MenuController> contextMenu_;
  double lastX_ = 8.0, lastY_ = 8.0;
};

class FreeFormPanel final : public WidgetObject {
 public:
  FreeFormPanel(CustomizeController& controller, std::string panelId) : controller_(controller), panelId_(std::move(panelId)) {}
  const char* typeName() const override { return "FreeFormPanel"; }
  void onAttached() override;
  void onDetached() override;

  FreeFormCanvas& canvas() const;
  core::tree::WidgetId canvasWidget() const { return canvas_; }
  core::tree::WidgetId header() const { return header_; }
  core::tree::WidgetId snapSwitch() const { return snap_; }
  const std::string& panelId() const { return panelId_; }

 private:
  void sync();

  CustomizeController& controller_;
  std::string panelId_;
  CustomizeController::ListenerId listener_ = 0;
  core::tree::WidgetId header_;
  core::tree::WidgetId canvas_;
  core::tree::WidgetId snap_;
  core::tree::WidgetId gridLabel_;
};

}  // namespace r1ui::widgets
