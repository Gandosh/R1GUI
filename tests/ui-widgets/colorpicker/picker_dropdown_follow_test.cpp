// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression test for the PickerDropdown list (phase 4 review M17): the list follows its field
//   when the field moves, and closes with it when the field is destroyed.
// Callers: CTest (fast tier). Calls: UiContext, PickerDropdown.

#include "TestSupport.h"
#include "r1ui/widgets/colorpicker/PickerDropdown.h"

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;

namespace {

class Spacer : public WidgetObject {
 public:
  const char* typeName() const override { return "Spacer"; }
  void onAttached() override {
    style().width = layout::Length::px(20);
    style().height = layout::Length::px(20);
    style().flexShrink = 0.0;
  }
};

}  // namespace

int main() {
  r1test::TestUi t(600, 400);
  UiContext& ui = t.ui;
  ui.rootStyle().direction = layout::FlexDirection::Row;
  ui.rootStyle().alignItems = layout::Align::Start;
  Spacer& spacer = ui.create<Spacer>(ui.root());
  PickerDropdown& dropdown = ui.create<PickerDropdown>(ui.root());
  dropdown.setItems({"RGB", "HSL", "HEX"});
  dropdown.style().width = layout::Length::px(100);
  dropdown.style().height = layout::Length::px(26);
  t.layout();
  dropdown.open();
  t.layout();
  R1_EXPECT(dropdown.isOpen());
  const auto host = ui.overlays().hostOf(ui.overlays().topmost());
  const int before = ui.absRect(host).x;

  // The field moves 100 px to the right; the list must follow once the watch has seen it.
  spacer.style().width = layout::Length::px(120);
  spacer.requestLayout();
  t.layout();
  ui.setTime(ui.now() + 200);
  ui.tick();
  t.layout();
  t.layout();
  R1_EXPECT(ui.absRect(host).x == ui.absRect(dropdown.id()).x);
  R1_EXPECT(ui.absRect(host).x >= before + 90);

  // Destroying the field closes the list.
  ui.destroy(dropdown.id());
  t.layout();
  ui.setTime(ui.now() + 200);
  ui.tick();
  t.layout();
  R1_EXPECT(ui.overlays().count() == 0);
  return r1test::finish();
}
