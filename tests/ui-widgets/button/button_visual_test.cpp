// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for Button against the reference crops of OpenPencil's buttons: the Share
//   button (accent, sm, icon + text, radius 6), the neutral text buttons ("Create collection", "Add"
//   with a leading plus and a trailing chevron), the share-popover accent button (md, centred
//   content) enabled and disabled, and the small accent button, in both themes. Hover states are
//   rendered with the pointer at the widget centre; the reference's disabled-hover crops cannot be
//   produced (a disabled widget gets no pointer) and are compared with the disabled idle render.
// Why: the widget's measured colours, sizes and content layout must match the reference within the
//   text profile (the reference draws LCD subpixel text, so text crops compare luminance).
// Callers: CTest (button gpu: renders offscreen on a Vulkan device, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/button/Button.h"

namespace {
using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
using r1test::visual::VisualSpec;
using r1ui::widgets::testing::VisualState;
namespace layout = r1ui::core::layout;

Button& make(UiContext& ui, WidgetId parent, const char* text, ButtonTone tone, ButtonSize size) {
  return ui.create<Button>(parent, text, tone, size);
}

}  // namespace

int main() {
  const r1test::visual::Build share = [](UiContext& ui, WidgetId parent) {
    Button& b = make(ui, parent, "Share", ButtonTone::Accent, ButtonSize::Sm);
    b.setIcon("share2");
    b.setRadius(6.0);
    return b.id();
  };
  const r1test::visual::Build textButton = [](UiContext& ui, WidgetId parent) { Button& b = make(ui, parent, "Create collection", ButtonTone::Neutral, ButtonSize::Sm); return b.id(); };
  const r1test::visual::Build addButton = [](UiContext& ui, WidgetId parent) {
    Button& b = make(ui, parent, "Add", ButtonTone::Neutral, ButtonSize::Sm);
    b.setIcon("plus");
    b.setTrailingIcon("chevron-down");
    b.setPaddingX(10.0);
    b.style().width = layout::Length::px(81);  // the crop is 1 px narrower than the button
    return b.id();
  };
  const auto accent = [](bool enabled) {
    return [enabled](UiContext& ui, WidgetId parent) {
      Button& b = make(ui, parent, "Share this file", ButtonTone::Accent, ButtonSize::Md);
      b.setIcon("share2");
      b.style().width = layout::Length::px(262);
      if (!enabled) b.setEnabled(false);
      return b.id();
    };
  };
  const r1test::visual::Build smallAccent = [](UiContext& ui, WidgetId parent) {
    Button& b = make(ui, parent, "Join", ButtonTone::Accent, ButtonSize::Sm);
    b.setWeight(400);
    b.setEnabled(false);
    return b.id();
  };

  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    const auto spec = [&](const char* ref, VisualState state) {
      return VisualSpec{.reference = ref, .theme = theme, .state = state, .profile = "text", .luminance = true};
    };
    R1_EXPECT_MATCHES(share, spec("widget-share-button-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(share, spec("widget-share-button-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(textButton, spec("widget-text-button-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(textButton, spec("widget-text-button-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(addButton, spec("widget-text-button-add-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(addButton, spec("widget-text-button-add-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(accent(true), spec("widget-button-accent-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(accent(true), spec("widget-button-accent-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(accent(false), spec("widget-button-accent-disabled-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(accent(false), spec("widget-button-accent-disabled-hover", VisualState::Idle));
    R1_EXPECT_MATCHES(smallAccent, spec("widget-button-accent-disabled-small-idle", VisualState::Idle));
  }
  return r1test::finish();
}
