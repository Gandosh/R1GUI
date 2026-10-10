// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ActionList (a search box above a virtualized, grouped list of actions) and ActionListView, the
//   list part. Each action is one row: icon and label, the DESCRIPTION in grey on the same row (elided
//   to the column, the tooltip shows it in full) and the shortcut text. Actions are grouped under
//   collapsible category headers.
// Why: three places need "all actions, searchable, with what they do": the hotkey editor, a menu
//   creator that offers actions to drag into a menu, and a command-palette style picker. One widget
//   with one drag payload serves them all, and it stays fast for tens of thousands of actions because
//   only the rows in view are measured and painted.
// Callers: HotkeyEditor, hosts, the gallery, tests. Calls: ActionModel (data and search), DragHub
//   (customize/DragHub.h, optional), TextInput, UiContext.
//
// Data: setActions(vector<ActionInfo>) or bindRegistry(registry, keymap) which keeps the list in step
//   with a CommandRegistry (rebuilt when it notifies, selection and scroll kept when the action is
//   still there). Every text is sanitised and cut at its limit (ActionModel.h); duplicate ids are
//   listed as given (the first match wins for select(id)).
// Search: space separated terms, ALL must occur (ASCII case-insensitive) in label, id, description or
//   category (and the shortcut text when ActionListOptions::searchShortcuts is set); matches are highlighted in the label and the description. While a search is active the
//   groups are shown expanded (their collapse state is kept and returns when the search is cleared).
// Selection and keys: click or Up/Down/PageUp/PageDown/Home/End select (a header row can be selected:
//   Enter, Space, Left and Right then collapse or expand it); Enter or a double-click on an action
//   calls onActivate; any printable character typed in the list, or Ctrl+F, moves to the search box
//   (the typed character starts the search text); Up/Down in the search box move the selection.
// Drag payload: pressing an action row and moving past the drag threshold starts a drag in the hub
//   given to setDragHub (nothing happens without a hub). The payload is the toolkit's DragPayload with
//   kind Command: `commandId` = the ActionInfo id, `text` = the label (the ghost's text). Any
//   DragTarget (the menu, toolbar and free-form editors of the customization layer, a host's own
//   target) therefore accepts it exactly as it accepts a CommandPalette row; makeActionDragPayload
//   builds the same payload for hosts that start a drag themselves.
// Performance: setActions and every filter change are O(actions x terms) over pre-folded text; a
//   frame costs O(visible rows). 20 000 actions filter in a few milliseconds (tested).
// Look: rows 28 px, headers 26 px, optional column header strip 24 px; hover `hover`, selection
//   `accent` with white text; panel background; a thin scroll thumb that can be dragged.
// Lifetime: the registry/keymap given to bindRegistry and the drag hub must outlive the widget; the
//   widget unsubscribes in onDetached and ends a running drag.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "r1ui/widgets/actions/ActionModel.h"
#include "r1ui/widgets/customize/DragHub.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

// The drag payload of an action row (see header comment).
DragPayload makeActionDragPayload(const ActionInfo& action);

struct ActionListOptions {
  bool columnHeaders = false;                // a fixed strip naming the columns above the rows
  std::string labelHeader = "Action";
  std::string descriptionHeader = "Description";
  std::string shortcutHeader = "Hotkey";
  double shortcutWidth = 140.0;              // logical px of the shortcut column
  bool showDescriptions = true;
  bool showIcons = true;
  std::string searchPlaceholder = "Search actions by name, description or category";
  bool showSearch = true;
  bool searchShortcuts = false;              // the search also matches the shortcut text
};

// One row of the flattened, filtered list.
struct ActionRow {
  bool header = false;
  uint32_t index = 0;  // header: group index; action: index into the action vector
  uint32_t count = 0;  // header: number of matching actions in the group
};

class ActionListView final : public WidgetObject {
 public:
  static constexpr double kRowHeight = 28.0;
  static constexpr double kHeaderHeight = 26.0;
  static constexpr double kColumnHeaderHeight = 24.0;

  explicit ActionListView(ActionListOptions options = {}) : options_(std::move(options)) {}
  const char* typeName() const override { return "ActionListView"; }
  void onAttached() override;
  void onDetached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override { return Cursor::Default; }
  std::string_view tooltipText() const override;
  bool wantsTextInput() const override { return true; }
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onPointerWheel(Event& e) override;
  void onDoubleClick(Event& e) override;
  void onDragStart(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;
  void onTextInput(Event& e) override;

  // ---- data ----
  void setActions(std::vector<ActionInfo> actions);
  const std::vector<ActionInfo>& actions() const { return actions_; }
  // Sorted distinct categories of all actions (not only the matching ones).
  const std::vector<std::string>& categories() const { return groupNames_; }
  void setQuery(std::string_view text);
  const std::string& query() const { return queryText_; }
  // Only this category (empty: all).
  void setCategoryFilter(std::string category);
  const std::string& categoryFilter() const { return categoryFilter_; }
  size_t matchCount() const { return matches_; }

  // ---- groups ----
  void setCollapsed(const std::string& category, bool collapsed);
  bool isCollapsed(const std::string& category) const;

  // ---- rows and selection ----
  const std::vector<ActionRow>& rows() const { return rows_; }
  int selectedRow() const { return selected_; }
  // The selected action, nullptr when nothing or a header is selected.
  const ActionInfo* selectedAction() const;
  bool selectAction(std::string_view id);
  void selectRow(int row);
  void moveSelection(int delta);
  void activateSelected();
  const ActionInfo& rowAction(int row) const { return actions_[rows_[static_cast<size_t>(row)].index]; }
  const std::string& rowCategory(int row) const;

  // ---- geometry ----
  double scrollOffset() const { return scroll_; }
  void setScrollOffset(double offset);
  double contentHeight() const { return tops_.empty() ? 0.0 : tops_.back(); }
  double viewportHeight() const;
  core::layout::RectD rowRect(int row) const;  // empty when scrolled out
  int rowAt(double x, double y) const;
  // Column rectangles of a row (window coordinates), for tests and the editor's overlays.
  struct Columns {
    double labelX, labelW, descX, descW, shortcutX, shortcutW;
  };
  Columns columns() const;
  // Window rectangle of the thumb; empty when everything fits.
  core::layout::RectD thumbRect() const;

  // ---- callbacks and drag ----
  void setOnSelect(std::function<void(const ActionInfo&)> callback) { onSelect_ = std::move(callback); }
  void setOnActivate(std::function<void(const ActionInfo&)> callback) { onActivate_ = std::move(callback); }
  // Called with the typed character (0 for Ctrl+F) when the list wants the search box.
  void setOnSearchRequest(std::function<void(char32_t)> callback) { onSearchRequest_ = std::move(callback); }
  void setDragHub(DragHub* hub) { hub_ = hub; }
  bool dragging() const { return dragging_; }
  const ActionListOptions& options() const { return options_; }

 private:
  void rebuildRows();
  void setSelectedRow(int row, bool notify);
  void scrollIntoView(int row);
  double rowHeight(const ActionRow& row) const { return row.header ? kHeaderHeight : kRowHeight; }
  double viewTop() const;
  bool thumbHit(double x, double y) const;
  void dragThumbTo(double y);
  void toggleGroup(uint32_t group);

  ActionListOptions options_;
  std::vector<ActionInfo> actions_;
  std::vector<std::string> haystacks_;           // folded search text per action
  std::vector<std::string> groupNames_;          // sorted distinct categories
  std::vector<std::vector<uint32_t>> groups_;    // action indices per group, in the given order
  std::vector<uint8_t> collapsed_;               // per group
  std::vector<uint32_t> groupOf_;                // per action
  std::string queryText_;
  ActionQuery query_;
  std::string categoryFilter_;
  size_t matches_ = 0;
  std::vector<ActionRow> rows_;
  std::vector<double> tops_;                     // rows_.size() + 1 prefix sums
  int selected_ = -1;
  int hover_ = -1;
  int pressed_ = -1;
  double scroll_ = 0.0;
  bool armed_ = false;
  bool dragging_ = false;
  bool thumbDrag_ = false;
  double thumbGrab_ = 0.0;
  DragHub* hub_ = nullptr;
  std::function<void(const ActionInfo&)> onSelect_;
  std::function<void(const ActionInfo&)> onActivate_;
  std::function<void(char32_t)> onSearchRequest_;
};

class ActionList final : public WidgetObject {
 public:
  explicit ActionList(ActionListOptions options = {}) : options_(std::move(options)) {}
  const char* typeName() const override { return "ActionList"; }
  void onAttached() override;
  void onDetached() override;
  void onKeyDown(Event& e) override;
  void onLayout() override { flush(); }

  // ---- data ----
  void setActions(std::vector<ActionInfo> actions) { view().setActions(std::move(actions)); }
  // Applies a registry change that is waiting for the next layout pass (changes are coalesced).
  void flush();
  // Lists the registry's commands and follows it (and the keymap's shortcut text) live. Pass nullptrs to
  // stop following. The registry notifies on every binding change, so shortcut text stays current.
  void bindRegistry(commands::CommandRegistry* registry, const commands::Keymap* keymap, ActionSourceOptions source = {});
  void refreshFromRegistry();

  void setFilter(const std::string& text);
  const std::string& filter() const { return view().query(); }
  void setCategoryFilter(std::string category) { view().setCategoryFilter(std::move(category)); }

  // ---- parts ----
  ActionListView& view() const;
  core::tree::WidgetId searchField() const { return search_; }
  core::tree::WidgetId viewWidget() const { return view_; }
  void focusSearch();
  void setDragHub(DragHub* hub) { view().setDragHub(hub); }

 private:
  void searchRequested(char32_t codePoint);

  ActionListOptions options_;
  core::tree::WidgetId search_;
  core::tree::WidgetId view_;
  commands::CommandRegistry* registry_ = nullptr;
  const commands::Keymap* keymap_ = nullptr;
  ActionSourceOptions source_;
  uint32_t listener_ = 0;
  bool dirty_ = false;
};

}  // namespace r1ui::widgets
