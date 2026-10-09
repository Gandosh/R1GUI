// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for TextInput against the default-tone input crops of the reference app
//   (docs/spec/widgets.md 2.3): md and sm sizes in idle, hover, focus and filled-focus, in both
//   themes. The reference draws text with LCD subpixel antialiasing, so crops are compared on
//   luminance under the "text" profile.
// Callers: CTest (textinput gpu: renders offscreen on a Vulkan device, no window).
// Notes: the reference focus crops were captured with the pointer away from the field and the value
//   typed, so the filled state is built by focusing with the pointer (no select-all on focus).
#include "VisualSupport.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;
using r1test::visual::VisualSpec;
using r1test::visual::VisualState;

r1test::visual::Build makeBuild(TextInputSize size, double width, const char* placeholder, const char* value, bool clearable, bool focusedByPointer) {
  return [=](UiContext& ui, WidgetId parent) {
    TextInput& input = ui.create<TextInput>(parent, TextInputTone::Default, size);
    input.style().width = layout::Length::px(width);
    input.setPlaceholder(placeholder);
    input.setClearable(clearable);
    if (value[0] != '\0') input.setText(value);
    if (focusedByPointer) ui.router().focus(input.id(), r1ui::core::events::FocusReason::Pointer);
    return input.id();
  };
}

}  // namespace

int main() {
  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    const auto spec = [&](const char* reference, VisualState state) {
      return VisualSpec{.reference = reference, .theme = theme, .state = state, .profile = "text", .luminance = true};
    };
    // md: 208 px beside a button (idle, hover) and 262 px full width (focus, filled).
    R1_EXPECT_MATCHES(makeBuild(TextInputSize::Md, 208, "Paste room link or ID", "", false, false), spec("widget-text-input-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(makeBuild(TextInputSize::Md, 208, "Paste room link or ID", "", false, false), spec("widget-text-input-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(makeBuild(TextInputSize::Md, 262, "Enter your name", "", false, false), spec("widget-text-input-focus", VisualState::Focus));
    R1_EXPECT_MATCHES(makeBuild(TextInputSize::Md, 262, "Enter your name", "Alex", false, true), spec("widget-text-input-filled-focus", VisualState::Idle));
    // sm: the assets search field, 242 px wide.
    R1_EXPECT_MATCHES(makeBuild(TextInputSize::Sm, 242, "Search local components", "", false, false), spec("widget-text-input-sm-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(makeBuild(TextInputSize::Sm, 242, "Search local components", "", false, false), spec("widget-text-input-sm-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(makeBuild(TextInputSize::Sm, 242, "Search local components", "", false, false), spec("widget-text-input-sm-focus", VisualState::Focus));
    R1_EXPECT_MATCHES(makeBuild(TextInputSize::Sm, 242, "Search local components", "btn", true, true), spec("widget-text-input-sm-filled-focus", VisualState::Idle));
  }
  return r1test::finish();
}
