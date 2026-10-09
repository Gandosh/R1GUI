// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: MenuEditor, the edit display of the menu bar (spec 06 rules 14 to 32 and the customization
//   panel of rules 27 to 31): a strip of menu titles and, below it, the selected menu drawn as an
//   editable list of sections and entries with a drag handle at the left and an eye (visibility
//   toggle) at the right of every row.
// Why: edit mode shows the menu with hidden entries still visible (faded), lets the user drag
//   entries and whole sections to a new place with an insertion indicator, drop a command dragged
//   from the palette, toggle visibility, rename in place, add entries by a context menu or the keyboard
//   (the command picker), create and delete user menus, and shows a lock glyph on locked menus with a
//   tooltip that says why an edit is refused (decisions D1 to D5).
// Callers: CustomizableMenuBar (switches it in for the MenuBar while editing), the gallery, tests.
//   Calls: CustomizeController (model, drag hub, refusal text), CommandPicker, RenameField, MenuController
//   (context menu).
// Rows: a Section row (its heading or a faint divider) then its entries; Sub-menu entries are followed by
//   their own sections, indented. A row has a handle zone (not on locked rows), a label zone and an eye
//   zone (a lock glyph on locked rows). Title tabs carry a lock glyph (locked) and an eye; the "New
//   menu" button follows the last tab.
// Mouse: press on a handle arms a drag (Node payload) and selects the row; past the drag threshold the
//   hub shows the ghost and asks this display (a DragTarget) for the indicator; the eye toggles on
//   click; double-click on a label starts the in-place rename; right-click opens the row's context
//   menu; a press on a title selects that menu, dragging a title reorders menus, dropping an entry on a
//   title moves it to the end of that menu.
// Keyboard (focus on the display): Up/Down/Home/End move the row cursor, Left/Right switch menu,
//   Space toggles visibility, F2 or Enter renames, Delete removes a user entry or hides a built-in one,
//   Alt+Up / Alt+Down move the entry, Insert opens the command picker and inserts after the cursor.
// Drop rules: the target position comes from the pointer: the upper half of an entry row is "before",
//   the lower half "after"; over a section row it is the start of that section; a dragged section snaps
//   to the section under the pointer; below the last row it is the end of the menu. The indicator is
//   shown only where the model accepts the change (Customization::preview).
// Look: panel surface with a border; hover and the cursor row use `hover`; hidden rows are drawn at
//   45% strength; entries whose command is missing use the `danger` colour and say so; the indicator is
//   a 2 px `accent` line.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "r1ui/widgets/customize/CustomizeController.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class MenuEditor final : public WidgetObject, public DragTarget {
 public:
  static constexpr double kTitleHeight = 30.0;
  static constexpr double kEntryHeight = 28.0;
  static constexpr double kHeadingHeight = 24.0;
  static constexpr double kSectionHeight = 22.0;
  static constexpr double kDividerHeight = 16.0;
  static constexpr double kIndent = 16.0;

  struct TitleView {
    std::string id;
    std::string label;
    bool visible = true, locked = false, user = false, selected = false;
    core::layout::RectD rect, eye;  // window coordinates
  };
  struct RowView {
    std::string id;
    commands::customize::Kind kind = commands::customize::Kind::Command;
    int depth = 0;
    std::string label, shortcut;
    bool visible = true, locked = false, user = false, missing = false, cursor = false;
    core::layout::RectD rect, handle, eye, text;  // window coordinates
  };
  struct Indicator {
    bool active = false;
    core::layout::RectD line;              // the 2 px line (a vertical one over the title strip)
    commands::customize::Placement placement;
    int title = -1;                        // a title tab highlighted as the drop target
  };

  explicit MenuEditor(CustomizeController& controller) : controller_(controller) {}
  ~MenuEditor() override;
  const char* typeName() const override { return "MenuEditor"; }
  void onAttached() override;
  void onDetached() override;
  void paint(PaintContext& ctx) override;
  Cursor cursor() const override;
  std::string_view tooltipText() const override { return tip_; }
  std::string_view accessibleName() const override { return "Menu editor"; }
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onClick(Event& e) override;
  void onDoubleClick(Event& e) override;
  void onDragStart(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;
  void onFocusIn(Event& e) override;
  void onFocusOut(Event& e) override;

  // DragTarget
  bool dragOver(const DragPayload& payload, double x, double y) override;
  void dragLeave() override;
  bool dragDrop(const DragPayload& payload, double x, double y) override;

  // ---- inspection and programmatic control (tests, the host) -----------------------------------
  size_t titleCount() const { return titles_.size(); }
  TitleView title(size_t index) const;
  const std::string& currentMenu() const { return current_; }
  bool setCurrentMenu(const std::string& id);
  size_t rowCount() const { return rows_.size(); }
  RowView row(size_t index) const;
  int rowIndex(const std::string& id) const;
  const std::string& cursorId() const { return cursor_; }
  void setCursor(const std::string& id);
  const Indicator& indicator() const { return indicator_; }
  core::layout::RectD newMenuRect() const;
  bool renaming() const { return rename_.valid(); }
  const std::string& renamingId() const { return renameTarget_; }
  core::tree::WidgetId renameField() const { return rename_; }
  // Starts the in-place rename of a row or a menu title; false when the entry cannot be renamed.
  bool beginRename(const std::string& id);
  // Opens the command picker; the chosen command is inserted after the cursor row.
  void addCommandAtCursor();
  // Opens the context menu of a row or title at a window position (right-click does the same).
  bool openContextMenu(const std::string& id, double x, double y);
  MenuController& contextMenu() { return *contextMenu_; }

 private:
  enum class Part : uint8_t { None, Title, TitleEye, NewMenu, Handle, Eye, Body };
  struct Hit {
    Part part = Part::None;
    int index = -1;  // title or row
  };
  struct Row {
    std::string id, parent;
    commands::customize::Kind kind = commands::customize::Kind::Command;
    int depth = 0;
    std::string label, shortcut, icon;
    bool visible = true, locked = false, user = false, missing = false, hasSubmenu = false;
    double y = 0.0, h = 0.0;
  };
  struct TitleTab {
    std::string id, label;
    bool visible = true, locked = false, user = false;
    double x = 0.0, w = 0.0;
  };
  struct Candidate {
    bool valid = false;
    commands::customize::Placement placement;
    core::layout::RectD line;
    int title = -1;
  };

  void rebuild();
  void appendSection(const commands::customize::Node& section, const std::string& parent, int depth);
  void appendEntry(const commands::customize::Node& entry, const std::string& parent, int depth);
  double rowsTop() const { return kTitleHeight + 6.0; }
  core::layout::RectD local(double x, double y, double w, double h) const;
  Hit hitAt(double x, double y) const;
  Candidate locate(const DragPayload& payload, double x, double y) const;
  commands::customize::Kind payloadKind(const DragPayload& payload) const;
  void updateHover(double x, double y);
  void toggleHidden(const std::string& id);
  void removeOrHide(const std::string& id);
  void moveByKey(const std::string& id, int direction);
  void endRename();
  void cancelDrag();
  std::string lockTip(const std::string& id) const;

  CustomizeController& controller_;
  CustomizeController::ListenerId listener_ = 0;
  std::vector<TitleTab> titles_;
  double newMenuX_ = 0.0, newMenuW_ = 0.0;
  std::vector<Row> rows_;
  std::string current_;
  std::string cursor_;
  Indicator indicator_;
  std::string tip_;
  Hit hover_;
  std::string armedId_;
  bool dragging_ = false;
  Hit pressed_;
  core::tree::WidgetId rename_;
  std::string renameTarget_;
  std::unique_ptr<MenuController> contextMenu_;
  bool registered_ = false;
};

}  // namespace r1ui::widgets
