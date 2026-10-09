// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: smoke oracle of buildGalleryDock: the page builds in both themes at several display scales,
//   lays out, shows the docked panels and the floating panel, paints (every style row and icon it
//   uses must resolve), survives a theme switch and a resize, and tears down cleanly.
// Callers: CTest (dock fast, no GPU; the painter records into a CPU list).
#include "DockTestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/dock/GalleryDock.h"

namespace {

using namespace dock_widget_test;
using r1ui::theme::ThemeId;

DockHost* findHost(UiContext& ui) {
  DockHost* found = nullptr;
  ui.tree().forEachDescendant(ui.root(), [&](WidgetId id) {
    if (auto* h = ui.objectAs<DockHost>(id)) found = h;
  });
  return found;
}

void paint(r1test::TestUi& t, int w, int h) {
  r1ui::render::Painter painter;
  painter.begin(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
  t.ui.paint(painter);
  t.ui.finishPaint();
  painter.end();
  R1_EXPECT(painter.stats().rejected == 0 && painter.stats().instances > 100, "something substantial was drawn");
}

void gallery_builds_lays_out_and_paints() {
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    for (const float scale : {1.0f, 1.5f, 2.0f}) {
      r1test::TestUi t(1000, 560, scale);
      t.services.theme().set(theme);
      t.ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
      buildGalleryDock(t.ui, t.ui.root());
      for (int i = 0; i < 5; ++i) t.ui.frame();
      DockHost* host = findHost(t.ui);
      R1_EXPECT(host != nullptr);
      if (host == nullptr) continue;
      R1_EXPECT(host->layout().areas().size() == 2, "the floating panel opened once the host had a size");
      R1_EXPECT(host->layout().isDocked(1) && host->layout().isDocked(3) && host->layout().isDocked(7));
      R1_EXPECT(host->contentOf(1).valid() && host->contentOf(3).valid() && host->contentOf(7).valid(), "every visible panel has content");
      R1_EXPECT(host->areaView(dock::kMainAreaId)->units().size() == 4, "four regions in the main area");
      paint(t, static_cast<int>(1000 * scale), static_cast<int>(560 * scale));
      // Theme switch and resize keep working.
      t.services.theme().set(theme == ThemeId::Dark ? ThemeId::Light : ThemeId::Dark);
      t.ui.setViewport(static_cast<int>(700 * scale), static_cast<int>(500 * scale), scale);
      for (int i = 0; i < 4; ++i) t.ui.frame();
      paint(t, static_cast<int>(700 * scale), static_cast<int>(500 * scale));
      R1_EXPECT(host->layout().validate().ok);
    }
  }
}

void gallery_dock_is_interactive() {
  r1test::TestUi t(1000, 560);
  t.ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
  buildGalleryDock(t.ui, t.ui.root());
  for (int i = 0; i < 5; ++i) t.ui.frame();
  DockHost* host = findHost(t.ui);
  DockTabStrip* strip = host->stripOf(3);
  const dock::Rect tab4 = strip->tabRect(*strip->indexOf(4));
  t.ui.pointerMove(tab4.x + 20, tab4.y + 10);
  t.ui.pointerDown(tab4.x + 20, tab4.y + 10);
  t.ui.pointerUp(tab4.x + 20, tab4.y + 10);
  t.ui.frame();
  R1_EXPECT(host->activePanel() == 4 && strip->frontPanel() == 4, "tabs of the gallery dock respond to input");
  // Destroying the page removes the floating window and everything else.
  const size_t widgets = t.ui.widgetCount();
  t.ui.destroy(t.ui.tree().lastChild(t.ui.root()) == t.ui.overlays().layer() ? t.ui.tree().prevSibling(t.ui.overlays().layer()) : t.ui.tree().lastChild(t.ui.root()));
  t.ui.frame();
  R1_EXPECT(t.ui.widgetCount() < widgets);
}

}  // namespace

int main() {
  gallery_builds_lays_out_and_paints();
  gallery_dock_is_interactive();
  return r1test::finish();
}
