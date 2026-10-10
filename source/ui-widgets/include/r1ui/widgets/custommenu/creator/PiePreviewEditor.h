// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PiePreviewEditor, the left side of the menu creator for a pie menu: the pie drawn as an editable
//   radial preview (the real PieMenu look, 4, 6 or 8 slots), where each slot is a drop target for an
//   action dragged from the action list, can be selected, cleared (key or context menu) and swapped with
//   another slot by dragging.
// Why: owner requirement 2026-10-10: "on the left the pie with its slots drawn as an editable radial
//   preview ... the user drags an action from the list onto a slot ... drag within the preview to
//   move/swap; delete/clear a slot". Drawing it with PieMenu guarantees the preview is exactly what the
//   right mouse button will show.
// Callers: CreateCustomMenuWindow (and the gallery/tests). Calls: PieMenu (child), DragHub (as a drag
//   source for swaps and a DragTarget for drops), MenuController (context menu), the draft's operations.
// Model: every change is an operation on the session's MenuDraft (setSlot, moveEntry, clearSlot), so the
//   model's limits and refusals apply; a refused change leaves the draft alone and is reported through
//   onMessage. A drop of a command the registry does not know is refused (a missing command can stay in a
//   menu that was loaded, but cannot be newly placed).
// Slots: slot 0 up, clockwise (PieGesture's geometry). A pointer position selects the slot whose sector
//   contains it (like the gesture), within the pie's disc and outside its dead zone.
// Keys (focus in the preview): arrows move the selection, Delete/Backspace clear the selected slot,
//   Escape cancels a running drag.
// Lifetime: session, services and hub outlive the widget; the widget unregisters from the hub, ends its
//   drag and closes its menu in onDetached. The draft may be replaced at any time: every access goes
//   through session.draft() and tolerates null. UI thread only.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/customize/DragHub.h"
#include "r1ui/widgets/custommenu/creator/CreatorSession.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

// Drag payload kind used for moving an entry inside a preview: DragPayload::Kind::Node with this prefix
// and the source index ("slot:3").
inline constexpr const char* kEntryDragPrefix = "slot:";

class PiePreviewEditor final : public WidgetObject, private DragTarget {
 public:
  PiePreviewEditor(CommandServices services, CreatorSession& session);

  const char* typeName() const override { return "PiePreviewEditor"; }
  void onAttached() override;
  void onDetached() override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override { return Cursor::Default; }
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onDragStart(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;

  void setDragHub(DragHub* hub);
  // Called after every change this editor made to the draft.
  void setOnChanged(std::function<void()> callback) { onChanged_ = std::move(callback); }
  void setOnSelect(std::function<void(int)> callback) { onSelect_ = std::move(callback); }
  void setOnMessage(std::function<void(const std::string&)> callback) { onMessage_ = std::move(callback); }

  // Rebuilds the pie drawing from the draft (call after the draft changed from outside).
  void refresh();

  // ---- selection and editing (the UI paths call these) ----
  int selected() const { return selected_; }
  void select(int slot);
  int dropHover() const { return dropSlot_; }
  bool dropCommand(int slot, const std::string& commandId);
  bool clearSlot(int slot);
  // The slot whose sector contains the window point, or -1.
  int slotAt(double x, double y) const;
  // Centre of a slot's pill in window coordinates (empty point for a bad index).
  bool slotCenter(int slot, double& x, double& y) const;
  bool dragging() const { return dragging_; }
  bool menuOpen() const { return menu_ != nullptr && menu_->isOpen(); }

 private:
  // DragTarget
  bool dragOver(const DragPayload& payload, double x, double y) override;
  void dragLeave() override;
  bool dragDrop(const DragPayload& payload, double x, double y) override;

  bool acceptsPayload(const DragPayload& payload, int slot, int& sourceSlot);
  void say(const std::string& text) const;
  void changed();
  void openContextMenu(int slot, double x, double y);
  commands::custommenu::MenuDraft* draft() const { return session_.draft(); }

  CommandServices services_;
  CreatorSession& session_;
  DragHub* hub_ = nullptr;
  core::tree::WidgetId pie_;
  std::unique_ptr<MenuController> menu_;
  int selected_ = -1;
  int dropSlot_ = -1;
  int pressed_ = -1;
  int dragSource_ = -1;
  bool armed_ = false;
  bool dragging_ = false;
  std::function<void()> onChanged_;
  std::function<void(int)> onSelect_;
  std::function<void(const std::string&)> onMessage_;
};

}  // namespace r1ui::widgets
