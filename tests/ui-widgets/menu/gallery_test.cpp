// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: a smoke test of the group's gallery entry without a GPU: buildGalleryOverlays() lays out every
//   preview with real sizes, the live triggers open and close their popups (menu, popover, dialog,
//   toast), and destroying the group closes everything it opened.
// Callers: CTest (label fast).
#include "TestSupport.h"
#include "r1ui/widgets/dialog/DialogParts.h"
#include "r1ui/widgets/menu/GalleryOverlays.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;

DialogButton* findButton(r1test::TestUi& t, WidgetId root, const char* label) {
  DialogButton* found = nullptr;
  t.ui.tree().forEachDescendant(root, [&](WidgetId id) {
    DialogButton* b = t.ui.objectAs<DialogButton>(id);
    if (b != nullptr && b->action().label == label) found = b;
  });
  return found;
}

void click(r1test::TestUi& t, WidgetId id) {
  const auto r = t.ui.absRect(id);
  t.ui.pointerMove(r.x + r.w / 2, r.y + r.h / 2);
  t.ui.pointerDown(r.x + r.w / 2, r.y + r.h / 2);
  t.ui.pointerUp(r.x + r.w / 2, r.y + r.h / 2);
}

}  // namespace

int main() {
  r1test::TestUi t(1400, 1800);
  t.ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
  buildGalleryOverlays(t.ui, t.ui.root());
  t.layout();
  R1_EXPECT(t.ui.widgetCount() > 200);
  WidgetId root;
  for (WidgetId c = t.ui.tree().firstChild(t.ui.root()); c.valid(); c = t.ui.tree().nextSibling(c)) {
    if (std::string(t.ui.object(c)->typeName()) == "GalleryOverlays") root = c;
  }
  R1_EXPECT(root.valid());
  R1_EXPECT(t.ui.absRect(root).h > 400);
  // The live triggers open real popups.
  struct Case {
    const char* label;
    bool modal;
  };
  for (const Case c : {Case{"Open context menu", false}, Case{"Open popover", false}, Case{"Open dialog", true}}) {
    DialogButton* b = findButton(t, root, c.label);
    R1_EXPECT(b != nullptr);
    if (b == nullptr) continue;
    click(t, b->id());
    t.layout();
    t.layout();
    R1_EXPECT(t.ui.overlays().count() >= 1);
    R1_EXPECT(t.ui.overlays().anyModal() == c.modal);
    t.ui.keyDown(r1ui::core::events::Key::Escape);
    t.layout();
    R1_EXPECT(t.ui.overlays().count() == 0);
  }
  // Toast triggers show toasts.
  DialogButton* toast = findButton(t, root, "Error toast");
  R1_EXPECT(toast != nullptr);
  if (toast != nullptr) click(t, toast->id());
  t.layout();
  // Destroying the gallery closes everything it opened.
  DialogButton* menu = findButton(t, root, "Open context menu");
  if (menu != nullptr) click(t, menu->id());
  t.layout();
  t.ui.destroy(root);
  t.layout();
  R1_EXPECT(t.ui.overlays().count() == 0);
  return r1test::finish();
}
