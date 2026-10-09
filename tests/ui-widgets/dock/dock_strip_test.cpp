// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of DockTabStrip geometry and presentation: tab sizes and caps (spec 02 rule 9),
//   application-page strips, hidden close buttons that keep their space, the lock glyph, tooltips
//   with the full title, titles that are empty, huge or invalid UTF-8, the drag gap and the lifted
//   tab, and slot arithmetic for a dragged tab.
// Why: the strip is the most event-dense widget of the dock; its geometry decides where drops land.
// Callers: CTest (label fast).
#include "DockTestSupport.h"

using namespace dock_widget_test;

namespace {

void tab_sizes_follow_the_rules() {
  DockRig rig(1000, 600, 12, DockHostOptions{});
  std::vector<dock::PanelId> ids;
  for (dock::PanelId i = 1; i <= 12; ++i) ids.push_back(i);
  rig.setRoot(stackOf(ids));
  DockTabStrip* strip = rig.strip(1);
  const dock::Rect t0 = strip->tabRect(0);
  R1_EXPECT(t0.h == 25 && std::abs(t0.w - 1000.0 / 12.0) < 0.01, "equal widths filling the strip");
  rig.t.ui.setViewport(300, 600, 1.0f);
  rig.settle();
  R1_EXPECT(strip->overflowing() && strip->tabRect(0).w == 60, "tabs stop shrinking at 60 px (D12)");

  DockRig few(1000, 600, 3, DockHostOptions{});
  few.setRoot(stackOf({1, 2}));
  R1_EXPECT(few.strip(1)->tabRect(0).w == 160, "capped at 160 px");
  R1_EXPECT(!few.strip(1)->overflowing());
  R1_EXPECT(few.strip(1)->tabRect(1).x == few.strip(1)->tabRect(0).x + 160, "tabs are adjacent");
}

void application_pages_get_tall_wide_tabs() {
  DockRig rig(1000, 600, 4, DockHostOptions{}, false);
  // Mark panels 1 and 2 as application pages through a fresh registry.
  PanelRegistry registry;
  for (dock::PanelId id = 1; id <= 3; ++id) {
    PanelDescriptor d;
    d.id = id;
    d.title = "Page " + std::to_string(id);
    d.kind = id <= 2 ? dock::PanelKind::ApplicationPage : dock::PanelKind::Panel;
    registry.add(d);
  }
  r1test::TestUi t(1000, 600);
  InWindowFloatingBackend backend(t.ui, t.ui.root());
  DockHost& host = t.ui.create<DockHost>(t.ui.root(), registry, backend);
  dock::DockLayoutResult created = dock::DockLayout::create(registry.infos(), {}, dock::Node::split(dock::Axis::Column, {stackOf({1, 2}), stackOf({3})}));
  host.setLayout(std::move(*created.layout));
  for (int i = 0; i < 4; ++i) t.ui.frame();
  DockTabStrip* pages = host.stripOf(1);
  DockTabStrip* normal = host.stripOf(3);
  R1_EXPECT(pages != nullptr && normal != nullptr);
  R1_EXPECT(t.ui.absRect(pages->id()).h == 50 && pages->tabRect(0).w == 210, "application pages: 210 x 50 (rule 9)");
  R1_EXPECT(t.ui.absRect(normal->id()).h == 25, "ordinary panels keep 25 px");
  // An application page may not be split off; dropping it on a body floats it instead.
  const dock::Rect body = toDockRect(t.ui.absRect(host.contentOf(3)));
  t.ui.pointerMove(pages->tabRect(1).x + 80, 25);
  t.ui.pointerDown(pages->tabRect(1).x + 80, 25);
  for (int i = 1; i <= 6; ++i) {
    t.ui.pointerMove(pages->tabRect(1).x + 80 + i * 10, 25 + i * 30);
    t.ui.frame();
  }
  const dock::Point edge{body.x + 10, body.y + body.h / 2};
  t.ui.pointerMove(edge.x, edge.y);
  t.ui.pointerUp(edge.x, edge.y);
  for (int i = 0; i < 3; ++i) t.ui.frame();
  const std::optional<dock::PanelSlot> slot = host.layout().locate(2);
  R1_EXPECT(slot && slot->area != dock::kMainAreaId, "an application page dropped on a side zone floats (split not allowed)");
}

void close_buttons_and_locks() {
  DockRig rig;
  rig.setRoot(stackOf({1, 2, 3}));
  DockTabStrip* strip = rig.strip(1);
  rig.setRoot(stackOf({1, 2, 3}));
  rig.host->setPanelLocked(3, true);
  rig.settle();
  // Hidden close buttons still occupy their space: the label reserves room either way.
  R1_EXPECT(strip->closeRect(0).w == 16 && strip->closeRect(1).w == 16);
  // The close button of a background tab shows while it is hovered, so one click closes it.
  const dock::Rect close2 = strip->closeRect(1);
  rig.click({close2.x + 8, close2.y + 8});
  R1_EXPECT(!rig.host->layout().isDocked(2), "a hovered tab's close button closes it");
  R1_EXPECT(rig.host->layout().isDocked(1) && rig.host->layout().isDocked(3), "and only that tab");
  // A locked tab has no close button: the same spot only activates it.
  const dock::Rect lockedClose = strip->closeRect(1);
  rig.click({lockedClose.x + 8, lockedClose.y + 8});
  R1_EXPECT(rig.host->layout().isDocked(3) && strip->frontPanel() == 3, "the lock glyph is not a button");  // Tooltips carry the full title.
  rig.move(rig.tabCenter(1));
  R1_EXPECT(strip->tooltipText() == "Panel 1");
  rig.move(rig.tabCenter(3));
  R1_EXPECT(strip->tooltipText() == "Panel 3 (locked)", "locked tabs say so");
  const dock::Rect c1 = strip->closeRect(0);
  rig.move({c1.x + 8, c1.y + 8});
  R1_EXPECT(strip->tooltipText() == "Close Panel 1");
}

void odd_titles_are_drawn_safely() {
  DockRig rig;
  rig.registry.setTitle(1, "");
  rig.registry.setTitle(2, std::string(100000, 'W'));
  rig.registry.setTitle(3, std::string("bad \xFF\xFE utf8 \xC3"));
  rig.registry.setTitle(4, "\xF0\x9F\x98\x80 emoji \xE2\x80\xAE bidi");
  rig.host->refreshPanels();
  rig.setRoot(stackOf({1, 2, 3, 4}));
  R1_EXPECT(rig.strip(1)->tab(1).title.size() <= PanelRegistry::kMaxTitleBytes, "titles are cut at registration");
  R1_EXPECT(rig.strip(1)->tab(0).title.empty());
  // Painting them must not fail: render one frame to a CPU painter.
  r1ui::render::Painter painter;
  painter.begin(900, 600);
  rig.t.ui.paint(painter);
  painter.end();
  R1_EXPECT(painter.stats().rejected == 0 && painter.stats().instances > 0, "everything was drawn without rejected primitives");
}

void the_drag_gap_and_slots() {
  DockRig rig;
  rig.setRoot(dock::Node::split(dock::Axis::Row, {stackOf({1, 2, 3}), stackOf({4})}));
  DockTabStrip* strip = rig.strip(1);
  const double w = strip->naturalTabWidth();
  R1_EXPECT(strip->slotAt(strip->tabRect(0).x + 3) == 0 && strip->slotAt(strip->tabRect(1).x + 3) == 1 && strip->slotAt(1.0e6) == 3, "slot = tab under the centre, clamped to the end");
  strip->setGap(1);
  R1_EXPECT(strip->tabRect(0).x < strip->tabRect(1).x && strip->tabRect(1).x >= strip->tabRect(0).x + strip->tabRect(0).w + strip->naturalTabWidth() - 0.5, "tabs after the gap move over");
  R1_EXPECT(strip->naturalTabWidth() <= w + 0.001, "a gap never makes tabs wider");
  strip->setGap(std::nullopt);
  strip->setLifted(2);
  R1_EXPECT(strip->tabRect(1).w == 0 && strip->tabAt(strip->tabRect(2).x + 3, 10) == std::optional<dock::PanelId>(3), "a lifted tab takes no room");
  strip->setLifted(0);
  R1_EXPECT(strip->tabRect(1).w > 0);
}

}  // namespace

int main() {
  tab_sizes_follow_the_rules();
  application_pages_get_tall_wide_tabs();
  close_buttons_and_locks();
  odd_titles_are_drawn_safely();
  the_drag_gap_and_slots();
  return r1test::finish();
}
