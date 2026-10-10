// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the GPU render of the menu creator gallery page in both themes (PNGs under R1UI_ARTIFACT_DIR for
//   the eye) in five states: the type chooser, the pie editor with three actions placed and a slot
//   selected, the pie with an action being dragged over a slot, the panel editor with buttons (columns,
//   size) and the file path dialog opened by Save to file. Checks that the renders are not empty, differ
//   between themes and states, that the drag and the dialog really exist in their states, and that the
//   validation layer (Debug trees) reports nothing while every widget, icon and overlay is drawn through
//   the real device.
// Callers: CTest (label gpu, offscreen, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/actions/ActionList.h"
#include "r1ui/widgets/custommenu/creator/CreateCustomMenuWindow.h"
#include "r1ui/widgets/custommenu/creator/GalleryCreator.h"
#include "r1ui/widgets/custommenu/creator/PanelPreviewEditor.h"
#include "r1ui/widgets/custommenu/creator/PiePreviewEditor.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::core::tree::WidgetId;
namespace cm = r1ui::commands::custommenu;

enum class Scene { Chooser, Pie, PieDrag, Panel, FileDialog };

template <class T>
T* firstOf(UiContext& ui, WidgetId root) {
  T* found = nullptr;
  ui.tree().forEachDescendant(root, [&](WidgetId id) {
    if (found == nullptr) found = ui.objectAs<T>(id);
  });
  return found;
}

void settle(UiContext& ui) {
  for (int i = 0; i < 4; ++i) {
    ui.setTime(ui.now() + 50);
    ui.tick();
    ui.frame();
  }
}

void click(UiContext& ui, WidgetId id) {
  const auto r = ui.absRect(id);
  ui.pointerMove(r.x + r.w / 2.0, r.y + r.h / 2.0);
  ui.pointerDown(r.x + r.w / 2.0, r.y + r.h / 2.0);
  ui.pointerUp(r.x + r.w / 2.0, r.y + r.h / 2.0);
  settle(ui);
}

BuildFn sceneIn(Scene scene, bool* ok) {
  return [scene, ok](UiContext& ui, WidgetId parent) {
    ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
    buildGalleryCreator(ui, parent);
    settle(ui);
    CreateCustomMenuWindow* w = firstOf<CreateCustomMenuWindow>(ui, parent);
    if (w == nullptr) {
      *ok = false;
      return parent;
    }
    if (scene == Scene::Chooser) return parent;
    if (scene == Scene::Panel) {
      w->chooseType(cm::MenuKind::Panel);
      settle(ui);
      for (const char* c : {"tool.select", "tool.move", "tool.rotate", "tool.scale", "edit.undo", "view.frame"}) w->addAction(c);
      w->session().draft()->setPanelColumns(2);
      w->panelEditor()->refresh();
      w->panelEditor()->select(1);
      settle(ui);
      *ok = *ok && w->panelEditor() != nullptr && w->session().draft()->entryCount() == 6;
      return parent;
    }
    w->chooseType(cm::MenuKind::Pie);
    settle(ui);
    w->addAction("tool.move");
    w->addAction("tool.rotate");
    w->addAction("tool.scale");
    w->pieEditor()->select(2);
    w->session().draft()->setName("My tools");
    settle(ui);
    *ok = *ok && w->pieEditor() != nullptr && w->session().draft()->filledCount() == 3;
    if (scene == Scene::PieDrag) {
      w->actions()->view().selectAction("edit.undo");
      settle(ui);
      const auto row = w->actions()->view().rowRect(w->actions()->view().selectedRow());
      double x = 0.0, y = 0.0;
      w->pieEditor()->slotCenter(4, x, y);
      ui.pointerMove(row.x + 40.0, row.y + row.h / 2.0);
      ui.pointerDown(row.x + 40.0, row.y + row.h / 2.0);
      ui.pointerMove(row.x + 20.0, row.y + row.h / 2.0 + 14.0);
      ui.pointerMove(x, y);
      ui.frame();
      *ok = *ok && w->dragHub()->active() && w->dragHub()->accepting() && w->pieEditor()->dropHover() == 4;
    } else if (scene == Scene::FileDialog) {
      click(ui, w->saveFileButton());
      *ok = *ok && ui.overlays().any();
    }
    return parent;
  };
}

}  // namespace

int main() {
  const VisualPaths paths = r1test::visual::paths();
  const char* names[] = {"chooser", "pie", "pie-drag", "panel", "file-dialog"};
  image::Image first[5];
  bool sceneOk = true;
  for (const r1ui::theme::ThemeId theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    for (int s = 0; s < 5; ++s) {
      RenderSpec spec;
      spec.width = 1100;
      spec.height = 940;
      spec.theme = theme;
      spec.padding = 0;
      spec.background = "panel";
      const image::Image render = renderWidget(sceneIn(static_cast<Scene>(s), &sceneOk), spec, paths);
      const bool dark = theme == r1ui::theme::ThemeId::Dark;
      image::writePng(paths.artifactDir / (std::string("creator-") + names[s] + (dark ? "-dark" : "-light") + ".png"), render.width, render.height, render.rgba);
      R1_EXPECT(render.width == 1100 && render.height == 940);
      if (dark) first[s] = render;
      else R1_EXPECT(render.rgba != first[s].rgba);
    }
  }
  R1_EXPECT(sceneOk);
  for (int a = 0; a < 5; ++a) {
    for (int b = a + 1; b < 5; ++b) R1_EXPECT(first[a].rgba != first[b].rgba);
  }
  R1_EXPECT(validationMessageCount() == 0);
  return r1test::finish();
}
