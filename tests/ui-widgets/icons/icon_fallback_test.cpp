// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression tests for icons that cannot be loaded (phase 4 review M13): drawing never throws,
//   a failed (name, size) is remembered so the disk is not searched every frame, the placeholder is a
//   hollow square, a named fallback icon is used when given, and the Painter's clip and opacity
//   stacks stay balanced when a widget names an icon that does not exist.
// Callers: CTest (fast tier; the Painter records instances without a GPU).
#include "../textinput/FieldRig.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/numberfield/NumberField.h"

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;

namespace {

const r1ui::render::Color kTint{1, 1, 1, 1};

}  // namespace

int main() {
  {  // the icon cache itself
    r1test::TestUi t;
    IconCache& icons = t.ui.icons();
    r1ui::render::Painter painter;
    painter.begin(100, 100);
    icons.draw(painter, "x", 10, 10, 16, kTint);
    R1_EXPECT(painter.stats().texInstances == 1 && icons.failedLoads() == 0);
    icons.draw(painter, "no-such-icon", 30, 10, 16, kTint);
    R1_EXPECT(painter.stats().texInstances == 1 && painter.stats().sdfInstances >= 1);  // a placeholder square
    R1_EXPECT(icons.failedLoads() == 1);
    for (int i = 0; i < 50; ++i) icons.draw(painter, "no-such-icon", 30, 10, 16, kTint);
    R1_EXPECT(icons.failedLoads() == 1);  // remembered, not searched again
    icons.draw(painter, "../etc/passwd", 50, 10, 16, kTint);  // an invalid name is a failure like any other
    R1_EXPECT(icons.failedLoads() == 2);
    const uint32_t texBefore = painter.stats().texInstances;
    icons.draw(painter, "no-such-icon", 70, 10, 16, kTint, "file");  // the same failed pair, now with a fallback icon
    R1_EXPECT(painter.stats().texInstances == texBefore + 1);
    painter.end();
  }
  {  // a widget that names a missing icon paints, and its frame ends cleanly
    r1test::FieldRig rig;
    NumberField& f = rig.ui.create<NumberField>(rig.ui.root());
    f.style().width = layout::Length::px(100);
    f.setLeadingIcon("no-such-icon");
    rig.layout();
    bool threw = false;
    try {
      rig.paint();
      rig.paint();
    } catch (const std::exception&) {
      threw = true;
    }
    R1_EXPECT(!threw);
    R1_EXPECT(rig.ui.icons().failedLoads() == 1);
  }
  return r1test::finish();
}
