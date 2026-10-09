// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for the property-panel widgets against the reference captures, in both themes:
//   the panel header ("Rectangle": icon, 13 px semibold title, trailing action), the section "add"
//   icon button (26 x 26) in idle and hover, the section title and field label crops (through the
//   widgets that draw them) and three section headers cut out of the full rectangle-selected screen
//   (Layout, Appearance with the eye action, Fill with the plus action) including the 1 px separator
//   and the position of the title inside the 26 px header.
// Callers: CTest (section gpu: renders offscreen on a Vulkan device, no window). Metrics are printed
//   by the harness for every comparison.
#include "../g3support/RegionCompare.h"
#include "r1ui/widgets/section/Section.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;
using r1ui::theme::ThemeId;

}  // namespace

int main() {
  const r1test::visual::Build panelHeader = [](UiContext& ui, WidgetId parent) {
    PanelHeader& h = ui.create<PanelHeader>(parent, "Rectangle", "square");
    h.style().width = layout::Length::px(258);
    h.addAction("shapes", "Create component", [](ActionButton&) {});
    return h.id();
  };
  const r1test::visual::Build addButton = [](UiContext& ui, WidgetId parent) {
    ActionButton& b = ui.create<ActionButton>(parent, "plus", "section.action");
    b.setSize(26, 26);
    return b.id();
  };
  const r1test::visual::Build fieldLabelGroup = [](UiContext& ui, WidgetId parent) {
    FieldGroup& g = ui.create<FieldGroup>(parent, "Blend mode");
    g.style().width = layout::Length::px(114);
    return g.id();
  };
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    for (const auto state : {r1test::visual::VisualState::Idle, r1test::visual::VisualState::Hover}) {
      const char* suffix = state == r1test::visual::VisualState::Idle ? "idle" : "hover";
      R1_EXPECT_MATCHES(panelHeader, (r1test::visual::VisualSpec{.reference = std::string("widget-panel-header-rectangle-") + suffix, .theme = theme, .state = state, .profile = "text", .luminance = true}));
      R1_EXPECT_MATCHES(addButton, (r1test::visual::VisualSpec{.reference = std::string("widget-section-add-button-") + suffix, .theme = theme, .state = state, .profile = "icons"}));
    }
    // The label crop also contains the top edge of the field below it (rows 19 and up).
    R1_EXPECT_MATCHES(fieldLabelGroup, (r1test::visual::VisualSpec{.reference = "widget-panel-field-label-idle", .theme = theme, .profile = "text", .ignore = {{0, 19, 126, 4}}, .luminance = true}));

    // Sections cut out of the full screen: x 1182 (the panel), 258 wide.
    const r1test::visual::Build layoutSection = [](UiContext& ui, WidgetId parent) {
      PropertySection& s = ui.create<PropertySection>(parent, "Layout");
      s.style().width = layout::Length::px(258);
      return s.id();
    };
    const r1test::visual::Build appearanceSection = [](UiContext& ui, WidgetId parent) {
      PropertySection& s = ui.create<PropertySection>(parent, "Appearance");
      s.style().width = layout::Length::px(258);
      s.addAction("eye", "Hide", [](ActionButton&) {});
      return s.id();
    };
    const r1test::visual::Build fillSection = [](UiContext& ui, WidgetId parent) {
      PropertySection& s = ui.create<PropertySection>(parent, "Fill");
      s.style().width = layout::Length::px(258);
      s.addAction("plus", "Add fill", [](ActionButton&) {});
      return s.id();
    };
    R1_EXPECT_MATCHES_REGION(layoutSection, (r1test::visual::RegionSpec{.reference = "screen-rectangle-selected", .x = 1182, .y = 247, .w = 258, .h = 26, .theme = theme, .profile = "text", .tag = "section-header-layout", .luminance = true}));
    R1_EXPECT_MATCHES_REGION(appearanceSection, (r1test::visual::RegionSpec{.reference = "screen-rectangle-selected", .x = 1182, .y = 307, .w = 258, .h = 35, .theme = theme, .profile = "text", .tag = "section-header-appearance", .luminance = true}));
    R1_EXPECT_MATCHES_REGION(fillSection, (r1test::visual::RegionSpec{.reference = "screen-rectangle-selected", .x = 1182, .y = 491, .w = 258, .h = 35, .theme = theme, .profile = "text", .tag = "section-header-fill", .luminance = true}));
  }
  R1_EXPECT(r1ui::widgets::testing::validationMessageCount() == 0);  // the Debug tree runs the validation layers
  return r1test::finish();
}
