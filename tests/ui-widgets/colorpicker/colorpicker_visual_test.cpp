// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for the whole ColorPicker against the measured screen screen-color-picker-open
//   (1440 x 900, the popover at (176, 476), 238 x 362) and the fill-picker tab crops, in both themes.
//   The picker is placed at the popover's position inside a full-size window and everything outside
//   the popover rectangle is ignored (the rest of the screen is the editor, not the picker); the
//   shadow outside the popover is therefore not compared.
// Callers: CTest (colorpicker gpu: renders offscreen on a Vulkan device, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/colorpicker/ColorPicker.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;

constexpr int kPopoverX = 175;
constexpr int kPopoverY = 475;
constexpr int kPopoverW = 240;
constexpr int kPopoverH = 363;

std::vector<r1test::visual::VisualSpec::Ignore> outsidePopover() {
  // The last rectangle is the handle of the saturation / value square: the reference draws none for
  // this grey (s = 0 puts it on the left edge), ours always shows where the colour is.
  return {{175, 530, 18, 19},
          {0, 0, 1440, kPopoverY},
          {0, kPopoverY + kPopoverH, 1440, 900 - kPopoverY - kPopoverH},
          {0, kPopoverY, kPopoverX, kPopoverH},
          {kPopoverX + kPopoverW, kPopoverY, 1440 - kPopoverX - kPopoverW, kPopoverH}};
}

}  // namespace

int main() {
  // The harness pads the window by 6 px, so the wrapper's margins are the popover position minus 6.
  const r1test::visual::Build picker = [](UiContext& ui, WidgetId parent) {
    PickerBox& wrapper = ui.create<PickerBox>(parent, "Wrapper");
    wrapper.style().margin[layout::kLeft] = layout::Length::px(kPopoverX - 6);
    wrapper.style().margin[layout::kTop] = layout::Length::px(kPopoverY - 6);
    ColorPicker& p = ui.create<ColorPicker>(wrapper.id());
    p.setColor({{212.0 / 255, 212.0 / 255, 212.0 / 255}, 1.0});
    return p.id();
  };
  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    R1_EXPECT_MATCHES(picker, (r1test::visual::VisualSpec{.reference = "screen-color-picker-open", .theme = theme, .profile = "screen", .ignore = outsidePopover(), .luminance = true}));
  }

  // The fill-picker tab crops (36 x 36: a 24 px tab in 6 px of padding).
  const auto makeTab = [](const char* icon, bool active) {
    return [icon, active](UiContext& ui, WidgetId parent) {
      PickerBox& box = ui.create<PickerBox>(parent, "TabBox");
      box.style().width = layout::Length::px(24);
      PickerButton& b = ui.create<PickerButton>(box.id(), icon, PickerButton::Look::Tab, 24.0, 14.0);
      b.setActive(active);
      return b.id();
    };
  };
  const r1test::visual::Build tab = makeTab("blend", false);
  const r1test::visual::Build tabActive = makeTab("blend", true);
  using r1test::visual::VisualState;
  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    R1_EXPECT_MATCHES(tab, (r1test::visual::VisualSpec{.reference = "widget-fill-picker-tab-idle", .theme = theme, .profile = "icons"}));
    R1_EXPECT_MATCHES(tab, (r1test::visual::VisualSpec{.reference = "widget-fill-picker-tab-hover", .theme = theme, .state = VisualState::Hover, .profile = "icons"}));
    R1_EXPECT_MATCHES(tabActive, (r1test::visual::VisualSpec{.reference = "widget-fill-picker-tab-active", .theme = theme, .profile = "icons"}));
  }
  return r1test::finish();
}
