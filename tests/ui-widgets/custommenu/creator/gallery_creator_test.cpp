// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the build and behaviour test of the creator gallery page: it builds under a plain container, holds
//   the real window over a sample command set, the type chooser works, an action can be added and the
//   page's Save to file and Load from file buttons open the in-toolkit file path dialog (which Escape
//   closes without a file); destroying the page leaves nothing behind.
// Callers: CTest (label fast).
#include <filesystem>

#include "TestSupport.h"
#include "r1ui/widgets/actions/ActionList.h"
#include "r1ui/widgets/custommenu/creator/CreateCustomMenuWindow.h"
#include "r1ui/widgets/custommenu/creator/GalleryCreator.h"
#include "r1ui/widgets/custommenu/creator/PiePreviewEditor.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace ev = r1ui::core::events;

template <class T>
T* find(UiContext& ui) {
  T* found = nullptr;
  ui.tree().forEachDescendant(ui.root(), [&](WidgetId id) {
    if (found == nullptr) found = ui.objectAs<T>(id);
  }, true);
  return found;
}

void settle(r1test::TestUi& t) {
  for (int i = 0; i < 4; ++i) {
    t.ui.setTime(t.ui.now() + 40);
    t.ui.tick();
    t.layout();
  }
}

// Escape in a field first drops its selection, then reverts an edit, then closes the dialog (the field
// handles it before the dialog does).
void escapeUntilClosed(r1test::TestUi& t, size_t base) {
  for (int i = 0; i < 3 && t.ui.overlays().stack().size() > base; ++i) {
    t.ui.keyDown(ev::Key::Escape);
    t.ui.keyUp(ev::Key::Escape);
    settle(t);
  }
}

void click(r1test::TestUi& t, WidgetId id) {
  const auto r = t.ui.absRect(id);
  t.ui.pointerMove(r.x + r.w / 2.0, r.y + r.h / 2.0);
  t.ui.pointerDown(r.x + r.w / 2.0, r.y + r.h / 2.0);
  t.ui.pointerUp(r.x + r.w / 2.0, r.y + r.h / 2.0);
  settle(t);
}

}  // namespace

int main() {
  r1test::TestUi t(1200, 900);
  t.ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
  buildGalleryCreator(t.ui, t.ui.root());
  settle(t);
  CreateCustomMenuWindow* window = find<CreateCustomMenuWindow>(t.ui);
  R1_EXPECT(window != nullptr && window->showingChooser());
  WidgetId page;
  t.ui.tree().forEachDescendant(t.ui.root(), [&](WidgetId id) {
    const WidgetObject* object = t.ui.object(id);  // the root has none
    if (!page.valid() && object != nullptr && std::string(object->typeName()) == "GalleryCreator") page = id;
  }, true);
  R1_EXPECT(page.valid());
  if (window == nullptr) return r1test::finish();

  click(t, window->pieCard());
  R1_EXPECT(!window->showingChooser() && window->pieEditor() != nullptr);
  R1_EXPECT(window->actions() != nullptr && window->actions()->view().actions().size() >= 12);
  R1_EXPECT(window->addAction("tool.move") && window->addAction("tool.rotate"));

  // Save to file needs a name; with one it opens the file dialog, and Escape closes it without a file.
  auto* name = t.ui.objectAs<TextInput>(window->nameField());
  R1_EXPECT(name != nullptr && !name->text().empty());  // a suggested name that is not taken
  const size_t base = t.ui.overlays().stack().size();  // a tooltip of the hovered button may be showing
  click(t, window->saveFileButton());
  R1_EXPECT(t.ui.overlays().stack().size() > base);
  escapeUntilClosed(t, base);
  R1_EXPECT(t.ui.overlays().stack().size() <= base);
  click(t, window->loadFileButton());
  R1_EXPECT(t.ui.overlays().stack().size() > base);
  escapeUntilClosed(t, base);
  R1_EXPECT(t.ui.overlays().stack().size() <= base);

  // Create commits into the page's own set and the window offers the chooser again.
  click(t, window->createButton());
  settle(t);
  R1_EXPECT(window->showingChooser());
  R1_EXPECT(t.ui.inputFaults() == 0);

  // The page goes away with its container.
  t.ui.destroy(page);
  t.layout();
  R1_EXPECT(t.ui.overlays().stack().empty() && t.ui.inputFaults() == 0);
  return r1test::finish();
}
