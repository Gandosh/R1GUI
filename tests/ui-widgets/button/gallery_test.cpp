// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: smoke oracle for the buttons gallery page: it builds under a parent, lays out inside a
//   narrow and a wide window without widgets leaving the window horizontally (rows wrap), paints at
//   several display scales without throwing, creates every widget kind and tolerates a second build.
// Why: the gallery preview mounts this builder unchanged; a throw or a clipped row would only be
//   seen there.
// Callers: CTest (button fast, no GPU).
#include <cstdio>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/button/GalleryButtons.h"
#include "r1ui/widgets/checkbox/Checkbox.h"
#include "r1ui/widgets/iconbutton/IconButton.h"
#include "r1ui/widgets/segmented/Segmented.h"
#include "r1ui/widgets/switch/Switch.h"

namespace {

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;

size_t countOf(UiContext& ui, const char* typeName) {
  size_t n = 0;
  ui.tree().forEachDescendant(ui.root(), [&](r1ui::core::tree::WidgetId id) {
    const WidgetObject* o = ui.object(id);
    if (o != nullptr && std::string(o->typeName()) == typeName) ++n;
  });
  return n;
}

}  // namespace

int main() {
  for (const int width : {360, 900, 1400}) {
    for (const float scale : {1.0f, 1.5f}) {
      r1test::TestUi t(width, 900, scale);
      auto& ui = t.ui;
      ui.rootStyle().direction = layout::FlexDirection::Column;
      ui.rootStyle().alignItems = layout::Align::Stretch;
      buildGalleryButtons(ui, ui.root());
      t.layout();
      R1_EXPECT(countOf(ui, "Button") == 35);
      R1_EXPECT(countOf(ui, "IconButton") == 7);
      R1_EXPECT(countOf(ui, "Checkbox") == 6);
      R1_EXPECT(countOf(ui, "Switch") == 10);
      R1_EXPECT(countOf(ui, "Segmented") == 6);
      if (width == 900 && scale == 1.0f) std::printf("gallery nodes: %zu\n", ui.widgetCount());
      // Nothing starts right of the window (rows wrap); single wide items may still overflow the narrow one.
      bool inside = true;
      ui.tree().forEachDescendant(ui.root(), [&](r1ui::core::tree::WidgetId id) {
        const WidgetObject* o = ui.object(id);
        if (o == nullptr || std::string(o->typeName()) == "GalleryBox") return;
        if (ui.absRect(id).x > ui.viewportWidth()) inside = false;
      });
      R1_EXPECT(inside);
      r1ui::render::Painter painter;
      painter.begin(static_cast<uint32_t>(width * scale), static_cast<uint32_t>(900 * scale));
      ui.paint(painter);
      ui.finishPaint();
      painter.end();
    }
  }
  // Two pages under one parent do not interfere.
  r1test::TestUi t(900, 900);
  buildGalleryButtons(t.ui, t.ui.root());
  buildGalleryButtons(t.ui, t.ui.root());
  t.layout();
  R1_EXPECT(countOf(t.ui, "Switch") == 20);
  return r1test::finish();
}
