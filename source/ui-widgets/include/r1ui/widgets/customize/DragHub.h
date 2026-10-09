// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DragHub, the one place a customization drag lives: the payload (a command from the palette or
//   an existing entry being moved), the registered drop targets (the edit displays of menus, toolbars
//   and free-form panels), the floating ghost, and the begin / move / end / cancel protocol.
// Why: spec 08 (drag threshold, ghost near the pointer, Escape cancels, a drop on nothing does
//   nothing) and decision D2 (dragging from a command palette is the main way to place a command).
//   The source widget keeps the pointer capture and forwards its events; the hub finds the target
//   under the pointer, asks it whether it would accept (so it can draw its insertion indicator only
//   where a drop is legal) and delivers the drop.
// Callers: the sources (CommandPalette rows, the entry handles of the edit displays) and the targets
//   (MenuEditor, ToolbarEditStrip, FreeFormPanel). Calls: UiContext (overlay ghost, geometry).
// Rules: one drag at a time (begin fails while active); a target that is destroyed is dropped from
//   the list lazily; end() asks the target under the pointer, never any other; cancel() and a
//   pointer-capture loss end the drag without a drop; the ghost overlay never takes input.
// Lifetime: owned by CustomizeController; targets register their widget id and a pointer to
//   themselves and must unregister in onDetached.
#pragma once

#include <string>
#include <vector>

#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/overlay/OverlayManager.h"

namespace r1ui::widgets {

class UiContext;

struct DragPayload {
  enum class Kind : uint8_t { Command, Node };
  Kind kind = Kind::Command;
  std::string commandId;  // Command: a palette command to place
  std::string nodeId;     // Node: an existing entry being moved
  std::string text;       // the ghost's text
};

class DragTarget {
 public:
  virtual ~DragTarget() = default;
  // The pointer is over the target (window coordinates). Returns true when a drop here would be
  // accepted; the target shows its insertion indicator accordingly.
  virtual bool dragOver(const DragPayload& payload, double x, double y) = 0;
  // The pointer left the target or the drag ended: remove the indicator.
  virtual void dragLeave() = 0;
  // The button was released over the target; true when the drop changed something.
  virtual bool dragDrop(const DragPayload& payload, double x, double y) = 0;
};

class DragHub {
 public:
  explicit DragHub(UiContext& ui);
  ~DragHub();
  DragHub(const DragHub&) = delete;
  DragHub& operator=(const DragHub&) = delete;

  void addTarget(core::tree::WidgetId widget, DragTarget* target);
  void removeTarget(core::tree::WidgetId widget);

  // False (nothing happens) while another drag is active.
  bool begin(DragPayload payload, double x, double y);
  void move(double x, double y);
  // Delivers the drop to the target under the pointer; true when it was applied. Always ends the drag.
  bool end(double x, double y);
  void cancel();

  bool active() const { return active_; }
  const DragPayload& payload() const { return payload_; }
  core::tree::WidgetId overTarget() const { return over_; }
  // The target under the pointer would accept a drop now.
  bool accepting() const { return accepting_; }
  size_t targetCount() const { return targets_.size(); }

 private:
  struct Entry {
    core::tree::WidgetId widget;
    DragTarget* target = nullptr;
  };
  Entry* hit(double x, double y);
  void leave();
  void closeGhost();

  UiContext& ui_;
  std::vector<Entry> targets_;
  bool active_ = false;
  bool accepting_ = false;
  DragPayload payload_;
  core::tree::WidgetId over_;
  OverlayId ghost_;
};

}  // namespace r1ui::widgets
