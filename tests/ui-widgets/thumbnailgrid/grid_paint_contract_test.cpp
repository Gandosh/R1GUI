// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression tests for what ThumbnailGrid does while it paints (phase 4 review M12, M16): the
//   selection callback that follows a finished filter is not called from paint but from the next tick,
//   and wrapping many long names neither flushes the shared shaped-run cache nor reshapes them every
//   frame.
// Callers: CTest (fast tier; the Painter records instances without a GPU).
#include <string>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/thumbnailgrid/ThumbnailGrid.h"

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;
namespace events = r1ui::core::events;

namespace {

void paintFrame(UiContext& ui) {
  ui.frame();
  r1ui::render::Painter painter;
  painter.begin(900, 700);
  ui.paint(painter);
  ui.finishPaint();
  painter.end();
}

VectorAssetModel makeModel(int n) {
  VectorAssetModel m;
  for (int i = 0; i < n; ++i) {
    GridItem item;
    item.key = 1000 + static_cast<uint64_t>(i);
    item.name = "Environment_Lighting_Preset_Variation_" + std::to_string(i) + "_final_v2_approved";
    item.icon = "file";
    m.items.push_back(item);
  }
  return m;
}

}  // namespace

int main() {
  {  // the selection callback of a finished filter runs from tick(), not from paint
    r1test::TestUi t(900, 700);
    t.ui.rootStyle().alignItems = layout::Align::Stretch;
    ThumbnailGrid& grid = t.ui.create<ThumbnailGrid>(t.ui.root());
    const VectorAssetModel model = makeModel(40);
    grid.setModel(&model);
    t.ui.frame();
    int notified = 0;
    grid.onSelectionChanged = [&] { ++notified; };
    t.ui.focusWidget(grid.id(), events::FocusReason::Keyboard);
    t.ui.keyDown(events::Key::Home);
    const int afterSelect = notified;
    R1_EXPECT(afterSelect >= 1);
    grid.setSearchText("no-item-has-this-text");
    R1_EXPECT(grid.filtering());
    paintFrame(t.ui);
    R1_EXPECT(!grid.filtering());
    R1_EXPECT(notified == afterSelect);  // painting made no application callback
    R1_EXPECT(t.ui.msUntilTick().has_value());
    t.ui.setTime(t.ui.now() + 1);
    t.ui.tick();
    R1_EXPECT(notified == afterSelect + 1);
  }
  {  // 300 long names: the second frame shapes nothing new and the run cache is not flushed
    r1test::TestUi t(900, 700);
    t.ui.rootStyle().alignItems = layout::Align::Stretch;
    ThumbnailGrid& grid = t.ui.create<ThumbnailGrid>(t.ui.root());
    const VectorAssetModel model = makeModel(300);
    grid.setModel(&model);
    grid.setViewMode(thumbs::ViewMode::Grid);
    t.ui.frame();
    paintFrame(t.ui);
    const size_t first = t.ui.text().cachedRuns();
    paintFrame(t.ui);
    const size_t second = t.ui.text().cachedRuns();
    R1_EXPECT(first > 0 && first < 2000);
    R1_EXPECT(second == first);
  }
  return r1test::finish();
}
