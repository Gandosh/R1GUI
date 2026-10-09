// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GalleryEditors.h.
// Invariants: every widget gets an explicit size; the sample data is generated, never loaded.
// Callers: the widget gallery host.
#include "r1ui/widgets/thumbnailgrid/GalleryEditors.h"

#include <string>

#include "r1ui/widgets/colorpicker/ColorPicker.h"
#include "r1ui/widgets/curveeditor/CurveEditor.h"
#include "r1ui/widgets/gradient/GradientEditor.h"
#include "r1ui/widgets/thumbnailgrid/ThumbnailGrid.h"

namespace r1ui::widgets {

namespace {

namespace layout = r1ui::core::layout;

// A few hundred assets of different kinds; names are numbered so natural sorting is visible.
const VectorAssetModel& sampleAssets() {
  static const VectorAssetModel model = [] {
    VectorAssetModel m;
    const char* kinds[] = {"Texture", "Mesh", "Material", "Prefab", "Audio"};
    const char* icons[] = {"image", "box", "palette", "package", "volume-2"};
    for (uint64_t i = 0; i < 240; ++i) {
      GridItem item;
      item.key = i + 1;
      item.folder = i < 6;
      item.name = item.folder ? "Folder " + std::to_string(i + 1) : std::string(kinds[i % 5]) + " " + std::to_string(i + 1);
      item.typeLabel = item.folder ? "Folder" : kinds[i % 5];
      item.icon = item.folder ? "folder" : icons[i % 5];
      item.modified = i % 17 == 4;
      m.items.push_back(std::move(item));
    }
    return m;
  }();
  return model;
}

}  // namespace

void buildGalleryEditors(UiContext& ui, core::tree::WidgetId parent) {
  ColorPicker& picker = ui.create<ColorPicker>(parent);
  picker.style().flexGrow = 0.0;
  picker.setColor(color::Rgba{color::Rgb{0.2, 0.55, 0.9}, 1.0});

  GradientEditor& gradientEditor = ui.create<GradientEditor>(parent);
  gradientEditor.style().flexGrow = 0.0;
  gradientEditor.setGradient(gradient::Gradient());

  CurveEditor& curves = ui.create<CurveEditor>(parent);
  curves.style().width = layout::Length::px(520);
  curves.style().height = layout::Length::px(320);
  curves.style().flexGrow = 0.0;
  curve::Curve c;
  c.id = 1;
  c.name = "Value";
  c.colour = {0.9, 0.3, 0.2};
  for (uint32_t i = 0; i < 4; ++i) {
    curve::Key k;
    k.id = i + 1;
    k.time = static_cast<double>(i);
    k.value = (i % 2 == 0) ? 0.0 : 1.0;
    k.interp = curve::Interp::Cubic;
    c.keys.push_back(k);
  }
  curves.graph().setCurves({c});

  ThumbnailGrid& grid = ui.create<ThumbnailGrid>(parent);
  grid.style().width = layout::Length::px(520);
  grid.style().height = layout::Length::px(360);
  grid.style().flexGrow = 0.0;
  grid.setModel(&sampleAssets());
}

}  // namespace r1ui::widgets
