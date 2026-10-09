// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GPU smoke render of the containers gallery page in both themes: it renders offscreen through
//   the real renderer (no reference image exists for a whole gallery), checks that the page is not blank
//   (many distinct colours, ink in every quadrant) and writes gallery-containers-<theme>.png to the
//   artifact folder for the eye.
// Callers: CTest (section gpu: renders offscreen on a Vulkan device, no window).
#include <set>

#include "VisualSupport.h"
#include "r1ui/widgets/section/GalleryContainers.h"

int main() try {
  using r1ui::theme::ThemeId;
  const auto paths = r1test::visual::paths();
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    r1ui::widgets::testing::RenderSpec spec;
    spec.width = 1300;
    spec.height = 1500;
    spec.theme = theme;
    spec.padding = 0;
    const auto img = r1ui::widgets::testing::renderWidget(
        [](r1ui::widgets::UiContext& ui, r1ui::core::tree::WidgetId parent) {
          ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
          r1ui::widgets::buildGalleryContainers(ui, parent);
          return ui.tree().lastChild(parent);
        },
        spec, paths);
    const char* name = theme == ThemeId::Dark ? "dark" : "light";
    r1ui::widgets::image::writePng(paths.artifactDir / (std::string("gallery-containers-") + name + ".png"), img.width, img.height, img.rgba);
    std::set<uint32_t> colours;
    int inkInQuadrant[4] = {0, 0, 0, 0};
    const uint32_t bg = *reinterpret_cast<const uint32_t*>(img.rgba.data());
    for (uint32_t y = 0; y < img.height; y += 2) {
      for (uint32_t x = 0; x < img.width; x += 2) {
        const uint32_t px = *reinterpret_cast<const uint32_t*>(img.rgba.data() + (static_cast<size_t>(y) * img.width + x) * 4);
        colours.insert(px);
        if (px != bg) ++inkInQuadrant[(y >= img.height / 2 ? 2 : 0) + (x >= img.width / 2 ? 1 : 0)];
      }
    }
    std::printf("visual gallery-containers %s: %zu distinct colours, ink per quadrant %d %d %d %d\n", name, colours.size(), inkInQuadrant[0], inkInQuadrant[1], inkInQuadrant[2], inkInQuadrant[3]);
    R1_EXPECT(colours.size() > 200);
    for (const int ink : inkInQuadrant) R1_EXPECT(ink > 500);
  }
  R1_EXPECT(r1ui::widgets::testing::validationMessageCount() == 0);  // the Debug tree runs the validation layers
  return r1test::finish();
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}
