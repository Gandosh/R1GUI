// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GPU render of the buttons gallery page in both themes (900 x 560 logical px, offscreen) to
//   <R1UI_ARTIFACT_DIR>/gallery-buttons-<theme>.png, checking that the page is drawn (the image has
//   many pixels that differ from the panel background) and that no Vulkan validation message was
//   produced (Debug trees). The PNGs are for the eye.
// Callers: CTest (button gpu: renders offscreen on a Vulkan device, no window).
#include <cstdio>

#include "VisualSupport.h"
#include "r1ui/widgets/button/GalleryButtons.h"

int main() {
  using namespace r1ui::widgets;
  using namespace r1ui::widgets::testing;
  const VisualPaths paths = r1test::visual::paths();
  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    RenderSpec spec;
    spec.width = 900;
    spec.height = 560;
    spec.theme = theme;
    spec.padding = 0;
    const BuildFn build = [](UiContext& ui, r1ui::core::tree::WidgetId parent) {
      ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
      buildGalleryButtons(ui, parent);
      return parent;
    };
    const image::Image img = renderWidget(build, spec, paths);
    const bool dark = theme == r1ui::theme::ThemeId::Dark;
    const auto bg = [&](size_t i) { return img.rgba[i]; };
    size_t differing = 0;
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
      if (bg(i) != bg(0) || bg(i + 1) != bg(1) || bg(i + 2) != bg(2)) ++differing;
    }
    std::printf("gallery buttons %s: %zu non-background pixels\n", dark ? "dark" : "light", differing);
    R1_EXPECT(differing > 5000);
    std::filesystem::create_directories(paths.artifactDir);
    image::writePng(paths.artifactDir / (std::string("gallery-buttons-") + (dark ? "dark" : "light") + ".png"), img.width, img.height, img.rgba);
  }
  R1_EXPECT(validationMessageCount() == 0);
  return r1test::finish();
}
