// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the visual oracle of the popover surface against widget-popover-grid-settings in both
//   themes: a 256 x 183 popover with 12 px padding at the reference position. The content of the
//   reference popover (two switches, a number input, labels) belongs to other widgets, so the content
//   rectangle is ignored; what is compared is the surface: radius 8, 1 px border, background, shadow
//   xl and the padding area.
// Callers: CTest (label gpu, offscreen, no window).
#include "../menu/ScreenCompare.h"
#include "r1ui/widgets/popover/Popover.h"

namespace {

using namespace r1test::screen;
using r1ui::theme::ThemeId;

struct Spacer : WidgetObject {
  Spacer(double w, double h) : w_(w), h_(h) {}
  const char* typeName() const override { return "Spacer"; }
  void onAttached() override {
    style().width = layout::Length::px(w_);
    style().height = layout::Length::px(h_);
  }
  double w_;
  double h_;
};

BuildFn gridPopover() {
  return [](UiContext& ui, WidgetId) {
    PopoverOptions o;
    o.anchorRect = {710, 656, 0, 0};
    o.placement = Placement::Manual;
    o.padding = 12.0;
    o.width = 256.0;
    o.focusOnOpen = false;
    const PopoverHandle handle = openPopover(ui, o);
    ui.create<Spacer>(handle.host, 230.0, 157.0);  // 256 - 2 * 13 wide, 183 - 2 * 13 high
    return handle.host;
  };
}

}  // namespace

int main() {
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    Case c;
    c.reference = "widget-popover-grid-settings";
    c.clipX = 704;
    c.clipY = 650;
    c.theme = theme;
    c.profile = "screen";
    c.page = false;
    // The content of the reference popover (other widgets) and the edge of the floating toolbar at the
    // bottom of the crop.
    c.ignore = {{19, 19, 230, 157}, {0, 192, 268, 3}};
    R1_EXPECT_CROP(gridPopover(), c);
  }
  return r1test::finish();
}
