// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for Checkbox against the reference crops widget-checkbox-clip-content-idle,
//   -hover, -checked and -checked-hover (the browser's native 13 x 13 checkbox with accent-color) in
//   both themes. Mixed, focus and disabled have no reference (the native control was only captured
//   in these four states); they are covered by unit tests.
// Why: box colours, radius and the check mark must match the measured control.
// Profile: "screen" (32 channels, 3% of pixels) for the same reason as the switch test: the native
//   box has soft corner pixels and a check mark that is a polygon in the reference and two strokes
//   here, so the checked crops land at 3.0 to 4.0% (reported, not hidden: they fail the profile).
// Callers: CTest (checkbox gpu: renders offscreen on a Vulkan device, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/checkbox/Checkbox.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
using r1test::visual::VisualSpec;
using r1ui::widgets::testing::VisualState;

}  // namespace

int main() {
  const r1test::visual::Build unchecked = [](UiContext& ui, WidgetId parent) { return ui.create<Checkbox>(parent).id(); };
  const r1test::visual::Build checked = [](UiContext& ui, WidgetId parent) {
    Checkbox& c = ui.create<Checkbox>(parent);
    c.setChecked(true);
    return c.id();
  };
  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    const auto spec = [&](const char* ref, VisualState state) { return VisualSpec{.reference = ref, .theme = theme, .state = state, .profile = "screen"}; };
    R1_EXPECT_MATCHES(unchecked, spec("widget-checkbox-clip-content-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(unchecked, spec("widget-checkbox-clip-content-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(checked, spec("widget-checkbox-clip-content-checked", VisualState::Idle));
    R1_EXPECT_MATCHES(checked, spec("widget-checkbox-clip-content-checked-hover", VisualState::Hover));
  }
  return r1test::finish();
}
