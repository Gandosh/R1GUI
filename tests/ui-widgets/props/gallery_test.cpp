// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: smoke oracle for the property panel gallery page: it builds in both themes at several display
//   scales, lays out and paints (every icon and style row it uses must resolve), switches between the
//   sample types and the one-or-two-object selection, shows mixed values for two objects, supports an
//   edit followed by undo and redo through its buttons, refreshes after a change "from outside", reacts to
//   a theme switch and a resize, and is destroyed cleanly with the page.
// Callers: CTest (props fast, no GPU; the painter records into a CPU list).
#include <cmath>
#include <string>

#include "../../ui-props/support/Samples.h"
#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/iconbutton/IconButton.h"
#include "r1ui/widgets/numberfield/NumberField.h"
#include "r1ui/widgets/props/GalleryProps.h"
#include "r1ui/widgets/props/PropertyPanel.h"
#include "r1ui/widgets/segmented/Segmented.h"

namespace {

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;
using r1ui::core::tree::WidgetId;
using r1ui::theme::ThemeId;

template <class T>
T* first(UiContext& ui) {
  T* found = nullptr;
  ui.tree().forEachDescendant(ui.root(), [&](WidgetId id) {
    if (found == nullptr) found = dynamic_cast<T*>(ui.object(id));
  });
  return found;
}

template <class T>
size_t count(UiContext& ui) {
  size_t n = 0;
  ui.tree().forEachDescendant(ui.root(), [&](WidgetId id) { n += dynamic_cast<T*>(ui.object(id)) != nullptr ? 1 : 0; });
  return n;
}

IconButton* button(UiContext& ui, const char* icon) {
  IconButton* found = nullptr;
  ui.tree().forEachDescendant(ui.root(), [&](WidgetId id) {
    auto* b = dynamic_cast<IconButton*>(ui.object(id));
    if (found == nullptr && b != nullptr && b->icon() == icon) found = b;
  });
  return found;
}

void paint(r1test::TestUi& t, int w, int h) {
  r1ui::render::Painter painter;
  painter.begin(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
  t.ui.paint(painter);
  t.ui.finishPaint();
  painter.end();
}

void clickCenter(r1test::TestUi& t, WidgetId id) {
  t.ui.setTime(t.ui.now() + 1000);
  const layout::Rect r = t.ui.absRect(id);
  t.ui.pointerMove(r.x + r.w * 0.5, r.y + r.h * 0.5);
  t.ui.pointerDown(r.x + r.w * 0.5, r.y + r.h * 0.5);
  t.ui.pointerUp(r.x + r.w * 0.5, r.y + r.h * 0.5);
}

void tick(r1test::TestUi& t) {
  t.ui.setTime(t.ui.now() + 20);
  t.ui.tick();
  t.layout();
}

}  // namespace

int main() try {
  for (const float scale : {1.0f, 1.5f, 2.0f}) {
    for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
      r1test::TestUi t(700, 900, scale);
      t.services.theme().set(theme);
      t.ui.rootStyle().direction = layout::FlexDirection::Column;
      buildGalleryProps(t.ui, t.ui.root());
      t.layout();
      paint(t, static_cast<int>(700 * scale), static_cast<int>(900 * scale));

      // The default page: two transforms. The two names and the Y positions differ, so mixed values show.
      PropertyPanel* panel = first<PropertyPanel>(t.ui);
      R1_EXPECT(panel != nullptr && panel->sectionCount() == 2);
      R1_EXPECT(panel->describe().find("Name\" string text=\"\" (mixed)") != std::string::npos);
      R1_EXPECT(count<Segmented>(t.ui) == 2 && count<IconButton>(t.ui) >= 4);

      // Switch the sample type to Material through the first Segmented: the panel regenerates.
      Segmented* type = first<Segmented>(t.ui);
      const layout::Rect tr = t.ui.absRect(type->id());
      t.ui.setTime(t.ui.now() + 1000);
      t.ui.pointerMove(tr.x + tr.w * 0.5, tr.y + tr.h * 0.5);
      t.ui.pointerDown(tr.x + tr.w * 0.5, tr.y + tr.h * 0.5);
      t.ui.pointerUp(tr.x + tr.w * 0.5, tr.y + tr.h * 0.5);
      tick(t);
      R1_EXPECT(type->selectedIndex() == 1 && panel->sectionCount() == 3 && panel->section(0)->title() == "Appearance");
      paint(t, static_cast<int>(700 * scale), static_cast<int>(900 * scale));

      // A theme switch and a smaller window repaint without trouble.
      t.services.theme().toggle();
      t.ui.setViewport(static_cast<int>(420 * scale), static_cast<int>(700 * scale), scale);
      t.layout();
      paint(t, static_cast<int>(420 * scale), static_cast<int>(700 * scale));
    }
  }

  // Edit by typing, then undo and redo through the buttons; a change from outside refreshes the panel.
  r1test::TestUi t(700, 900);
  t.ui.rootStyle().direction = layout::FlexDirection::Column;
  buildGalleryProps(t.ui, t.ui.root());
  t.layout();
  PropertyPanel* panel = first<PropertyPanel>(t.ui);
  IconButton* undo = button(t.ui, "undo2");
  IconButton* redo = button(t.ui, "redo2");
  IconButton* script = button(t.ui, "rotate-cw");
  R1_EXPECT(undo != nullptr && redo != nullptr && script != nullptr && !undo->enabled() && !redo->enabled());

  NumberField* x = panel->rowView(0)->number(0);  // Position X: both transforms have 141
  R1_EXPECT(x != nullptr && x->value() == 141.0 && !x->hasState(StateFlag::kMixed));
  clickCenter(t, x->id());
  R1_EXPECT(x->editing());
  t.ui.keyDown(r1ui::core::events::Key::A, r1ui::core::events::Mod::kCtrl);
  for (const char c : std::string("200")) t.ui.textInput(static_cast<char32_t>(c));
  t.ui.keyDown(r1ui::core::events::Key::Enter);
  tick(t);
  R1_EXPECT(x->value() == 200.0 && undo->enabled() && !redo->enabled());
  R1_EXPECT(undo->tooltipText() == "Undo: Set Position");
  clickCenter(t, undo->id());
  tick(t);
  R1_EXPECT(x->value() == 141.0 && !undo->enabled() && redo->enabled());
  clickCenter(t, redo->id());
  tick(t);
  R1_EXPECT(x->value() == 200.0);

  clickCenter(t, script->id());
  tick(t);
  R1_EXPECT(x->value() == 210.0);  // the "script" moved the first transform by 10; the other still has 200: now mixed
  R1_EXPECT(x->hasState(StateFlag::kMixed));
  paint(t, 700, 900);

  // Destroying the page with timers pending and listeners registered is clean.
  script = button(t.ui, "rotate-cw");
  clickCenter(t, script->id());
  WidgetId page;
  t.ui.tree().forEachDescendant(t.ui.root(), [&](WidgetId id) {
    if (std::string_view(t.ui.object(id)->typeName()) == "GalleryPropsPage") page = id;
  });
  R1_EXPECT(page.valid());
  t.ui.destroy(page);
  tick(t);
  R1_EXPECT(count<PropertyPanel>(t.ui) == 0);
  return r1test::finish();
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}
