// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression tests for the TreeView fixes of the phase 4 review: collapsing the parent of a
//   dragged node mid-drag ends the drag instead of throwing from the next pointer move (M9), and
//   the timed work (slow-click rename, edge auto-scroll) runs from a context timer, never from paint
//   (M12).
// Callers: CTest (fast tier). Calls: UiContext, TreeView, SimpleTreeModel.
#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/tree/TreeView.h"

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;

namespace {

struct Rig {
  Rig() : t(500, 600) {
    t.ui.rootStyle().direction = layout::FlexDirection::Column;
    NodeFlags f;
    f.draggable = true;
    f.acceptsDrops = true;
    f.selectable = true;
    model = std::make_shared<SimpleTreeModel>();
    model->add(kTreeRoot, 1, "Frame", "frame", f);
    model->add(1, 2, "A", "square", f);
    model->add(1, 3, "B", "square", f);
    model->add(kTreeRoot, 6, "Ellipse", "circle", f);
    tree = &t.ui.create<TreeView>(t.ui.root());
    tree->style().width = layout::Length::px(260);
    tree->style().height = layout::Length::px(240);
    tree->setModel(model);
    tree->expandAll();
    t.layout();
  }
  double rowY(size_t row) {
    const auto rc = tree->rowRect(row);
    return rc.y + rc.h / 2.0;
  }
  void paint() {
    r1ui::render::Painter painter;
    painter.begin(500, 600);
    t.ui.paint(painter);
    t.ui.finishPaint();
    painter.end();
  }
  r1test::TestUi t;
  std::shared_ptr<SimpleTreeModel> model;
  TreeView* tree = nullptr;
};

}  // namespace

int main() {
  {  // M9: collapse the parent of a dragged row without a model change, then keep moving the pointer
    Rig rig;
    const double x = rig.t.ui.absRect(rig.tree->id()).x + 120;
    rig.t.ui.setTime(1000);
    rig.t.ui.pointerMove(x, rig.rowY(1));
    rig.t.ui.pointerDown(x, rig.rowY(1));
    rig.t.ui.pointerMove(x, rig.rowY(1) + 10);
    rig.t.ui.pointerMove(x, rig.rowY(1) + 30);
    rig.tree->setExpanded(1, false);
    rig.t.ui.pointerMove(x, rig.rowY(1) + 12);
    R1_EXPECT(rig.t.ui.inputFaults() == 0);
    R1_EXPECT(!rig.t.ui.router().capturer().valid());
    rig.t.ui.pointerUp(x, rig.rowY(1));
    R1_EXPECT(rig.t.ui.inputFaults() == 0);
    R1_EXPECT(rig.model->label(2) == "A");  // nothing was dropped anywhere
  }
  {  // M12: the slow-click rename starts from the timer step, not from the paint walk
    Rig rig;
    rig.t.ui.setTime(10000);
    rig.tree->clearSelection();
    const double x = rig.t.ui.absRect(rig.tree->id()).x + 100;
    rig.t.ui.pointerMove(x, rig.rowY(1));
    rig.t.ui.pointerDown(x, rig.rowY(1));
    rig.t.ui.pointerUp(x, rig.rowY(1));
    rig.t.ui.setTime(12000);
    rig.t.ui.pointerDown(x, rig.rowY(1));  // already selected: arms the rename timer
    rig.t.ui.pointerUp(x, rig.rowY(1));
    R1_EXPECT(!rig.tree->renaming());
    rig.t.ui.setTime(12700);
    rig.paint();
    R1_EXPECT(!rig.tree->renaming());  // painting is read-only: no focus change, no editor
    R1_EXPECT(rig.t.ui.msUntilTick().has_value());
    rig.t.ui.tick();
    R1_EXPECT(rig.tree->renaming());
  }
  return r1test::finish();
}
