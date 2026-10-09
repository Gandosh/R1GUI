// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for ScrollArea and ScrollBar against the thin scrollbar captures of the design
//   panel (widget-scrollbar-idle and widget-scrollbar-thumb-hover) in both themes: the 10 px gutter,
//   the 6 px wide thumb with its 3 px radius at the position of the capture, and the hover colour.
//   The panel content next to the bar is the editor's, not the scroll area's, so only the gutter column
//   over the thumb's extent is compared (everything else is masked, see the ignore list).
// Callers: CTest (scroll gpu: renders offscreen on a Vulkan device, no window).
#include "../g3support/RegionCompare.h"
#include "r1ui/widgets/scroll/ScrollArea.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;
using r1ui::theme::ThemeId;
using r1test::visual::RegionSpec;

// Content that is as tall as the capture's design panel (the thumb of 513 px in a 783 px track means
// about 1195 px of content) and draws nothing.
class Tall : public WidgetObject {
 public:
  const char* typeName() const override { return "Tall"; }
  void onAttached() override {
    style().height = layout::Length::px(1195);
    style().flexShrink = 0.0;
  }
};

// The panel's scroll area at (6, 6) scrolled by 18 px; `hoverThumb` puts the pointer on the thumb.
r1test::visual::Build panelScroll(bool hoverThumb) {
  return [hoverThumb](UiContext& ui, WidgetId parent) {
    ui.rootStyle().alignItems = layout::Align::Start;
    ScrollArea& area = ui.create<ScrollArea>(parent);
    area.style().width = layout::Length::px(258);
    area.style().height = layout::Length::px(783);
    area.style().margin[layout::kLeft] = layout::Length::px(6);
    area.style().margin[layout::kTop] = layout::Length::px(6);
    area.style().flexShrink = 0.0;
    ui.create<Tall>(area.content());
    ui.frame();
    area.scrollTo(0, 18);
    ui.frame();
    if (hoverThumb) {
      const layout::Rect r = ui.absRect(area.id());
      ui.pointerMove(r.x + r.w - 5, r.y + 100);
    }
    return area.id();
  };
}

}  // namespace

int main() {
  // Only the gutter column (x 254..263) over the thumb (y 19..531 in the capture) is compared.
  const std::vector<r1test::visual::VisualSpec::Ignore> onlyGutter = {{0, 0, 254, 789}, {254, 0, 10, 19}, {254, 532, 10, 257}};
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    R1_EXPECT_MATCHES_REGION(panelScroll(false), (RegionSpec{.reference = "widget-scrollbar-idle", .x = 0, .y = 0, .w = 264, .h = 789, .theme = theme, .profile = "default", .tag = "scrollbar-idle", .ignore = onlyGutter}));
    R1_EXPECT_MATCHES_REGION(panelScroll(true), (RegionSpec{.reference = "widget-scrollbar-thumb-hover", .x = 0, .y = 0, .w = 264, .h = 789, .theme = theme, .profile = "default", .tag = "scrollbar-thumb-hover", .ignore = onlyGutter}));
  }
  R1_EXPECT(r1ui::widgets::testing::validationMessageCount() == 0);  // the Debug tree runs the validation layers
  return r1test::finish();
}
