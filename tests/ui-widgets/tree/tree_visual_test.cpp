// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for TreeView against the layer tree captures in both themes: the whole layer
//   tree (idle with an unfocused selection, the same with the inner frame collapsed, and with the
//   focused selection), single rows (idle, hover with the row actions, hidden, selected focused and
//   unfocused in idle and hover, the inline rename field), the hover actions (row hovered, action
//   hovered) and the disclosure chevron expanded and collapsed. A row is compared by building the
//   parent chain above it and scrolling the 24 px high view down to it, so the crop's offsets
//   (6 px of context around the row) are reproduced exactly.
// Callers: CTest (tree gpu: renders offscreen on a Vulkan device, no window).
#include "../g3support/RegionCompare.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;
using r1ui::theme::ThemeId;
using r1test::visual::RegionSpec;
using r1test::visual::VisualState;

enum class Pointer { None, Row, Action, Disclosure };

struct RowCase {
  int depth = 1;            // nesting level of the row under test (its parents are frames)
  bool selected = false;
  bool hidden = false;
  bool locked = false;
  bool collapsedChild = false;  // the row is a frame whose child is collapsed (disclosure points right)
  bool rename = false;
  Pointer pointer = Pointer::None;
  int left = 4, top = 6;    // where the row starts in the render
  int width = 250, height = 24;
  bool sibling = false;     // a second row below, partially visible (rename crop)
};

r1test::visual::Build rowAt(RowCase c) {
  return [c](UiContext& ui, WidgetId parent) {
    ui.rootStyle().alignItems = layout::Align::Start;
    auto model = std::make_shared<SimpleTreeModel>();
    NodeId prev = kTreeRoot;
    NodeId next = 1;
    for (int d = 0; d < c.depth; ++d) {
      model->add(prev, next, "Frame", "frame");
      prev = next++;
    }
    NodeFlags flags;
    flags.hidden = c.hidden;
    flags.locked = c.locked;
    const NodeId row = next++;
    model->add(prev, row, c.collapsedChild ? "Frame" : "Rectangle", c.collapsedChild ? "frame" : "square", flags);
    if (c.collapsedChild) model->add(row, next++, "Rectangle", "square");
    if (c.sibling) model->add(prev, next++, "Rectangle", "square");
    TreeView& tree = ui.create<TreeView>(parent);
    tree.style().width = layout::Length::px(c.width);
    tree.style().height = layout::Length::px(c.height);
    tree.style().flexShrink = 0.0;
    tree.style().margin[layout::kLeft] = layout::Length::px(c.left);
    tree.style().margin[layout::kTop] = layout::Length::px(c.top);
    tree.setModel(model);
    tree.setScrollbarPolicy(ScrollbarPolicy::Never);
    for (NodeId id = 1; id <= static_cast<NodeId>(c.depth); ++id) tree.setExpanded(id, true);
    if (c.selected) tree.select(row);
    ui.frame();
    tree.scrollTo(c.depth * 24.0);
    if (c.rename) {
      tree.beginRename(row);
      tree.scrollTo(c.depth * 24.0);
    }
    ui.frame();
    const layout::Rect area = ui.absRect(tree.id());
    switch (c.pointer) {
      case Pointer::None: break;
      case Pointer::Row: ui.pointerMove(area.x + 2, area.y + 12); break;
      case Pointer::Action: {
        const layout::Rect a = tree.actionRect(static_cast<size_t>(c.depth), RowAction::ToggleVisibility);
        ui.pointerMove(a.x + 8, a.y + 8);
        break;
      }
      case Pointer::Disclosure: {
        const layout::Rect d = tree.disclosureRect(static_cast<size_t>(c.depth));
        ui.pointerMove(d.x + 8, d.y + 8);
        break;
      }
    }
    return tree.id();
  };
}

// The layer tree of the capture: Frame { Rectangle, Rectangle, Frame { Rectangle (selected) } }.
r1test::visual::Build wholeTree(bool collapseInner) {
  return [collapseInner](UiContext& ui, WidgetId parent) {
    ui.rootStyle().alignItems = layout::Align::Start;
    auto model = std::make_shared<SimpleTreeModel>();
    model->add(kTreeRoot, 1, "Frame", "frame");
    model->add(1, 2, "Rectangle", "square");
    model->add(1, 3, "Rectangle", "square");
    model->add(1, 4, "Frame", "frame");
    model->add(4, 5, "Rectangle", "square");
    TreeView& tree = ui.create<TreeView>(parent);
    tree.style().width = layout::Length::px(250);
    tree.style().height = layout::Length::px(collapseInner ? 96 : 120);
    tree.style().flexShrink = 0.0;
    tree.style().margin[layout::kLeft] = layout::Length::px(4);
    tree.style().margin[layout::kTop] = layout::Length::px(6);
    tree.setModel(model);
    tree.expandAll();
    tree.select(5);
    if (collapseInner) tree.setExpanded(4, false);
    ui.frame();
    return tree.id();
  };
}

}  // namespace

int main() {
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    const auto text = [&](const char* ref, int w, int h, const char* tag) {
      return RegionSpec{.reference = ref, .x = 0, .y = 0, .w = w, .h = h, .theme = theme, .profile = "text", .tag = tag, .luminance = true};
    };
    // The whole tree.
    R1_EXPECT_MATCHES_REGION(wholeTree(false), text("widget-layer-tree-idle", 260, 132, "tree-idle"));
    R1_EXPECT_MATCHES_REGION(wholeTree(true), text("widget-layer-tree-collapsed-frame", 260, 108, "tree-collapsed-frame"));
    {
      RegionSpec spec = text("widget-layer-tree-selected-focused", 260, 132, "tree-selected-focused");
      spec.state = VisualState::Focus;
      R1_EXPECT_MATCHES_REGION(wholeTree(false), spec);
    }
    // Rows.
    R1_EXPECT_MATCHES_REGION(rowAt({}), text("widget-layer-row-idle", 260, 36, "row-idle"));
    R1_EXPECT_MATCHES_REGION(rowAt({.pointer = Pointer::Row}), text("widget-layer-row-hover", 260, 36, "row-hover"));
    R1_EXPECT_MATCHES_REGION(rowAt({.hidden = true}), text("widget-layer-row-hidden-idle", 260, 36, "row-hidden"));
    R1_EXPECT_MATCHES_REGION(rowAt({.depth = 0, .selected = true}), text("widget-layer-row-selected-idle", 262, 36, "row-selected-idle"));
    R1_EXPECT_MATCHES_REGION(rowAt({.depth = 0, .selected = true, .pointer = Pointer::Row}), text("widget-layer-row-selected-hover", 262, 36, "row-selected-hover"));
    R1_EXPECT_MATCHES_REGION(rowAt({.depth = 2, .selected = true}), text("widget-layer-row-selected-unfocused-idle", 260, 36, "row-selected-unfocused-idle"));
    R1_EXPECT_MATCHES_REGION(rowAt({.depth = 2, .selected = true, .pointer = Pointer::Row}), text("widget-layer-row-selected-unfocused-hover", 260, 36, "row-selected-unfocused-hover"));
    {
      RegionSpec spec = text("widget-layer-row-selected-focused-idle", 260, 36, "row-selected-focused-idle");
      spec.state = VisualState::Focus;
      R1_EXPECT_MATCHES_REGION(rowAt({.selected = true}), spec);
      RegionSpec hover = text("widget-layer-row-selected-focused-hover", 260, 36, "row-selected-focused-hover");
      hover.state = VisualState::Focus;
      R1_EXPECT_MATCHES_REGION(rowAt({.selected = true, .pointer = Pointer::Row}), hover);
    }
    R1_EXPECT_MATCHES_REGION(rowAt({.rename = true, .height = 32, .sibling = true}), text("widget-layer-row-rename-focus", 260, 38, "row-rename"));
    // Row actions: the 28 x 28 crop shows one 16 px action at (6, 6) of a hovered row.
    R1_EXPECT_MATCHES_REGION(rowAt({.pointer = Pointer::Row, .left = 0, .top = 2, .width = 26}), text("widget-layer-row-action-row-hover", 28, 28, "row-action-row-hover"));
    R1_EXPECT_MATCHES_REGION(rowAt({.pointer = Pointer::Action, .left = 0, .top = 2, .width = 26}), text("widget-layer-row-action-hover", 28, 28, "row-action-hover"));
    // The disclosure chevron: expanded (a frame) and collapsed.
    R1_EXPECT_MATCHES_REGION(rowAt({.depth = 1, .left = -12, .top = 2, .width = 60}), text("widget-layer-disclosure-idle", 24, 28, "disclosure-expanded-idle"));
    R1_EXPECT_MATCHES_REGION(rowAt({.depth = 1, .pointer = Pointer::Disclosure, .left = -12, .top = 2, .width = 200}), text("widget-layer-disclosure-hover", 24, 28, "disclosure-expanded-hover"));
    R1_EXPECT_MATCHES_REGION(rowAt({.depth = 1, .collapsedChild = true, .left = -10, .top = 0, .width = 60}), text("widget-layer-disclosure-collapsed", 28, 24, "disclosure-collapsed"));
  }
  return r1test::finish();
}
