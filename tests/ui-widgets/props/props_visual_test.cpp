// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for the generated property panel parts against the reference captures, in both
//   themes: the X field of a two-axis row (idle, hover, mixed idle and hover; docs/spec/widgets.md 2.4),
//   the enum row's Select trigger (docs/spec/widgets.md 2.6), and the header of a generated category
//   section cut out of the full rectangle-selected screen (Appearance: title position, the 1 px
//   separator, the 35 px header with an action). Text crops are compared on luminance (the reference
//   draws LCD subpixel text) under the "text" profile.
// Callers: CTest (props gpu: renders offscreen on a Vulkan device, no window).
// Notes: the rows are the real PropertyRowView / buildPropertyCategory output over a context, built
//   captionless for the field crops so the field sits at the crop's top-left as the reference crop does.
//   The generated header carries the reset-category arrow where the reference shows its own action
//   icon (the eye); that 26 px button is masked, the rest of the header is compared.
#include <memory>

#include "../g3support/RegionCompare.h"
#include "r1ui/props/PanelState.h"
#include "r1ui/props/PropertySet.h"
#include "r1ui/widgets/numberfield/NumberField.h"
#include "r1ui/widgets/props/PropertyPanel.h"
#include "r1ui/widgets/props/PropertyRow.h"
#include "r1ui/widgets/select/Select.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::props;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;
using r1test::visual::VisualSpec;
using r1test::visual::VisualState;

enum class Blend { PassThrough, Normal, Darken, Multiply, ColorBurn, Lighten, Screen, Overlay };

struct Sample {
  Vec2 point{141.0, 72.0};
  Blend blend = Blend::PassThrough;
};

const PropertySet<Sample>& sampleSet() {
  static const PropertySet<Sample> set = [] {
    PropertySet<Sample> s("Sample");
    s.category("Appearance");
    s.add("point", "Position", &Sample::point).range(-100000.0, 100000.0);
    s.add("blend", "Blend mode", &Sample::blend)
        .value("pass-through", Blend::PassThrough, "Pass through")
        .value("normal", Blend::Normal, "Normal")
        .value("darken", Blend::Darken, "Darken")
        .value("multiply", Blend::Multiply, "Multiply")
        .value("color-burn", Blend::ColorBurn, "Color burn")
        .value("lighten", Blend::Lighten, "Lighten")
        .value("screen", Blend::Screen, "Screen")
        .value("overlay", Blend::Overlay, "Overlay");
    return s;
  }();
  return set;
}

// Context and objects live as long as the build function (the harness keeps the copy alive).
struct Fixture {
  Sample a, b;
  PropertyContext context;
  PanelState state;
  explicit Fixture(bool mixed) {
    b.point = {99.0, 72.0};
    std::vector<Target> targets = {targetOf(a, sampleSet())};
    if (mixed) targets.push_back(targetOf(b, sampleSet()));
    context.setSelection(targets);
  }
};

PropertyRowView& captionless(UiContext& ui, WidgetId parent, Fixture& f, const char* row, double width) {
  PropertyRowOptions options;
  options.caption = false;
  PropertyRowView& view = ui.create<PropertyRowView>(parent, f.context, *f.context.findRow(row), options);
  view.style().width = layout::Length::px(width);
  return view;
}

}  // namespace

int main() {
  const auto single = std::make_shared<Fixture>(false);
  const auto two = std::make_shared<Fixture>(true);
  const r1test::visual::Build x = [single](UiContext& ui, WidgetId parent) { return captionless(ui, parent, *single, "point", 234).number(0)->id(); };
  const r1test::visual::Build mixedX = [two](UiContext& ui, WidgetId parent) { return captionless(ui, parent, *two, "point", 234).number(0)->id(); };
  const r1test::visual::Build blend = [single](UiContext& ui, WidgetId parent) { return captionless(ui, parent, *single, "blend", 114).select()->id(); };

  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    const auto spec = [&](const char* reference, VisualState state) {
      return VisualSpec{.reference = reference, .theme = theme, .state = state, .profile = "text", .luminance = true};
    };
    R1_EXPECT_MATCHES(x, spec("widget-number-field-x-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(x, spec("widget-number-field-x-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(mixedX, spec("widget-number-field-x-mixed-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(mixedX, spec("widget-number-field-x-mixed-hover", VisualState::Hover));
    R1_EXPECT_MATCHES(blend, spec("widget-select-trigger-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(blend, spec("widget-select-trigger-hover", VisualState::Hover));

    // The generated Appearance category header against the header cut out of the full screen. The header
    // action is the reset arrow, not the eye of the reference: its 26 px button (right of the header) is masked.
    const r1test::visual::Build header = [single](UiContext& ui, WidgetId parent) {
      PanelCategory category;
      category.name = "Appearance";
      PropertyPanelOptions options;
      const CategoryWidgets parts = buildPropertyCategory(ui, parent, single->context, category, options);
      ui.object(parts.section)->style().width = layout::Length::px(258);
      return parts.section;
    };
    R1_EXPECT_MATCHES_REGION(header, (r1test::visual::RegionSpec{.reference = "screen-rectangle-selected", .x = 1182, .y = 307, .w = 258, .h = 35, .theme = theme, .profile = "text",
                                                                 .tag = "props-category-header-appearance", .ignore = {{214, 0, 44, 35}}, .luminance = true}));
  }
  R1_EXPECT(r1ui::widgets::testing::validationMessageCount() == 0);  // Debug trees run with validation layers
  return r1test::finish();
}
