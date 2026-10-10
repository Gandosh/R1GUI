// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the GPU render of the custom menu gallery page in both themes (PNGs under R1UI_ARTIFACT_DIR for the
//   eye), in three states: the page as built, the page with the live pie drawn by a real right-button
//   gesture, and the page with the "Custom Menus" main menu open. Checks that the renders are not empty,
//   differ between themes and states, that the live pie and the open menu really exist in their states, and
//   that the validation layer (Debug trees) reports nothing while every widget, icon and popup is drawn
//   through the real device.
// Callers: CTest (label gpu, offscreen, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/custommenu/CustomMenuPanel.h"
#include "r1ui/widgets/custommenu/GalleryCustomMenus.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/pie/PieTrigger.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::core::tree::WidgetId;

enum class Scene { Normal, LivePie, MainMenu };

template <class T>
T* firstOf(UiContext& ui, WidgetId root) {
  T* found = nullptr;
  ui.tree().forEachDescendant(root, [&](WidgetId id) {
    if (found == nullptr) found = ui.objectAs<T>(id);
  });
  return found;
}

BuildFn sceneIn(Scene scene, bool* ok) {
  return [scene, ok](UiContext& ui, WidgetId parent) {
    ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
    buildGalleryCustomMenus(ui, parent);
    ui.frame();
    if (scene == Scene::LivePie) {
      PieTrigger* trigger = firstOf<PieTrigger>(ui, parent);
      const auto area = ui.absRect(trigger->id());
      const double cx = area.x + area.w / 2.0;
      const double cy = area.y + area.h / 2.0;
      ui.setTime(1000);
      ui.pointerMove(cx, cy);
      ui.pointerDown(cx, cy, r1ui::core::events::Button::Right);
      ui.setTime(1200);
      ui.tick();
      ui.frame();
      const PiePoint toward = pieSlotOffset(8, 2, 80.0);
      ui.pointerMove(cx + toward.x, cy + toward.y);
      ui.frame();
      *ok = *ok && trigger->pieDrawn() && trigger->highlighted() == 2;
    } else if (scene == Scene::MainMenu) {
      MenuBar* bar = firstOf<MenuBar>(ui, parent);
      *ok = *ok && bar != nullptr && bar->openMenu(0);
      ui.frame();
      ui.frame();
      *ok = *ok && ui.overlays().any();
    }
    return parent;
  };
}

}  // namespace

int main() {
  const VisualPaths paths = r1test::visual::paths();
  const char* names[] = {"normal", "live-pie", "main-menu"};
  image::Image first[3];
  bool sceneOk = true;
  for (const r1ui::theme::ThemeId theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    for (int s = 0; s < 3; ++s) {
      RenderSpec spec;
      spec.width = 1200;
      spec.height = 1180;
      spec.theme = theme;
      spec.padding = 0;
      spec.background = "panel";
      const image::Image render = renderWidget(sceneIn(static_cast<Scene>(s), &sceneOk), spec, paths);
      const bool dark = theme == r1ui::theme::ThemeId::Dark;
      image::writePng(paths.artifactDir / (std::string("gallery-custommenus-") + names[s] + (dark ? "-dark" : "-light") + ".png"), render.width, render.height, render.rgba);
      R1_EXPECT(render.width == 1200 && render.height == 1180);
      if (dark) {
        first[s] = render;
      } else {
        R1_EXPECT(render.rgba != first[s].rgba);
      }
    }
  }
  R1_EXPECT(sceneOk);
  for (int a = 0; a < 3; ++a) {
    for (int b = a + 1; b < 3; ++b) R1_EXPECT(first[a].rgba != first[b].rgba);
  }
  R1_EXPECT(validationMessageCount() == 0);
  return r1test::finish();
}
