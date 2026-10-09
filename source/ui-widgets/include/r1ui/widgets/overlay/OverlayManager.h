// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the overlay layer of one UiContext: popups that are drawn above the normal tree and are
//   not clipped by ancestors (menus, select lists, popovers, tooltips, dialogs), their stacking,
//   placement, dismissal rules, modality, focus trap and focus restore.
// Why: every popup-like widget needs the same mechanism (spec 10 and spec 01): open at an anchor
//   with a placement preference and flip at the window edges, close on an outside press or Escape
//   (top overlay first), block input behind a modal one, keep Tab inside it, and give focus back to
//   where it was. Building it once keeps menus, selects, dialogs and tooltips consistent.
// Callers: UiContext owns one (ui.overlays()); popup widgets call open()/close(); UiContext calls the
//   input hooks (pressOutside, escape, trapTab) and afterLayout(). Calls: UiContext (create/destroy
//   widgets, router focus), Placement.
// Structure: an overlay is an OverlayHost widget (the popup surface) that is a child of the
//   context's overlay layer, a full-window absolute container above the normal tree. The caller
//   builds the popup content as children of `handle.host`. A modal overlay also gets a blocker
//   widget behind its host that swallows pointer input (and paints the scrim when asked).
//   Overlays stack in opening order; the last is topmost. Hit testing needs nothing special: the
//   layer has the highest `layer` value, so the Router hits overlays first.
// Placement: open() creates the host hidden; after the next layout the manager measures it, applies
//   `matchAnchorWidth` and the maximum height (80% of the window by default, spec 10 rule 23), picks
//   the position with placePopup(), moves it by setting its absolute insets and shows it. Anchors
//   are in logical window coordinates (use WidgetObject geometry via ui.tree()).
// Dismissal: close(id, reason) destroys the host (and blocker), restores focus (only when focus is
//   missing or was inside the closed overlay, so a command that moved focus wins, spec 01 scenario
//   13) and then calls onClosed. Pointer presses outside the stack close the overlays whose
//   dismissOnOutsidePress is set, top first; a press that lands in the anchor widget only closes
//   (no pass-through) so a trigger button can toggle its popup. Escape closes the top overlay, or
//   every overlay flagged closeAllOnEscape (menu stacks) and clears focus before restoring it.
// Invariants: ids are never reused; every method tolerates stale ids; an entry whose host was destroyed
//   by its owner is closed (Programmatic) at the next input or frame hook; close() may be called from
//   inside the popup's own event handlers (destruction of the widgets is deferred by UiContext).
// Threading: UI thread only.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "r1ui/core/layout/Geometry.h"
#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/overlay/Placement.h"

namespace r1ui::widgets {

class UiContext;
class OverlayHost;

struct OverlayId {
  uint32_t value = 0;
  bool valid() const { return value != 0; }
  friend bool operator==(const OverlayId&, const OverlayId&) = default;
};

enum class DismissReason : uint8_t { Programmatic, OutsidePress, Escape, WindowDeactivated, Replaced };

// Visual treatment of the host surface (style rows overlay.<name>, see OverlayHost).
enum class OverlaySurface : uint8_t { None, Popover, Menu, Tooltip, Dialog };

struct OverlayOptions {
  // ---- placement ----
  core::layout::Rect anchor;               // logical window coordinates; for Manual the top-left is used
  Placement placement = Placement::BelowStart;
  double gap = 0.0;                        // between anchor and popup
  double windowMargin = 4.0;               // keep this far from the window edges
  bool flip = true;
  bool matchAnchorWidth = false;           // popup at least as wide as the anchor (selects)
  double maxHeightFraction = 0.8;          // of the window height; 0 = no limit
  core::tree::WidgetId anchorWidget;       // presses inside it only close the overlay (toggle)

  // ---- behaviour ----
  bool modal = false;                      // blocks input behind it, traps focus unless trapFocus is false
  bool scrim = false;                      // modal only: paint the dimming layer (black at 50%)
  bool trapFocus = true;                   // only meaningful together with modal
  bool allowGlobalShortcuts = false;       // modal only: application shortcuts keep working behind the dialog
  bool dismissOnOutsidePress = true;
  bool outsidePressPassesThrough = true;   // the dismissing press is also delivered (spec 10 rule 38)
  bool dismissOnEscape = true;
  bool escapeFirst = false;                // Escape is offered to the overlay before the focused widget (menus)
  bool closeAllOnEscape = false;           // Escape closes every contiguous overlay with this flag (menu stack)
  bool dismissOnWindowDeactivate = false;
  bool restoreFocus = true;
  bool focusOnOpen = false;                // focus the first focusable widget inside once placed
  bool interactive = true;                 // false: the host is hit-test transparent (tooltips)

  // ---- look ----
  OverlaySurface surface = OverlaySurface::Popover;
  std::string shadow;                      // shadow token ("overlay", "xl", ...) replacing the surface's default; empty = default
  double fadeInMs = 0.0;                   // opacity ramp after the first paint (tooltips: 100)

  std::function<void(DismissReason)> onClosed;
};

struct OverlayHandle {
  OverlayId id;
  core::tree::WidgetId host;  // add the popup content as children of this widget
  bool valid() const { return id.valid(); }
};

class OverlayManager {
 public:
  explicit OverlayManager(UiContext& ui);
  ~OverlayManager();
  OverlayManager(const OverlayManager&) = delete;
  OverlayManager& operator=(const OverlayManager&) = delete;

  // Called once by UiContext after the root exists: creates the overlay layer widget.
  void createLayer();
  core::tree::WidgetId layer() const { return layer_; }

  // ---- open / close ----
  OverlayHandle open(const OverlayOptions& options);
  // True when the overlay existed. Destroys its widgets, restores focus, calls onClosed.
  bool close(OverlayId id, DismissReason reason = DismissReason::Programmatic);
  void closeAll(DismissReason reason = DismissReason::Programmatic);
  // Moves a Manual overlay (tooltips following the pointer).
  void setPosition(OverlayId id, double x, double y);
  // Replaces the anchor of an open overlay and places it again (a menu that follows its trigger).
  void setAnchor(OverlayId id, const core::layout::Rect& anchor);

  // ---- queries ----
  bool isOpen(OverlayId id) const;
  size_t count() const { return entries_.size(); }
  bool any() const { return !entries_.empty(); }
  bool anyModal() const;
  // True while a modal overlay that has not opted in (allowGlobalShortcuts) is open: the context then
  // withholds keys from the application's global shortcut handler.
  bool blocksGlobalShortcuts() const;
  OverlayId topmost() const { return entries_.empty() ? OverlayId{} : OverlayId{entries_.back().id}; }
  // Overlays in stacking order, bottom first.
  std::vector<OverlayId> stack() const;
  core::tree::WidgetId hostOf(OverlayId id) const;
  // Index in the stack of the topmost overlay whose host contains `widget` (or is it); -1 if none.
  int indexContaining(core::tree::WidgetId widget) const;
  // True when `widget` is inside the host of overlay `id`.
  bool contains(OverlayId id, core::tree::WidgetId widget) const;

  // ---- input hooks (called by UiContext) ----
  struct PressOutcome {
    bool deliver = true;   // pass the press on to the normal routing
    bool dismissed = false;
  };
  // A pointer press is about to be routed; `hit` is what the router would hit.
  PressOutcome pressOutside(core::tree::WidgetId hit);
  // Escape was pressed. phase false = before the focused widget (only escapeFirst overlays take it),
  // true = nobody used it. Returns true when an overlay consumed it.
  bool escape(bool afterWidgets);
  // The window lost activation.
  void windowDeactivated();
  // A Tab press must stay inside the topmost trapping overlay. Returns true when it moved focus.
  bool trapTab(bool backwards);
  // Keeps focus inside the topmost trapping overlay (called each frame).
  void enforceFocusTrap();

  // ---- frame hooks ----
  // Places overlays that are waiting for their size; true when something was placed (layout dirty,
  // the caller must lay out again in the same frame).
  bool afterLayout();
  bool needsPlacement() const;

 private:
  struct Entry {
    uint32_t id = 0;
    core::tree::WidgetId host;
    core::tree::WidgetId blocker;
    OverlayOptions options;
    core::tree::WidgetId savedFocus;
    bool savedFocusVisible = false;
    bool placed = false;
    bool focusPending = false;
    int placeAttempts = 0;
  };

  Entry* find(OverlayId id);
  const Entry* find(OverlayId id) const;
  size_t indexOf(uint32_t id) const;
  void place(Entry& entry);
  void reapDead();
  void restoreFocusFor(const Entry& entry, bool focusWasInside);
  bool trapping(const Entry& entry) const { return entry.options.modal && entry.options.trapFocus; }

  UiContext& ui_;
  core::tree::WidgetId layer_;
  std::vector<Entry> entries_;
  uint32_t nextId_ = 1;
};

}  // namespace r1ui::widgets
