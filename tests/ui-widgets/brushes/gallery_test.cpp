// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the fast test of the brush library gallery page: it builds and lays out in a headless window, shows
//   the expected popups in the expected states, paints without error, and is removed cleanly (no widget,
//   timer or animation is left behind).
// Callers: CTest (label fast).
#include "TestSupport.h"
#include "r1ui/widgets/brushes/BrushLibraryPopup.h"
#include "r1ui/widgets/brushes/GalleryBrushes.h"
#include "r1ui/widgets/section/Section.h"

int main() {
  using namespace r1ui::widgets;
  r1test::TestUi t(1300, 1800);
  t.ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
  const size_t before = t.ui.widgetCount();
  SectionBox& wrapper = t.ui.create<SectionBox>(t.ui.root());
  wrapper.style().direction = r1ui::core::layout::FlexDirection::Column;
  buildGalleryBrushes(t.ui, wrapper.id());
  t.layout();

  std::vector<BrushLibraryPopup*> popups;
  t.ui.tree().forEachDescendant(t.ui.root(), [&](r1ui::core::tree::WidgetId id) {
    if (auto* p = t.ui.objectAs<BrushLibraryPopup>(id)) popups.push_back(p);
  });
  R1_EXPECT(popups.size() == 8);
  if (popups.size() == 8) {
    namespace br = r1ui::commands::brushes;
    // Browsing: three recents, then the list with the favourites first.
    R1_EXPECT(popups[0]->result().recentCount == 3 && popups[0]->result().tiles.size() == 27 && popups[0]->text().empty());
    // Typing s: four brushes with their next letters.
    R1_EXPECT(popups[1]->text() == "s" && popups[1]->result().matchCount == 4);
    // Clay is exact and first.
    R1_EXPECT(popups[2]->result().matchCount == 2 && popups[2]->result().tiles[0].exact);
    R1_EXPECT(popups[3]->mode() == br::QueryMode::SearchAnywhere && popups[3]->result().matchCount == 2);
    R1_EXPECT(popups[4]->assigning() && popups[4]->assignFeedback().find("other brushes") != std::string::npos);
    R1_EXPECT(popups[5]->menuOpen());
    R1_EXPECT(popups[6]->result().tiles.empty());
    R1_EXPECT(popups[7]->category() == "Surface" && popups[7]->result().totalCount == 6);
    for (const BrushLibraryPopup* popup : popups) {
      const auto r = t.ui.absRect(popup->id());
      R1_EXPECT(r.w == 600 && r.h == 420);
    }
  }
  r1ui::render::Painter painter;
  painter.begin(static_cast<uint32_t>(t.ui.viewportWidth()), static_cast<uint32_t>(t.ui.viewportHeight()));
  t.ui.paint(painter);
  t.ui.finishPaint();
  painter.end();

  // Removing the page leaves nothing behind.
  t.ui.destroy(wrapper.id());
  t.layout();
  R1_EXPECT(t.ui.widgetCount() == before);
  R1_EXPECT(t.ui.animationCount() == 0);
  return r1test::finish();
}
