// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CommandPalette (a search field above a categorized, scrolling list of every registry
//   command: icon, label, shortcut text, category headers) and CommandPaletteList, the list part.
// Why: decision D2: any command can be placed from a palette; dragging a row onto a menu, toolbar or
//   free-form panel in edit mode is the main path, and the same list in the command-picker dialog
//   (CommandPicker.h) is the keyboard and accessibility path (arrows, Enter).
// Callers: hosts and the gallery (a docked palette next to the edit displays), openCommandPicker.
//   Calls: CustomizeController (registry, keymap, drag hub).
// Content: every registered command that is not hiddenFromEditor, grouped by category in the
//   registry's order (sorted categories, commands in registration order inside), narrowed by the
//   search text: each space separated word must occur (case-insensitively) in the label, id,
//   description, category or shortcut text. The list follows the registry live (rebuilt when it
//   notifies), keeping the selection when its command is still shown.
// Interaction: click selects; double-click or Enter chooses (onChoose); Up/Down/PageUp/PageDown move
//   the selection (also while the search field has focus); dragging a row starts a Command drag in
//   the controller's DragHub; the wheel scrolls; the selected row scrolls into view.
// Look: rows 28 px, headers 24 px; hover uses `hover`, selection `accent` with white text.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "r1ui/widgets/customize/CustomizeController.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

struct PaletteRow {
  bool header = false;
  std::string commandId;  // command rows
  std::string text;       // label, or the category for a header
  std::string icon;
  std::string shortcut;
  std::string description;
};

class CommandPaletteList final : public WidgetObject {
 public:
  static constexpr double kRowHeight = 28.0;
  static constexpr double kHeaderHeight = 24.0;

  explicit CommandPaletteList(CustomizeController& controller) : controller_(controller) {}
  const char* typeName() const override { return "CommandPaletteList"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override { return Cursor::Pointer; }
  std::string_view tooltipText() const override;
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onPointerWheel(Event& e) override;
  void onDoubleClick(Event& e) override;
  void onDragStart(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;

  void setRows(std::vector<PaletteRow> rows);
  const std::vector<PaletteRow>& rows() const { return rows_; }
  // Index of the selected row, -1 for none; only command rows can be selected.
  int selectedIndex() const { return selected_; }
  bool select(const std::string& commandId);
  void moveSelection(int delta);
  // Reports the selected command through onChoose (Enter does the same).
  void chooseSelected();
  void setOnChoose(std::function<void(const std::string&)> callback) { onChoose_ = std::move(callback); }
  // Called with the command id whenever the selection changes (click or keys).
  void setOnSelect(std::function<void(const std::string&)> callback) { onSelect_ = std::move(callback); }
  double scrollOffset() const { return scroll_; }
  void setScrollOffset(double offset);
  double contentHeight() const;
  // Window rectangle of row `index` (empty when scrolled out of the visible part).
  core::layout::RectD rowRect(int index) const;
  int rowAt(double x, double y) const;

 private:
  double rowTop(int index) const;
  void scrollIntoView(int index);
  void setSelected(int index);

  CustomizeController& controller_;
  std::vector<PaletteRow> rows_;
  int selected_ = -1;
  int hover_ = -1;
  double scroll_ = 0.0;
  int pressed_ = -1;
  bool armed_ = false;
  bool dragging_ = false;
  std::function<void(const std::string&)> onChoose_;
  std::function<void(const std::string&)> onSelect_;
};

class CommandPalette final : public WidgetObject {
 public:
  explicit CommandPalette(CustomizeController& controller) : controller_(controller) {}
  const char* typeName() const override { return "CommandPalette"; }
  void onAttached() override;
  void onDetached() override;
  void onKeyDown(Event& e) override;

  // Replaces the search text (as if typed) and refilters.
  void setFilter(const std::string& text);
  const std::string& filter() const { return filter_; }
  const std::vector<PaletteRow>& rows() const { return list().rows(); }
  std::string selectedCommand() const;
  bool select(const std::string& commandId) { return list().select(commandId); }
  // Called with the command id on Enter and on a double-click of a row.
  void setOnChoose(std::function<void(const std::string&)> callback);
  core::tree::WidgetId searchField() const { return search_; }
  core::tree::WidgetId listWidget() const { return list_; }
  CommandPaletteList& list() const;
  void focusSearch();
  void rebuild();

 private:
  CustomizeController& controller_;
  core::tree::WidgetId search_;
  core::tree::WidgetId list_;
  std::string filter_;
  std::function<void(const std::string&)> onChoose_;
  commands::CommandRegistry::ListenerId registryListener_ = 0;
};

}  // namespace r1ui::widgets
