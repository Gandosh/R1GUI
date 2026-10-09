// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the GPU render of the customization gallery page in both themes (written under R1UI_ARTIFACT_DIR
//   for the eye) in six states: normal, edit mode with a hidden entry and a user menu, a palette drag in
//   progress over a menu (insertion indicator and ghost), a toolbar item drag, the free-form panel with a
//   selected button (eight handles) and a marquee, and the command picker dialog. There is no reference
//   for edit mode; the structure is covered by the golden structural tests. Checks that the renders are
//   not empty, differ between themes and states, and that the validation layer (Debug trees) reports
//   nothing while every widget, icon and popup is drawn through the real device.
// Callers: CTest (label gpu, offscreen, no window).
#include "NoDialogs.h"
#include "VisualSupport.h"
#include "r1ui/widgets/customize/CommandPicker.h"
#include "r1ui/widgets/customize/GalleryCustomize.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::core::tree::WidgetId;

enum class Scene { Normal, Edit, PaletteDrag, ToolbarDrag, Panel, Picker };

void press(UiContext& ui, double x, double y) {
  ui.pointerMove(x, y);
  ui.pointerDown(x, y);
}

void moveTo(UiContext& ui, double fx, double fy, double tx, double ty) {
  for (int i = 1; i <= 8; ++i) ui.pointerMove(fx + (tx - fx) * i / 8.0, fy + (ty - fy) * i / 8.0);
}

BuildFn sceneIn(Scene scene) {
  return [scene](UiContext& ui, WidgetId parent) {
    ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
    GalleryCustomizePage& page = createGalleryCustomize(ui, parent);
    ui.frame();
    if (scene == Scene::Normal) return parent;
    page.controller().setEditMode(true);
    ui.frame();
    MenuEditor* menus = page.menuBar().editor();
    menus->setCurrentMenu("menu.edit");
    ui.frame();
    page.model().hideEntry("menu.edit.edit.copy");
    page.model().addUserMenu("My tools");
    ui.frame();
    menus->setCursor("menu.edit.edit.cut");
    ui.frame();
    if (scene == Scene::PaletteDrag) {
      page.palette().select("tool.pen");
      ui.frame();
      const auto row = page.palette().list().rowRect(page.palette().list().selectedIndex());
      const auto target = menus->row(static_cast<size_t>(menus->rowIndex("menu.edit.edit.undo")));
      press(ui, row.x + 80, row.y + row.h / 2);
      moveTo(ui, row.x + 80, row.y + row.h / 2, target.rect.x + 120, target.rect.y + target.rect.h * 0.75);
      ui.frame();
    } else if (scene == Scene::ToolbarDrag) {
      ToolbarEditor* tools = page.mainToolbar().editor();
      const auto first = tools->strip().item(0).rect;
      const auto last = tools->strip().item(tools->strip().itemCount() - 1).rect;
      press(ui, first.x + first.w / 2, first.y + first.h / 2);
      moveTo(ui, first.x + first.w / 2, first.y + first.h / 2, last.x + last.w - 4, last.y + last.h / 2);
      ui.frame();
    } else if (scene == Scene::Panel) {
      FreeFormCanvas& canvas = page.panel().canvas();
      canvas.select("fp.quick.undo");
      const auto b = canvas.button(static_cast<size_t>(canvas.buttonIndex("fp.quick.grid"))).rect;
      press(ui, b.x - 30, b.y + 60);
      moveTo(ui, b.x - 30, b.y + 60, b.x + 40, b.y + 8);
      ui.frame();
    } else if (scene == Scene::Picker) {
      menus->addCommandAtCursor();
      ui.frame();
    }
    return parent;
  };
}

}  // namespace

int main() {
  const VisualPaths paths = r1test::visual::paths();
  const char* names[] = {"normal", "edit", "palette-drag", "toolbar-drag", "panel", "picker"};
  image::Image first[6];
  for (const r1ui::theme::ThemeId theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    for (int s = 0; s < 6; ++s) {
      RenderSpec spec;
      spec.width = 1200;
      spec.height = 1100;
      spec.theme = theme;
      spec.padding = 0;
      spec.background = "panel";
      const image::Image render = renderWidget(sceneIn(static_cast<Scene>(s)), spec, paths);
      const bool dark = theme == r1ui::theme::ThemeId::Dark;
      image::writePng(paths.artifactDir / (std::string("gallery-customize-") + names[s] + (dark ? "-dark" : "-light") + ".png"), render.width, render.height, render.rgba);
      R1_EXPECT(render.width == 1200 && render.height == 1100);
      if (dark) {
        first[s] = render;
      } else {
        R1_EXPECT(render.rgba != first[s].rgba);  // the themes differ
      }
    }
  }
  for (int a = 0; a < 6; ++a) {
    for (int b = a + 1; b < 6; ++b) R1_EXPECT(first[a].rgba != first[b].rgba);  // the states differ
  }
  R1_EXPECT(validationMessageCount() == 0);
  return r1test::finish();
}
