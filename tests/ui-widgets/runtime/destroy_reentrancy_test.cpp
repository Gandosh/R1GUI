// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression tests for UiContext::destroy when an onDetached hook destroys other widgets of
//   the same subtree (phase 4 review H1) and for the per-widget bookkeeping that must go with a
//   destroyed widget (layout-callback registrations, review M3; animation tweens).
// Why: a duplicated free-slot let two later create() calls share one object slot, which freed a live
//   widget (use after free). The tests create widgets after the re-entrant destroy and check that
//   every live widget is still its own object.
// Callers: CTest (fast tier). Calls: UiContext, ThumbnailGrid, Popover.
#include <set>

#include "TestSupport.h"
#include "r1ui/widgets/popover/Popover.h"
#include "r1ui/widgets/thumbnailgrid/ThumbnailGrid.h"

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;
namespace events = r1ui::core::events;
using r1ui::core::tree::WidgetId;

namespace {

int g_destroyed = 0;
int g_childDetached = 0;

class Leaf : public WidgetObject {
 public:
  ~Leaf() override { ++g_destroyed; }
  const char* typeName() const override { return "Leaf"; }
  void onDetached() override { ++g_childDetached; }
  void onAttached() override {
    style().width = layout::Length::px(10);
    style().height = layout::Length::px(10);
  }
};

// A parent that destroys its own child from onDetached (a grid dropping its rename box).
class TearsDownChild : public WidgetObject {
 public:
  const char* typeName() const override { return "TearsDownChild"; }
  void onAttached() override { child = ui().create<Leaf>(id()).id(); }
  void onDetached() override {
    if (ui().alive(child)) ui().destroy(child);
  }
  WidgetId child;
};

// A leaf that destroys its parent when it is detached: the outer destroy() is re-entered from the top.
class KillsParent : public WidgetObject {
 public:
  const char* typeName() const override { return "KillsParent"; }
  void onDetached() override {
    if (ui().alive(victim)) ui().destroy(victim);
  }
  WidgetId victim;
};

class Model final : public AssetModel {
 public:
  size_t count() const override { return 40; }
  void item(size_t i, GridItem& out) const override {
    out = GridItem{};
    out.key = 1000 + i;
    out.name = "Asset " + std::to_string(i);
  }
};

// Creates `n` leaves and checks that each one is its own live object.
void expectDistinctLiveObjects(UiContext& ui, int n) {
  std::set<const WidgetObject*> seen;
  std::vector<WidgetId> ids;
  const int destroyedBefore = g_destroyed;
  for (int i = 0; i < n; ++i) ids.push_back(ui.create<Leaf>(ui.root()).id());
  for (const WidgetId id : ids) {
    R1_EXPECT(ui.alive(id));
    const WidgetObject* o = ui.object(id);
    R1_EXPECT(o != nullptr && o->id() == id);
    R1_EXPECT(seen.insert(o).second);
  }
  R1_EXPECT(g_destroyed == destroyedBefore);  // no live widget was freed by a later create()
}

}  // namespace

int main() {
  {  // a parent destroys its child from onDetached; the object slots must stay unique
    r1test::TestUi t;
    TearsDownChild& parent = t.ui.create<TearsDownChild>(t.ui.root());
    t.ui.frame();
    g_destroyed = 0;
    g_childDetached = 0;
    R1_EXPECT(t.ui.destroy(parent.id()));
    R1_EXPECT(g_destroyed == 1);
    R1_EXPECT(g_childDetached == 1);  // the hook ran once even though destroy() was re-entered
    expectDistinctLiveObjects(t.ui, 6);
  }
  {  // a leaf destroys the parent that is already being destroyed
    r1test::TestUi t;
    TearsDownChild& host = t.ui.create<TearsDownChild>(t.ui.root());
    KillsParent& killer = t.ui.create<KillsParent>(host.id());
    killer.victim = host.id();
    t.ui.frame();
    g_destroyed = 0;
    R1_EXPECT(t.ui.destroy(host.id()));
    R1_EXPECT(!t.ui.alive(host.id()));
    R1_EXPECT(g_destroyed == 1);
    expectDistinctLiveObjects(t.ui, 6);
  }
  {  // a destroyed grid with an open rename box (the real trigger of the double free)
    r1test::TestUi t(600, 500);
    t.ui.rootStyle().alignItems = layout::Align::Stretch;
    ThumbnailGrid& grid = t.ui.create<ThumbnailGrid>(t.ui.root());
    Model model;
    grid.setModel(&model);
    t.ui.frame();
    grid.beginRename(1001);
    R1_EXPECT(grid.renaming());
    const size_t widgets = t.ui.widgetCount();
    R1_EXPECT(t.ui.destroy(grid.id()));
    R1_EXPECT(t.ui.widgetCount() < widgets);
    g_destroyed = 0;
    expectDistinctLiveObjects(t.ui, 8);
    t.ui.keyDown(events::Key::A);
    t.ui.pointerMove(5, 5);
    R1_EXPECT(t.ui.router().focused() == WidgetId{} || t.ui.alive(t.ui.router().focused()));
  }
  {  // reparenting a focused widget into a hidden parent releases its focus
    r1test::TestUi t;
    Leaf& focusable = t.ui.create<Leaf>(t.ui.root());
    focusable.node().flags.focusable = true;
    Leaf& hidden = t.ui.create<Leaf>(t.ui.root());
    t.ui.frame();
    t.ui.focusWidget(focusable.id());
    R1_EXPECT(t.ui.router().focused() == focusable.id());
    t.ui.invalidator().setVisible(hidden.id(), false);
    R1_EXPECT(t.ui.reparent(focusable.id(), hidden.id()));
    R1_EXPECT(t.ui.router().focused() != focusable.id());
  }
  {  // popovers register a layout callback; closing them must remove it
    r1test::TestUi t;
    Leaf& anchor = t.ui.create<Leaf>(t.ui.root());
    t.ui.frame();
    const size_t before = t.ui.layoutCallbackCount();
    for (int i = 0; i < 200; ++i) {
      PopoverOptions options;
      options.anchorWidget = anchor.id();
      const auto handle = openPopover(t.ui, options);
      t.ui.create<Leaf>(handle.host);
      t.ui.frame();
      closePopover(t.ui, handle);
      t.ui.frame();
    }
    R1_EXPECT(t.ui.layoutCallbackCount() == before);
    R1_EXPECT(t.ui.widgetCount() < 20);
  }
  return r1test::finish();
}
