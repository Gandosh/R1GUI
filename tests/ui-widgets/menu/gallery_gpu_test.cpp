// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the GPU render of the group's gallery in both themes (written under R1UI_ARTIFACT_DIR for the
//   eye), a check that the render is not empty and differs between themes, and that the validation
//   layer (Debug trees) reports nothing while every popup surface, shadow, icon and text style is
//   drawn through the real device.
// Callers: CTest (label gpu, offscreen, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/menu/GalleryOverlays.h"

int main() {
  using namespace r1ui::widgets;
  using namespace r1ui::widgets::testing;
  const VisualPaths paths = r1test::visual::paths();
  const BuildFn build = [](UiContext& ui, r1ui::core::tree::WidgetId parent) {
    buildGalleryOverlays(ui, parent);
    return parent;
  };
  image::Image renders[2];
  int index = 0;
  for (const r1ui::theme::ThemeId theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    RenderSpec spec;
    spec.width = 1400;
    spec.height = 1500;
    spec.theme = theme;
    spec.padding = 0;
    spec.background = "panel";
    renders[index] = renderWidget(build, spec, paths);
    image::writePng(paths.artifactDir / (std::string("gallery-overlays-") + (index == 0 ? "dark" : "light") + ".png"), renders[index].width, renders[index].height, renders[index].rgba);
    ++index;
  }
  R1_EXPECT(renders[0].rgba != renders[1].rgba);
  R1_EXPECT(validationMessageCount() == 0);
  return r1test::finish();
}
