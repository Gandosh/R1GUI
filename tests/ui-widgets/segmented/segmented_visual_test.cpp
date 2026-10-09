// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for Segmented against widget-segmented-control-idle and -hover (the selected
//   "File" item of the File / Assets control, md size) in both themes. The reference crop is one
//   117 x 22 item with 6 px around it, so the control is shifted by its 2 px padding (margin -2) to
//   put the item where the crop has it, and made 240 px wide so both items are 117 px wide.
// Why: container fill, the selected-muted item fill, radius and text placement must match. The
//   reference hover is on the selected item and looks like idle (no extra fill on the selected item).
// Callers: CTest (segmented gpu: renders offscreen on a Vulkan device, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/segmented/Segmented.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
using r1test::visual::VisualSpec;
using r1ui::widgets::testing::VisualState;
namespace layout = r1ui::core::layout;

}  // namespace

int main() {
  const r1test::visual::Build build = [](UiContext& ui, WidgetId parent) {
    Segmented& s = ui.create<Segmented>(parent, SegmentedSize::Md);
    s.setItems({{.text = "File"}, {.text = "Assets"}});
    s.setSelectedIndex(0);
    s.style().width = layout::Length::px(240);
    s.style().margin[layout::kLeft] = layout::Length::px(-2);
    s.style().margin[layout::kTop] = layout::Length::px(-2);
    return s.id();
  };
  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    const auto spec = [&](const char* ref, VisualState state) { return VisualSpec{.reference = ref, .theme = theme, .state = state, .profile = "text", .luminance = true}; };
    R1_EXPECT_MATCHES(build, spec("widget-segmented-control-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(build, spec("widget-segmented-control-hover", VisualState::Hover));
  }
  return r1test::finish();
}
