// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: smoke oracle for the containers gallery page: it builds in both themes at several display
//   scales, lays out inside a window, paints (every icon and style row it uses must resolve), reacts to
//   a theme switch and a resize, and contains the expected variants (an overflowing tab bar, a
//   renaming tree, a collapsed splitter pane, a vertical toolbar, a focused action button).
// Callers: CTest (section fast, no GPU; the painter records into a CPU list).
#include <cmath>
#include <string>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/section/GalleryContainers.h"
#include "r1ui/widgets/section/Section.h"
#include "r1ui/widgets/splitter/Splitter.h"
#include "r1ui/widgets/tabbar/TabBar.h"
#include "r1ui/widgets/toolbar/Toolbar.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace {

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;
using r1ui::theme::ThemeId;

struct Counts {
  int tabBars = 0, overflowing = 0, trees = 0, renaming = 0, splitters = 0, collapsed = 0, toolbars = 0, vertical = 0, sections = 0, areas = 0;
};

Counts count(UiContext& ui) {
  Counts c;
  ui.tree().forEachDescendant(ui.root(), [&](r1ui::core::tree::WidgetId id) {
    WidgetObject* o = ui.object(id);
    if (auto* bar = dynamic_cast<TabBar*>(o)) {
      ++c.tabBars;
      if (bar->overflowing()) ++c.overflowing;
    } else if (auto* tree = dynamic_cast<TreeView*>(o)) {
      ++c.trees;
      if (tree->renaming()) ++c.renaming;
    } else if (auto* split = dynamic_cast<Splitter*>(o)) {
      ++c.splitters;
      for (size_t i = 0; i < split->paneCount(); ++i) c.collapsed += split->isCollapsed(i) ? 1 : 0;
    } else if (auto* tb = dynamic_cast<Toolbar*>(o)) {
      ++c.toolbars;
      if (tb->orientation() == ToolbarOrientation::Vertical) ++c.vertical;
    } else if (dynamic_cast<PropertySection*>(o) != nullptr) {
      ++c.sections;
    } else if (dynamic_cast<ScrollArea*>(o) != nullptr) {
      ++c.areas;
    }
  });
  return c;
}

void paint(r1test::TestUi& t, int physicalWidth, int physicalHeight) {
  r1ui::render::Painter painter;
  painter.begin(static_cast<uint32_t>(physicalWidth), static_cast<uint32_t>(physicalHeight));
  t.ui.paint(painter);
  t.ui.finishPaint();
  painter.end();
}

}  // namespace

int main() try {
  for (const float scale : {1.0f, 1.5f, 2.0f}) {
    for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
      r1test::TestUi t(1300, 1500, scale);
      t.services.theme().set(theme);
      t.ui.rootStyle().direction = layout::FlexDirection::Column;
      buildGalleryContainers(t.ui, t.ui.root());
      t.layout();
      paint(t, 1300, 1500);
      const Counts c = count(t.ui);
      R1_EXPECT(c.tabBars == 2 && c.overflowing == 1);
      R1_EXPECT(c.trees == 6 && c.renaming == 1);
      R1_EXPECT(c.splitters == 3 && c.collapsed == 1);
      R1_EXPECT(c.toolbars == 3 && c.vertical == 1);
      R1_EXPECT(c.sections == 3 && c.areas == 4);
      // The page lays out inside the window and every cell has a size.
      const layout::Rect page = t.ui.absRect(t.ui.tree().firstChild(t.ui.root()));
      R1_EXPECT(std::abs(page.w - 1300.0 / scale) <= 1.0 && page.h > 400);
      // A theme switch and a smaller window repaint without trouble.
      t.services.theme().toggle();
      t.ui.setViewport(static_cast<int>(700 * scale), static_cast<int>(900 * scale), scale);
      t.layout();
      paint(t, static_cast<int>(700 * scale), static_cast<int>(900 * scale));
    }
  }
  return r1test::finish();
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}
