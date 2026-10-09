// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GPU oracle for the overlay layer's painting: a menu popup is drawn above the normal tree
//   without being clipped by its parent (surface colour, border and a shadow darker than the window
//   behind it), a modal scrim dims everything behind a dialog, and the Debug validation layer reports
//   nothing while the shadow, border and text pipelines are exercised through the real device.
// Callers: CTest (label gpu, offscreen, no window).
#include <algorithm>

#include "VisualSupport.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/overlay/OverlayHost.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;

uint8_t red(const image::Image& img, uint32_t x, uint32_t y) { return img.rgba[(size_t{y} * img.width + x) * 4]; }

}  // namespace

int main() {
  const VisualPaths paths = r1test::visual::paths();
  RenderSpec spec;
  spec.width = 200;
  spec.height = 120;
  spec.padding = 0;
  spec.background = "canvas";

  const BuildFn plain = [](UiContext& ui, WidgetId parent) { return ui.create<Label>(parent, "x").id(); };
  const image::Image base = renderWidget(plain, spec, paths);

  // A menu placed below an anchor near the top-left: surface colour inside, shadow just outside.
  const BuildFn menu = [](UiContext& ui, WidgetId parent) {
    OverlayOptions o;
    o.anchor = {20, 10, 60, 20};
    o.surface = OverlaySurface::Menu;
    const OverlayHandle h = ui.overlays().open(o);
    ui.create<Label>(h.host, "Copy");
    ui.create<Label>(h.host, "Paste");
    return ui.create<Label>(parent, "x").id();
  };
  const image::Image withMenu = renderWidget(menu, spec, paths);
  // Menu host: x 20.., y 30.. ; the surface is the panel colour, the window behind is `canvas`.
  const auto& theme = sharedServices(paths).theme();
  // (30, 33) lies in the padding above the first label: (30, 36) is inside the glyphs of "Copy", whose ink
  // depends on the calibrated text weight.
  R1_EXPECT(red(withMenu, 30, 33) == theme.color("panel")->r);
  R1_EXPECT(red(base, 30, 33) == theme.color("canvas")->r);
  // The shadow darkens the canvas a few pixels outside the menu (below it) compared with no menu.
  uint32_t shadowed = 0;
  for (uint32_t y = 62; y < 90; ++y) {
    for (uint32_t x = 30; x < 70; ++x) shadowed += red(withMenu, x, y) < red(base, x, y) ? 1u : 0u;
  }
  R1_EXPECT(shadowed > 50);

  // A modal dialog with a scrim dims the whole window behind it.
  const BuildFn dialog = [](UiContext& ui, WidgetId parent) {
    OverlayOptions o;
    o.surface = OverlaySurface::Dialog;
    o.placement = Placement::Center;
    o.modal = true;
    o.scrim = true;
    const OverlayHandle h = ui.overlays().open(o);
    ui.create<Label>(h.host, "Are you sure?", LabelRole::Title);
    return ui.create<Label>(parent, "x").id();
  };
  const image::Image withDialog = renderWidget(dialog, spec, paths);
  R1_EXPECT(red(withDialog, 3, 3) < red(base, 3, 3));  // scrim over the corner
  R1_EXPECT(red(withDialog, 100, 60) != red(withDialog, 3, 3));  // the dialog surface in the middle is not dimmed like the corner

  R1_EXPECT(validationMessageCount() == 0);
  return r1test::finish();
}
