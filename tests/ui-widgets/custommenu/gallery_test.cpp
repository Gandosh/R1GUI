// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the fast test of the custom menu gallery page: it builds and lays out in a headless window, shows
//   three pies and three panels with the expected buttons, the live area reacts to a real right-button
//   gesture (the sample command runs and the status line says so), and everything is removed cleanly.
// Callers: CTest (label fast).
#include "TestSupport.h"
#include "r1ui/widgets/custommenu/CustomMenuPanel.h"
#include "r1ui/widgets/custommenu/GalleryCustomMenus.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/pie/PieMenu.h"
#include "r1ui/widgets/pie/PieTrigger.h"

int main() {
  using namespace r1ui::widgets;
  r1test::TestUi t(1200, 1400);
  t.ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
  buildGalleryCustomMenus(t.ui, t.ui.root());
  t.layout();

  std::vector<PieMenu*> pies;
  std::vector<CustomMenuPanel*> panels;
  PieTrigger* trigger = nullptr;
  t.ui.tree().forEachDescendant(t.ui.root(), [&](r1ui::core::tree::WidgetId id) {
    if (auto* p = t.ui.objectAs<PieMenu>(id)) pies.push_back(p);
    if (auto* p = t.ui.objectAs<CustomMenuPanel>(id)) panels.push_back(p);
    if (auto* p = t.ui.objectAs<PieTrigger>(id)) trigger = p;
  });
  R1_EXPECT(pies.size() == 3 && panels.size() == 3 && trigger != nullptr);
  R1_EXPECT(pies[0]->slotCount() == 8 && pies[0]->highlight() == 1 && pies[1]->slotCount() == 6 && pies[2]->slotCount() == 4);
  R1_EXPECT(panels[0]->buttonCount() == 9 && panels[1]->buttonCount() == 8 && panels[2]->buttonCount() == 3);
  // The missing command and the disabled one are shown dim.
  R1_EXPECT(panels[0]->button(8)->state().missing && !panels[0]->button(7)->state().available);
  R1_EXPECT(pies[0]->slots()[5].filled && !pies[0]->slots()[5].selectable && !pies[0]->slots()[6].filled);
  R1_EXPECT(pies[1]->slots()[3].missing);
  for (const PieMenu* pie : pies) {
    const auto r = t.ui.absRect(pie->id());
    R1_EXPECT(r.w == static_cast<int>(PieMenu::extent()) && r.h == r.w);
  }

  // A real gesture on the live area: flick up-right to run the second slot of the "Tools" pie.
  const auto area = t.ui.absRect(trigger->id());
  const double cx = area.x + area.w / 2.0;
  const double cy = area.y + area.h / 2.0;
  t.ui.setTime(1000);
  t.ui.pointerMove(cx, cy);
  t.ui.pointerDown(cx, cy, r1ui::core::events::Button::Right);
  R1_EXPECT(trigger->gestureActive());
  t.ui.setTime(1040);
  t.ui.pointerMove(cx + 40, cy);  // slot 2 (right): Rotate
  t.ui.setTime(1060);
  t.ui.pointerUp(cx + 40, cy, r1ui::core::events::Button::Right);
  R1_EXPECT(!trigger->gestureActive());
  t.layout();
  bool sawStatus = false;
  t.ui.tree().forEachDescendant(t.ui.root(), [&](r1ui::core::tree::WidgetId id) {
    if (const auto* label = t.ui.objectAs<Label>(id)) sawStatus = sawStatus || label->text().find("Ran: Rotate") != std::string::npos;
  });
  R1_EXPECT(sawStatus);
  t.layout();
  R1_EXPECT(t.ui.inputFaults() == 0 && !t.ui.overlays().any());

  t.ui.tree().forEachChild(t.ui.root(), [&](r1ui::core::tree::WidgetId child) { t.ui.destroy(child); });
  t.layout();
  R1_EXPECT(t.ui.widgetCount() <= 3);
  return r1test::finish();
}
