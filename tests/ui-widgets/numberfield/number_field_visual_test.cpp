// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for NumberField against the properties-panel crops of the reference app
//   (docs/spec/widgets.md 2.4 and 5.4): X (label), W (label, variable button, dropdown), Opacity
//   (glyph, % suffix, variable button), mixed X, and the bound W field, in the states the reference
//   captured, in both themes. Text crops are compared on luminance (the reference uses LCD subpixel
//   text) under the "text" profile.
// Callers: CTest (numberfield gpu: renders offscreen on a Vulkan device, no window).
// Notes: the reference "focus" and "bound" crops were captured with the pointer over the focused
//   field, so those states are built by focusing in the builder and rendering the hover state.
#include "VisualSupport.h"
#include "r1ui/widgets/numberfield/NumberField.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;
using r1test::visual::VisualSpec;
using r1test::visual::VisualState;
using r1ui::core::events::FocusReason;

NumberField& field(UiContext& ui, WidgetId parent) {
  NumberField& f = ui.create<NumberField>(parent);
  f.style().width = layout::Length::px(114);
  return f;
}

}  // namespace

int main() {
  const r1test::visual::Build x = [](UiContext& ui, WidgetId parent) {
    NumberField& f = field(ui, parent);
    f.setLabel("X");
    f.setValue(141);
    return f.id();
  };
  const r1test::visual::Build width = [](UiContext& ui, WidgetId parent) {
    NumberField& f = field(ui, parent);
    f.setLabel("W");
    f.setValue(300);
    f.setVariableButton(true);
    f.setDropdownButton(true);
    return f.id();
  };
  const r1test::visual::Build widthFocus = [](UiContext& ui, WidgetId parent) {
    NumberField& f = field(ui, parent);
    f.setLabel("W");
    f.setValue(300);
    f.setVariableButton(true);
    f.setDropdownButton(true);
    ui.router().focus(f.id(), FocusReason::Keyboard);  // Tab focus: edit mode with the text selected
    return f.id();
  };
  const r1test::visual::Build opacity = [](UiContext& ui, WidgetId parent) {
    NumberField& f = field(ui, parent);
    f.setLeadingIcon("blend");
    f.setSuffix("%");
    f.setValue(100);
    f.setVariableButton(true);
    return f.id();
  };
  const r1test::visual::Build mixed = [](UiContext& ui, WidgetId parent) {
    NumberField& f = field(ui, parent);
    f.setLabel("X");
    f.setMixed(true);
    return f.id();
  };
  const r1test::visual::Build bound = [](UiContext& ui, WidgetId parent) {
    NumberField& f = field(ui, parent);
    f.setLabel("W");
    f.setValue(300);
    f.setVariableButton(true);
    f.setDropdownButton(true);
    f.setBoundVariable("New number");
    ui.router().focus(f.id(), FocusReason::Pointer);
    return f.id();
  };

  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    const auto spec = [&](const char* reference, VisualState state) {
      return VisualSpec{.reference = reference, .theme = theme, .state = state, .profile = "text", .luminance = true};
    };
    R1_EXPECT_MATCHES(x, spec("widget-number-field-x-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(x, spec("widget-number-field-x-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(width, spec("widget-number-field-width-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(width, spec("widget-number-field-width-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(widthFocus, spec("widget-number-field-width-focus", VisualState::Hover));
    R1_EXPECT_MATCHES(opacity, spec("widget-number-field-opacity-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(opacity, spec("widget-number-field-opacity-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(mixed, spec("widget-number-field-x-mixed-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(mixed, spec("widget-number-field-x-mixed-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(bound, spec("widget-number-field-width-bound-idle", VisualState::Hover));
  }
  R1_EXPECT(r1ui::widgets::testing::validationMessageCount() == 0);  // Debug trees run with validation layers
  return r1test::finish();
}
