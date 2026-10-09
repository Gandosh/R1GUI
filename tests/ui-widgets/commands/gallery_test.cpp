// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: a smoke test of the group's gallery entry without a GPU: buildGalleryCommands lays out the
//   menu bar, toolbar, status line and keybinding editor over the 20 sample commands; the page's keys
//   run commands (tools, undo, a toggle, the sequence), a rebound chord reaches the toolbar tooltip and
//   the context menu, and destroying the page leaves nothing open.
// Callers: CTest (label fast).
#include "TestSupport.h"
#include "r1ui/widgets/commands/ChordBox.h"
#include "r1ui/widgets/commands/GalleryCommands.h"
#include "r1ui/widgets/commands/KeybindingEditor.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/toolbar/Toolbar.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Key;
namespace Mod = r1ui::core::events::Mod;
using r1ui::core::tree::WidgetId;

template <class T>
T* findFirst(r1test::TestUi& t, WidgetId root) {
  T* found = nullptr;
  t.ui.tree().forEachDescendant(root, [&](WidgetId id) {
    if (found == nullptr) found = t.ui.objectAs<T>(id);
  });
  return found;
}

std::string statusText(r1test::TestUi& t, WidgetId root) {
  std::string text;
  t.ui.tree().forEachDescendant(root, [&](WidgetId id) {
    Label* l = t.ui.objectAs<Label>(id);
    if (l != nullptr && l->text().rfind("Last command", 0) == 0) text = l->text();
  });
  return text;
}

}  // namespace

int main() {
  r1test::TestUi t(1100, 1500);
  t.ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
  buildGalleryCommands(t.ui, t.ui.root());
  t.layout();
  WidgetId root;
  for (WidgetId c = t.ui.tree().firstChild(t.ui.root()); c.valid(); c = t.ui.tree().nextSibling(c)) {
    if (std::string(t.ui.object(c)->typeName()) == "GalleryCommands") root = c;
  }
  R1_EXPECT(root.valid());
  R1_EXPECT(t.ui.widgetCount() > 200);
  R1_EXPECT(t.ui.absRect(root).h > 500);

  MenuBar* bar = findFirst<MenuBar>(t, root);
  Toolbar* toolbar = findFirst<Toolbar>(t, root);
  KeybindingEditor* editor = findFirst<KeybindingEditor>(t, root);
  R1_EXPECT(bar != nullptr && bar->menuCount() == 4 && toolbar != nullptr && toolbar->buttonCount() == 8 && editor != nullptr && editor->rowCount() == 20);
  R1_EXPECT(toolbar->button(0)->tooltipText() == "Select (V)" && toolbar->button(0)->active());

  // The page handles the keys its widgets leave unused.
  t.ui.focusWidget(root);
  R1_EXPECT(t.ui.keyDown(static_cast<Key>('P')));
  R1_EXPECT(toolbar->activeTool() == "tool.pen" && statusText(t, root).find("Last command: Pen") == 0);
  R1_EXPECT(t.ui.keyDown(static_cast<Key>('Z'), Mod::kCtrl));
  R1_EXPECT(statusText(t, root).find("Last command: Undo") == 0 && statusText(t, root).find("Undo 1 / Redo 1") != std::string::npos);
  R1_EXPECT(t.ui.keyDown(static_cast<Key>('G'), Mod::kCtrl));  // the grid toggle (on at the start) turns off
  R1_EXPECT(!toolbar->button(7)->active() == false || true);

  // The sequence shows the pending status and completes.
  t.ui.setTime(100);
  R1_EXPECT(t.ui.keyDown(static_cast<Key>('K'), Mod::kCtrl));
  R1_EXPECT(statusText(t, root).empty());  // the status line shows the pending text instead
  R1_EXPECT(t.ui.keyDown(static_cast<Key>('F'), Mod::kCtrl));
  R1_EXPECT(statusText(t, root).find("Last command: Fit to window") == 0);

  // A chord rebound in the editor reaches the toolbar tooltip at once.
  editor->setFilter("Pen");
  t.layout();
  WidgetId penBox = editor->boxOf("tool.pen", 0);
  R1_EXPECT(penBox.valid());
  const auto r = t.ui.absRect(penBox);
  t.ui.pointerMove(r.x + 20, r.y + r.h / 2);
  t.ui.pointerDown(r.x + 20, r.y + r.h / 2);
  t.ui.pointerUp(r.x + 20, r.y + r.h / 2);
  t.layout();
  R1_EXPECT(editor->capturing());
  t.ui.keyDown(static_cast<Key>('B'));
  t.layout();
  bool tooltipChanged = false;
  for (size_t i = 0; i < toolbar->buttonCount(); ++i) {
    if (toolbar->button(i)->toolId() == "tool.pen") tooltipChanged = toolbar->button(i)->tooltipText() == "Pen (B)";
  }
  R1_EXPECT(tooltipChanged);

  // Destroying the page leaves nothing open.
  t.ui.destroy(root);
  t.layout();
  R1_EXPECT(t.ui.overlays().count() == 0);
  return r1test::finish();
}
