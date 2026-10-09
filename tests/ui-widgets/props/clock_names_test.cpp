// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression test for a name clash found while wiring the preview's Editor screen: the command
//   layer's router clock and the property layer's undo clock were both called r1ui::widgets::UiClock, so
//   one translation unit could not include CommandServices.h and PropertyPanel.h together (and the
//   inline constructors of two different classes shared one linker name). The property one is now
//   PropsUiClock. The test includes both headers, builds both clocks over one context and checks that
//   each reads the context time through its own base interface.
// Callers: CTest (props fast, no GPU).
#include "TestSupport.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/props/PropertyPanel.h"

int main() {
  r1test::TestUi t(100, 100);
  t.ui.setTime(1234);
  r1ui::widgets::UiClock routerClock(t.ui);
  r1ui::widgets::PropsUiClock undoClock(t.ui);
  const r1ui::commands::Clock& asCommands = routerClock;
  const r1ui::props::Clock& asProps = undoClock;
  R1_EXPECT(asCommands.nowMs() == 1234);  // the router clock reads the context time
  R1_EXPECT(asProps.nowMs() == 1234);  // the undo clock reads the context time
  t.ui.setTime(5000);
  R1_EXPECT(asCommands.nowMs() == 5000 && asProps.nowMs() == 5000);  // both follow the context
  return r1test::finish();
}
