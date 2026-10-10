// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PanelPreviewEditor, the left side of the menu creator for a dockable panel: the panel drawn as the
//   grid of action buttons it will have (columns, button size, labels on or off, icon and label as the
//   real panel shows them), where an action dragged from the list is dropped at an insertion point, a
//   button can be selected, removed, moved by dragging or with Ctrl+arrows.
// Why: owner requirement 2026-10-10: "for a dockable panel: the panel preview as a button grid/list with
//   columns, button size, show labels ... drag within the preview to move/swap; delete/clear a slot".
// Callers: CreateCustomMenuWindow (and the gallery/tests). Calls: DragHub (source for moves, DragTarget
//   for drops), MenuController (context menu), the draft's operations (addEntry, moveEntry, clearSlot).
// Model: every change is an operation on the session's MenuDraft, so limits and refusals are the model's
//   (256 entries, valid ids). A drop of a command the registry does not know is refused. The preview draws
//   buttons itself (it must not run commands when clicked) with the same look as CustomMenuPanel; a
//   missing command (only possible in a loaded menu) is dimmed and shows its id.
// Drop point: the pointer over the left half of a button inserts before it, over the right half after it;
//   below or after the last button the action is appended; a dashed "drop here" cell follows the last
//   button. Moving an entry onto its own position changes nothing and is not offered.
// Keys (focus in the preview): arrows select, Home/End first/last, Delete/Backspace remove, Ctrl+Left/Right
//   move the selected button, Escape cancels a running drag.
// Lifetime: as PiePreviewEditor.
#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/customize/DragHub.h"
#include "r1ui/widgets/custommenu/creator/CreatorSession.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class PanelPreviewEditor final : public WidgetObject, private DragTarget {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();
  static constexpr double kPadding = 8.0;
  static constexpr double kGap = 6.0;

  PanelPreviewEditor(CommandServices services, CreatorSession& session);

  const char* typeName() const override { return "PanelPreviewEditor"; }
  void onAttached() override;
  void onDetached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override { return Cursor::Default; }
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerWheel(Event& e) override;
  void onDragStart(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;

  void setDragHub(DragHub* hub);
  void setOnChanged(std::function<void()> callback) { onChanged_ = std::move(callback); }
  void setOnSelect(std::function<void(int)> callback) { onSelect_ = std::move(callback); }
  void setOnMessage(std::function<void(const std::string&)> callback) { onMessage_ = std::move(callback); }

  // Repaints after the draft changed from outside (columns, size, labels, entries).
  void refresh();

  int selected() const { return selected_; }
  void select(int index);
  // The insertion index the drop indicator shows, or -1.
  int dropIndex() const { return dropIndex_; }
  bool dropCommand(int insertAt, const std::string& commandId);
  bool removeEntry(int index);
  bool moveSelected(int delta);
  // The button under a window point (an index < entry count), or -1.
  int entryAt(double x, double y) const;
  // The insertion index for a window point (0..entry count).
  int insertionAt(double x, double y) const;
  // Window rectangle of cell `index` (index == entry count is the drop-here cell); empty when out of range.
  bool cellRect(int index, double& x, double& y, double& w, double& h) const;
  bool dragging() const { return dragging_; }
  bool menuOpen() const { return menu_ != nullptr && menu_->isOpen(); }

 private:
  struct Grid {
    int columns = 1;
    double cellW = 0.0;
    double cellH = 40.0;
    int entries = 0;
    int rows = 1;
    double contentHeight = 0.0;
  };
  Grid grid() const;
  double maxScroll() const;
  void setScroll(double offset);

  bool dragOver(const DragPayload& payload, double x, double y) override;
  void dragLeave() override;
  bool dragDrop(const DragPayload& payload, double x, double y) override;

  int acceptedInsertion(const DragPayload& payload, double x, double y);
  void say(const std::string& text) const;
  void changed();
  void openContextMenu(int index, double x, double y);
  commands::custommenu::MenuDraft* draft() const { return session_.draft(); }

  CommandServices services_;
  CreatorSession& session_;
  DragHub* hub_ = nullptr;
  std::unique_ptr<MenuController> menu_;
  int selected_ = -1;
  int dropIndex_ = -1;
  int pressed_ = -1;
  int dragSource_ = -1;
  int hover_ = -1;
  double scroll_ = 0.0;
  bool armed_ = false;
  bool dragging_ = false;
  std::function<void()> onChanged_;
  std::function<void(int)> onSelect_;
  std::function<void(const std::string&)> onMessage_;
};

}  // namespace r1ui::widgets
