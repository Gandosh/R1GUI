// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the generic anchored popover: openPopover() puts a popover surface (8 px radius, 1 px border
//   `border`, background `panel`, shadow xl, no arrow) on the overlay layer next to an anchor widget
//   or rectangle and returns the host the caller fills with content; closePopover() closes it.
// Why: popovers (fill, colour picker, grid settings, share) differ only in their content; the
//   behaviour is shared: placement with flipping and pushing inside the window, dismissal by outside
//   press (the press is delivered, except one on the anchor which only closes: the trigger toggles),
//   Escape, window deactivation optional, focus moved in on open and restored on close, following
//   an anchor that moves and closing when the anchor or an owner widget goes away.
// Callers: application code and other widgets (selects, pickers) as the container for their content.
//   Calls: OverlayManager (open, placement, dismissal), OverlayWatch.
// Content: the caller adds children to `handle.host`. The host is a flex column; options.padding is
//   the content padding (the 1 px border has no layout effect, so one more pixel is added), and
//   options.width fixes the outer width (0 = size by content).
// Look: docs/spec/widgets.md 2.10 (measured grid popover 256 x 183, padding 12, opens above its
//   button with an 8 px offset).
// Boundaries: non-finite or negative padding, width and gap are treated as 0; an invalid anchor
//   widget falls back to anchorRect.
#pragma once

#include <functional>

#include "r1ui/core/layout/Geometry.h"
#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/overlay/OverlayManager.h"

namespace r1ui::widgets {

class UiContext;

struct PopoverOptions {
  core::tree::WidgetId anchorWidget;      // placed next to it, followed while open
  core::layout::Rect anchorRect;          // used when anchorWidget is invalid
  core::tree::WidgetId owner;             // the popover closes when this widget goes away
  Placement placement = Placement::BelowCenter;
  double gap = 8.0;
  double padding = 0.0;
  double width = 0.0;
  bool flip = true;
  bool matchAnchorWidth = false;
  bool modal = false;
  bool dismissOnOutsidePress = true;
  bool dismissOnEscape = true;
  bool dismissOnWindowDeactivate = false;
  bool focusOnOpen = true;
  bool restoreFocus = true;
  std::function<void(DismissReason)> onClosed;
};

struct PopoverHandle {
  OverlayId overlay;
  core::tree::WidgetId host;  // add the content as children of this widget
  bool valid() const { return overlay.valid(); }
};

// Opens a popover; an invalid handle when the overlay layer is unavailable.
PopoverHandle openPopover(UiContext& ui, const PopoverOptions& options);
// True when it was still open.
bool closePopover(UiContext& ui, const PopoverHandle& handle);
bool isPopoverOpen(UiContext& ui, const PopoverHandle& handle);

}  // namespace r1ui::widgets
