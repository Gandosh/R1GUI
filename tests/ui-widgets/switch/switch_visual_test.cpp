// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for Switch (sm, 28 x 16) against the reference crops widget-switch-idle,
//   -hover, -focus, -on and -on-hover in both themes. The reference shows no hover change, which the
//   test also pins (hover renders must match the idle crops).
// Why: pill colours, the 3 px thumb inset, the 12 px travel and the 1 px focus ring must match the
//   measured switch. The thumb's shadow-sm is not drawn (documented difference).
// Profile: "screen" (32 channels, 3% of pixels). The "default" profile (8 channels, 0.2%) is below the
//   antialiasing difference of rounded shapes between Chrome and our rasteriser (the pill and thumb
//   edges differ by up to 45 levels on a few dozen pixels), so it is not used for these crops.
// Callers: CTest (switch gpu: renders offscreen on a Vulkan device, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/switch/Switch.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
using r1test::visual::VisualSpec;
using r1ui::widgets::testing::VisualState;

}  // namespace

int main() {
  const r1test::visual::Build off = [](UiContext& ui, WidgetId parent) { return ui.create<Switch>(parent).id(); };
  const r1test::visual::Build on = [](UiContext& ui, WidgetId parent) {
    Switch& s = ui.create<Switch>(parent);
    s.setChecked(true);
    return s.id();
  };
  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    const auto spec = [&](const char* ref, VisualState state) { return VisualSpec{.reference = ref, .theme = theme, .state = state, .profile = "screen"}; };
    R1_EXPECT_MATCHES(off, spec("widget-switch-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(off, spec("widget-switch-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(off, spec("widget-switch-focus", VisualState::Focus));
    R1_EXPECT_MATCHES(on, spec("widget-switch-on", VisualState::Idle));
    R1_EXPECT_MATCHES(on, spec("widget-switch-on-hover", VisualState::Hover));
  }
  return r1test::finish();
}
