// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the modal dialog: openDialog() builds a dialog on the overlay layer (centred, scrim black at
//   50% in both themes, 12 px radius, border, shadow 2xl) with a title (14 px, weight 600), an
//   optional description (12 px muted, wrapped), a body slot the caller fills, a footer of action
//   buttons and an optional close button, and reports how it ended through one result callback.
// Why: spec 10 rules 56 to 64 define a modal window: it blocks everything behind it, keeps Tab
//   inside, closes on Escape as cancel and runs the default action on Enter, and several dialogs
//   stack with only the newest interactive. The overlay layer provides blocking, focus trap and
//   restore; this file adds the dialog look and the keyboard contract.
// Callers: application code, the gallery. Calls: OverlayManager (modal overlay, Center placement),
//   OverlayWatch (owner), TooltipContent's wrapTooltipText for the wrapped description.
// Contract: the result callback runs exactly once, after the dialog is gone and focus restored:
//   `action` is the id of the pressed action button; Escape and the close button end the dialog as
//   cancel (dismissed = true, action = the id of the action marked isCancel, if any). Enter, when not
//   used by the focused widget, activates the first enabled action marked isDefault. Pressing the
//   scrim does nothing unless closeOnScrimPress is set. Closing the owner widget closes the dialog.
//   closeDialog() ends it from code with an action id.
// Size: sized by content up to maxWidth (default 512 px) and never wider than the window; width and
//   height fix the outer size (the variables dialog is 800 x 675). Content taller than 80% of the
//   window is clipped (the body shrinks), not scrolled: put a scrolling widget in the body.
// Layout and look (docs/spec/widgets.md 2.11): header padding 16 x 12 with the close button 24 x 24
//   at 12 px from the top and 16 px from the right, optional 1 px divider under the header, body
//   padding bodyPadding, footer right-aligned with 8 px gaps. Neutral action buttons use the text
//   button look (fill `hover`, `border` on hover, 12 px `surface` text, padding 12 x 6, radius 4);
//   primary actions use `accent` with white text.
// Boundaries: texts are sanitised; at most 8 actions are built; the id strings are opaque.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/overlay/OverlayManager.h"

namespace r1ui::widgets {

class UiContext;
struct DialogState;

enum class DialogActionKind : uint8_t { Neutral, Primary, Danger };

struct DialogAction {
  std::string id;
  std::string label;
  DialogActionKind kind = DialogActionKind::Neutral;
  bool isDefault = false;  // Enter runs it
  bool isCancel = false;   // Escape and the close button report its id
  bool enabled = true;
};

struct DialogResult {
  std::string action;      // pressed action, or the cancel action's id (may be empty)
  bool dismissed = false;  // Escape, close button, outside press or owner gone
};

struct DialogSpec {
  std::string title;
  std::string description;
  std::vector<DialogAction> actions;
  bool closeButton = true;
  bool headerDivider = false;
  bool closeOnScrimPress = false;
  double width = 0.0;      // 0 = by content
  double height = 0.0;
  double maxWidth = 512.0;
  double bodyPadding = 16.0;
  core::tree::WidgetId owner;
  std::function<void(const DialogResult&)> onResult;
};

struct DialogHandle {
  OverlayId overlay;
  core::tree::WidgetId host;
  core::tree::WidgetId content;
  core::tree::WidgetId body;  // add the caller's content as children of this widget
  std::shared_ptr<DialogState> state;
  bool valid() const { return overlay.valid(); }
};

// An invalid handle when the overlay layer is unavailable or the widgets could not be created.
DialogHandle openDialog(UiContext& ui, DialogSpec spec);
// Ends the dialog as if the action `action` was pressed (dismissed = false). False when already closed.
bool closeDialog(UiContext& ui, const DialogHandle& handle, std::string action = {});
bool isDialogOpen(UiContext& ui, const DialogHandle& handle);

}  // namespace r1ui::widgets
