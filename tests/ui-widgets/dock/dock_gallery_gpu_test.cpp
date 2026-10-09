// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GPU smoke render of the dock gallery page in both themes: it renders offscreen through the
//   real renderer, checks that the page is not blank (many distinct colours, the six panel colours
//   and the floating window's frame are present) and writes gallery-dock-<theme>.png to the artifact
//   folder for the eye.
// Callers: CTest (dock gpu: renders offscreen on a Vulkan device, no window).
#include <set>

#include "ExpectWithMessage.h"
#include "VisualSupport.h"
#include "r1ui/widgets/dock/GalleryDock.h"
#include "r1ui/widgets/image/Png.h"

int main() try {
  using r1ui::theme::ThemeId;
  const auto paths = r1test::visual::paths();
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    r1ui::widgets::testing::RenderSpec spec;
    spec.width = 1000;
    spec.height = 560;
    spec.theme = theme;
    spec.padding = 8;
    const auto img = r1ui::widgets::testing::renderWidget(
        [](r1ui::widgets::UiContext& ui, r1ui::core::tree::WidgetId parent) {
          ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
          r1ui::widgets::buildGalleryDock(ui, parent);
          return ui.tree().lastChild(parent);
        },
        spec, paths);
    const char* name = theme == ThemeId::Dark ? "dark" : "light";
    r1ui::widgets::image::writePng(paths.artifactDir / (std::string("gallery-dock-") + name + ".png"), img.width, img.height, img.rgba);
    std::set<uint32_t> colours;
    for (uint32_t y = 0; y < img.height; y += 2) {
      for (uint32_t x = 0; x < img.width; x += 2) {
        colours.insert(*reinterpret_cast<const uint32_t*>(img.rgba.data() + (static_cast<size_t>(y) * img.width + x) * 4));
      }
    }
    std::printf("visual gallery-dock %s: %zu distinct colours\n", name, colours.size());
    R1_EXPECT(colours.size() > 150, "a page with six coloured panels, tabs, text, a window with a shadow");
  }
  R1_EXPECT(r1ui::widgets::testing::validationMessageCount() == 0);  // the Debug tree runs the validation layers
  return r1test::finish();
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}
