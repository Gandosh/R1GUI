// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for Label against the reference crops of OpenPencil's text roles: the section
//   title (11 px, weight 600, `surface`) and the field label (11 px, weight 400, `muted`), in both
//   themes, under the "text" tolerance profile. It is the template for a widget's visual test:
//   build the widget, name the reference crop, the state and the profile.
// Callers: CTest (label gpu: renders offscreen on a Vulkan device, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/label/Label.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;

}  // namespace

int main() {
  // The crops are 6 px of padding around the text box, whose width is the panel column (234 px).
  const r1test::visual::Build sectionTitle = [](UiContext& ui, WidgetId parent) {
    Label& label = ui.create<Label>(parent, "Layout", LabelRole::Heading);
    label.style().width = layout::Length::px(234);
    return label.id();
  };
  const r1test::visual::Build fieldLabel = [](UiContext& ui, WidgetId parent) {
    Label& label = ui.create<Label>(parent, "Blend mode", LabelRole::Caption);
    label.style().width = layout::Length::px(114);
    return label.id();
  };
  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    R1_EXPECT_MATCHES(sectionTitle, (r1test::visual::VisualSpec{.reference = "widget-panel-section-title-idle", .theme = theme, .profile = "text", .luminance = true}));
    // The crop also contains the top edge of the field below the label (rows 19 and up): not part of the Label.
    R1_EXPECT_MATCHES(fieldLabel, (r1test::visual::VisualSpec{.reference = "widget-panel-field-label-idle", .theme = theme, .profile = "text", .ignore = {{0, 19, 126, 4}}, .luminance = true}));
  }
  return r1test::finish();
}
