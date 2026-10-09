// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for IconButton against the reference crops of OpenPencil's icon buttons: the
//   20 px "small" button (apply variable glyph) and the same button as the variable trigger, the
//   26 px panel button (flip horizontal), the section add button, the 24 px dialog close button and
//   the 16 px add-stop button, idle and hover, in both themes. The sm size changes the glyph only on
//   hover (measured), the md size also fills with `hover`.
// Why: sizes, radii, glyph sizes and the hover colours must match the reference crops.
// Notes on the crops: the two small buttons sit inside a number field, so their crop shows the field
//   (`panel-field`, `panel-field-hover` while hovered, because the field itself is hovered) behind
//   the button; the test renders on that colour and ignores the rows above and below the field
//   (3 and 4 px of panel).
// Callers: CTest (iconbutton gpu: renders offscreen on a Vulkan device, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/iconbutton/IconButton.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
using r1test::visual::VisualSpec;
using r1ui::widgets::testing::VisualState;

struct Variant {
  const char* name;  // reference name without state suffix
  const char* icon;
  IconButtonSize size;
  double box;        // 0 = the size class' box
  double iconSize;
  bool inField;      // the crop shows a number field behind the button
};

}  // namespace

int main() {
  const Variant variants[] = {
      {"widget-icon-button-small", "apply-variable", IconButtonSize::Sm, 0, 14, true},
      {"widget-icon-button-variable-trigger", "apply-variable", IconButtonSize::Sm, 0, 14, true},
      {"widget-icon-button-panel", "flip-horizontal2", IconButtonSize::Md, 0, 14, false},
      {"widget-section-add-button", "plus", IconButtonSize::Md, 0, 14, false},
      {"widget-dialog-close-button", "x", IconButtonSize::Md, 24, 16, false},
      {"widget-icon-button-add-stop", "plus", IconButtonSize::Sm, 16, 12, false},
  };
  for (const Variant& v : variants) {
    const r1test::visual::Build build = [v](UiContext& ui, WidgetId parent) {
      IconButton& b = ui.create<IconButton>(parent, v.icon, v.size);
      if (v.box > 0.0) b.setBoxSize(v.box);
      b.setIconSize(v.iconSize);
      b.setHoverFill(v.size == IconButtonSize::Md);
      return b.id();
    };
    for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
      for (const bool hover : {false, true}) {
        VisualSpec spec;
        spec.reference = std::string(v.name) + (hover ? "-hover" : "-idle");
        spec.theme = theme;
        spec.state = hover ? VisualState::Hover : VisualState::Idle;
        spec.profile = "icons";
        if (v.inField) {
          spec.background = hover ? "panel-field-hover" : "panel-field";
          spec.ignore = {{0, 0, 32, 3}, {0, 28, 32, 4}};
        }
        R1_EXPECT_MATCHES(build, spec);
      }
    }
  }
  return r1test::finish();
}
