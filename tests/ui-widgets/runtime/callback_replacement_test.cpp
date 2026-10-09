// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression test for handlers that replace their own callback while it runs (phase 4 review
//   M18): the widgets call a copy, so the running function object (and what it captured) stays alive
//   until it returns.
// Why: calling onClick_() directly and then assigning a new onClick_ from inside destroyed the
//   running std::function; the captured state read after the replacement was freed memory.
// Callers: CTest (fast tier). Calls: Button, IconButton, Checkbox, Switch, Segmented.
#include <string>

#include "TestSupport.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/checkbox/Checkbox.h"
#include "r1ui/widgets/iconbutton/IconButton.h"
#include "r1ui/widgets/segmented/Segmented.h"
#include "r1ui/widgets/switch/Switch.h"

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;

namespace {

// The state a handler captures; it checks it is intact after its own replacement.
struct Payload {
  std::string text = std::string(200, 'x');  // long enough to live on the heap
  bool intact() const { return text == std::string(200, 'x'); }
};

void click(UiContext& ui, r1ui::core::tree::WidgetId id, double dx = 0.5) {
  const auto r = ui.absRect(id);
  const double x = r.x + r.w * dx;
  const double y = r.y + r.h / 2.0;
  ui.setTime(ui.now() + 1000);
  ui.pointerMove(x, y);
  ui.pointerDown(x, y);
  ui.pointerUp(x, y);
}

}  // namespace

int main() {
  int ran = 0;
  bool intact = true;
  const auto handler = [&ran, &intact](auto replace) {
    return [payload = Payload{}, replace, &ran, &intact](auto&&...) mutable {
      ++ran;
      replace();  // replaces the callback that is running right now
      intact = intact && payload.intact();
    };
  };
  {
    r1test::TestUi t;
    t.ui.rootStyle().direction = layout::FlexDirection::Row;
    Button& button = t.ui.create<Button>(t.ui.root(), "Go");
    IconButton& icon = t.ui.create<IconButton>(t.ui.root(), "x");
    Checkbox& check = t.ui.create<Checkbox>(t.ui.root(), "Check");
    Switch& toggle = t.ui.create<Switch>(t.ui.root());
    Segmented& seg = t.ui.create<Segmented>(t.ui.root());
    seg.setItems({{"A", ""}, {"B", ""}});
    t.layout();
    button.setOnClick(handler([&] { button.setOnClick([] {}); }));
    icon.setOnClick(handler([&] { icon.setOnClick([] {}); }));
    check.setOnChange(handler([&] { check.setOnChange([](bool) {}); }));
    toggle.setOnChange(handler([&] { toggle.setOnChange([](bool) {}); }));
    seg.setOnChange(handler([&] { seg.setOnChange([](int) {}); }));
    click(t.ui, button.id());
    click(t.ui, icon.id());
    click(t.ui, check.id(), 0.1);
    click(t.ui, toggle.id());
    click(t.ui, seg.id(), 0.9);
    R1_EXPECT(ran == 5);
    R1_EXPECT(intact);
  }
  return r1test::finish();
}
