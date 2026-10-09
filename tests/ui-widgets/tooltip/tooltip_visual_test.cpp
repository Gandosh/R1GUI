// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the visual oracle of the tooltip against the three reference tooltip captures in both
//   themes: the toolbar tooltip with its shortcut as part of the text ("Pen (P)"), the panel icon
//   button tooltip and the tab close tooltip. The tooltip is shown the way the TooltipManager shows it
//   (a non-interactive Tooltip overlay at a fixed point, content from the installed factory) at the
//   position of the reference capture, on a 1440 x 900 canvas, and the same crop is compared.
// Callers: CTest (label gpu, offscreen, no window).
// Method notes: the reference places a tooltip centred over its trigger at a fractional position;
//   ours is placed at the rounded position of the reference box. The toolbar tooltip crop includes
//   the top edge of the floating toolbar (not part of the tooltip) which is ignored.
#include "../menu/ScreenCompare.h"
#include "r1ui/widgets/tooltip/TooltipContent.h"

namespace {

using namespace r1test::screen;
using r1ui::theme::ThemeId;

// Shows `text` as a tooltip with its top-left at (x, y).
BuildFn tooltipAt(int x, int y, std::string text) {
  return [=](UiContext& ui, WidgetId) {
    RichTooltips::install(ui);
    OverlayOptions o;
    o.surface = OverlaySurface::Tooltip;
    o.placement = Placement::Manual;
    o.anchor = {x, y, 0, 0};
    o.interactive = false;
    o.dismissOnOutsidePress = false;
    o.dismissOnEscape = false;
    o.restoreFocus = false;
    o.maxHeightFraction = 0.0;
    const OverlayHandle handle = ui.overlays().open(o);
    TooltipInfo info;
    info.title = text;
    ui.create<TooltipContent>(handle.host, info);
    return handle.host;
  };
}

Case crop(const char* reference, double x, double y, ThemeId theme, const char* canvasToken) {
  Case c;
  c.reference = reference;
  c.clipX = x;
  c.clipY = y;
  c.theme = theme;
  c.profile = "text";
  c.luminance = true;  // reference text has LCD subpixel antialiasing; ours is grayscale
  c.page = false;
  c.canvasToken = canvasToken;
  return c;
}

}  // namespace

int main() {
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    Case toolbar = crop("widget-tooltip-toolbar-shortcut", 660, 812, theme, "");
    toolbar.ignore = {{0, 33, 71, 5}};  // the floating toolbar below the tooltip
    R1_EXPECT_CROP(tooltipAt(666, 818, "Pen (P)"), toolbar);
    R1_EXPECT_CROP(tooltipAt(1313, 184, "Flip horizontal"), crop("widget-tooltip-panel-icon-button", 1307, 178, theme, "panel"));
    R1_EXPECT_CROP(tooltipAt(151, 8, "Close Untitled"), crop("widget-tooltip-tab-close", 145, 2, theme, "panel"));
  }
  return r1test::finish();
}
