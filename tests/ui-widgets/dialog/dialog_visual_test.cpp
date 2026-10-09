// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the visual oracle of the modal dialog against the variables-dialog captures in both themes:
//   the scrim (black at 50% over the whole window in both themes), the dialog surface (800 x 675,
//   radius 12, border, shadow 2xl, centred), the header with its 14 px weight 600 title and divider,
//   and the 24 x 24 close button (idle and hover).
// Callers: CTest (label gpu, offscreen, no window).
// Not covered: widget-dialog-search-input-*, widget-dialog-collection-tab-* and the variable rows,
//   buttons and tab strip inside the reference dialog are part of the variables editor, not of the
//   dialog widget; the empty-state text and its button in the body are ignored (they are other
//   widgets: the reference text is 14 px, the dialog's own description style is 12 px).
#include "../menu/ScreenCompare.h"
#include "r1ui/widgets/dialog/Dialog.h"
#include "r1ui/widgets/dialog/DialogParts.h"

namespace {

using namespace r1test::screen;
using r1ui::theme::ThemeId;

// The variables dialog of the reference, empty state; returns the widget the state applies to.
BuildFn variablesDialog(bool closeButtonIsTarget) {
  return [=](UiContext& ui, WidgetId) {
    DialogSpec spec;
    spec.title = "Local variables";
    spec.width = 800;
    spec.height = 675;
    spec.headerDivider = true;
    spec.bodyPadding = 0;
    const DialogHandle handle = openDialog(ui, spec);
    ui.frame();
    WidgetId close;
    ui.tree().forEachDescendant(handle.content, [&](WidgetId id) {
      if (ui.objectAs<DialogCloseButton>(id) != nullptr) close = id;
    });
    return closeButtonIsTarget ? close : handle.host;
  };
}

Case crop(const char* reference, double x, double y, ThemeId theme, VisualState state = VisualState::Idle) {
  Case c;
  c.reference = reference;
  c.clipX = x;
  c.clipY = y;
  c.theme = theme;
  c.state = state;
  c.profile = "screen";
  c.luminance = true;  // reference text has LCD subpixel antialiasing; ours is grayscale
  c.page = false;
  return c;
}

}  // namespace

int main() {
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    // The scrim: only a flat canvas strip above the dialog is compared (the rest of the reference
    // screen is the application behind it).
    Case overlay = crop("widget-dialog-overlay-idle", 0, 0, theme);
    overlay.only = {{300, 30, 860, 70}};
    R1_EXPECT_CROP(variablesDialog(false), overlay);

    Case content = crop("widget-dialog-content-idle", 314, 106.5, theme);
    content.ignore = {{326, 338, 170, 64}};  // empty-state text and button (other widgets)
    R1_EXPECT_CROP(variablesDialog(false), content);

    R1_EXPECT_CROP(variablesDialog(true), crop("widget-dialog-close-button-idle", 1073, 119.5, theme));
    R1_EXPECT_CROP(variablesDialog(true), crop("widget-dialog-close-button-hover", 1073, 119.5, theme, VisualState::Hover));
  }
  return r1test::finish();
}
