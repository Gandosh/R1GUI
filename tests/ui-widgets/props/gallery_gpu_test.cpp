// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: renders the property panel gallery page offscreen in both themes (the page the preview mounts)
//   for the sample types the page offers (transform, material, light, with one and two objects selected)
//   and leaves the images under the artifact directory for the eye; asserts that each render is not blank
//   (many distinct colours) so a broken page cannot pass silently, and that the Vulkan validation layers
//   reported nothing.
// Callers: CTest (props gpu: offscreen Vulkan device, no window).
#include <set>
#include <string>

#include "VisualSupport.h"
#include "r1ui/widgets/image/Png.h"
#include "r1ui/widgets/numberfield/NumberField.h"
#include "r1ui/widgets/props/GalleryProps.h"
#include "r1ui/widgets/props/PropertyPanel.h"
#include "r1ui/widgets/segmented/Segmented.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;

template <class T>
T* find(UiContext& ui, size_t nth = 0) {
  T* found = nullptr;
  size_t seen = 0;
  ui.tree().forEachDescendant(ui.root(), [&](WidgetId id) {
    if (T* o = dynamic_cast<T*>(ui.object(id)); o != nullptr && found == nullptr && seen++ == nth) found = o;
  });
  return found;
}

}  // namespace

int main() {
  const auto paths = r1test::visual::paths();
  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    for (const int type : {0, 1, 2}) {
      for (const int objects : {1, 2}) {
        const r1test::visual::Build build = [type, objects](UiContext& ui, WidgetId parent) {
          buildGalleryProps(ui, parent);
          ui.frame();
          // Drive the page's own controls: the same events a click on the segmented controls produces.
          const auto click = [&](Segmented& s, int index) {
            const r1ui::core::layout::Rect r = ui.absRect(s.id());
            const double w = r.w / static_cast<double>(s.items().size());
            ui.setTime(ui.now() + 1000);
            ui.pointerMove(r.x + w * (index + 0.5), r.y + r.h * 0.5);
            ui.pointerDown(r.x + w * (index + 0.5), r.y + r.h * 0.5);
            ui.pointerUp(r.x + w * (index + 0.5), r.y + r.h * 0.5);
          };
          click(*find<Segmented>(ui, 0), type);
          click(*find<Segmented>(ui, 1), objects - 1);
          ui.setTime(ui.now() + 20);
          ui.tick();
          ui.pointerMove(-10, -10);
          return parent;
        };
        testing::RenderSpec spec;
        spec.width = 760;
        spec.height = 780;
        spec.theme = theme;
        spec.padding = 12;
        const image::Image img = testing::renderWidget(build, spec, paths);
        const std::string name = std::string("props-gallery-") + (type == 0 ? "transform" : type == 1 ? "material" : "lightsource") + "-" + std::to_string(objects) + "-" +
                                 (theme == r1ui::theme::ThemeId::Dark ? "dark" : "light") + ".png";
        image::writePng(paths.artifactDir / name, img.width, img.height, img.rgba);
        std::set<uint32_t> colours;
        for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) colours.insert(static_cast<uint32_t>(img.rgba[i]) << 16 | static_cast<uint32_t>(img.rgba[i + 1]) << 8 | img.rgba[i + 2]);
        std::printf("gallery %-40s %zu distinct colours -> %s\n", name.c_str(), colours.size(), (paths.artifactDir / name).string().c_str());
        r1test::report(colours.size() > 60, name.c_str(), __FILE__, __LINE__);
      }
    }
  }
  R1_EXPECT(r1ui::widgets::testing::validationMessageCount() == 0);  // Debug trees run with validation layers
  return r1test::finish();
}
