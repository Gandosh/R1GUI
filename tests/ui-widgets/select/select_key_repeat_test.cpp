// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression test for key auto-repeat on Select and its list (phase 4 review M10): holding
//   Enter must not open the list, choose the highlighted item and open the list again.
// Callers: CTest (fast tier). Calls: UiContext, Select.
#include "TestSupport.h"
#include "r1ui/widgets/select/Select.h"

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;
using r1ui::core::events::Key;

int main() {
  {  // a repeat of the key that opened the list does not choose from it
    r1test::TestUi t(400, 300);
    t.ui.rootStyle().direction = layout::FlexDirection::Column;
    Select& s = t.ui.create<Select>(t.ui.root());
    s.style().width = layout::Length::px(150);
    s.addItem("a", "a");
    s.addItem("b", "b");
    s.addItem("c", "c");
    int changes = 0;
    s.setOnChanged([&](size_t, std::string_view) { ++changes; });
    t.layout();
    t.ui.focusWidget(s.id(), r1ui::core::events::FocusReason::Keyboard);
    t.ui.keyDown(Key::Enter, 0, false);
    t.ui.setTime(10);
    t.ui.frame();
    t.ui.setTime(200);
    t.ui.tick();
    t.ui.frame();
    R1_EXPECT(s.isOpen());
    t.ui.keyDown(Key::Enter, 0, true);  // auto-repeat of the held key
    t.ui.keyDown(Key::Enter, 0, true);
    R1_EXPECT(s.isOpen() && changes == 0);
    t.ui.keyUp(Key::Enter, 0);
    t.ui.keyDown(Key::Enter, 0, false);  // a new press chooses
    R1_EXPECT(!s.isOpen() && changes == 1);
    // A repeat after the list closed must not reopen it.
    t.ui.keyDown(Key::Enter, 0, true);
    t.ui.frame();
    R1_EXPECT(!s.isOpen());
  }
  return r1test::finish();
}
