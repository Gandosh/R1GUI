// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: renders the field gallery page offscreen in both themes (the page the Phase 4 gallery preview
//   mounts) and leaves the images under the artifact directory for the eye; asserts that the render is
//   not blank (many distinct colours) so a broken page cannot pass silently.
// Callers: CTest (textinput gpu: offscreen Vulkan device, no window).
#include <set>

#include "VisualSupport.h"
#include "r1ui/widgets/image/Png.h"
#include "r1ui/widgets/textinput/GalleryFields.h"

int main() {
  using namespace r1ui::widgets;
  const auto paths = r1test::visual::paths();
  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    const r1test::visual::Build build = [](UiContext& ui, r1ui::core::tree::WidgetId parent) {
      buildGalleryFields(ui, parent);
      return parent;
    };
    testing::RenderSpec spec;
    spec.width = 1000;
    spec.height = 1060;
    spec.theme = theme;
    spec.padding = 0;
    const image::Image img = testing::renderWidget(build, spec, paths);
    const char* name = theme == r1ui::theme::ThemeId::Dark ? "fields-gallery-dark.png" : "fields-gallery-light.png";
    image::writePng(paths.artifactDir / name, img.width, img.height, img.rgba);
    std::set<uint32_t> colours;
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) colours.insert(static_cast<uint32_t>(img.rgba[i]) << 16 | static_cast<uint32_t>(img.rgba[i + 1]) << 8 | img.rgba[i + 2]);
    std::printf("gallery %-5s %zu distinct colours -> %s\n", theme == r1ui::theme::ThemeId::Dark ? "dark" : "light", colours.size(), (paths.artifactDir / name).string().c_str());
    r1test::report(colours.size() > 60, "gallery is not blank", __FILE__, __LINE__);
  }
  return r1test::finish();
}
