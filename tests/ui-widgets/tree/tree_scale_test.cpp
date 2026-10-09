// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the scale and hostile-model oracle of TreeView: 100k rows (two-level tree and flat list) must
//   flatten, select, scroll, paint and navigate fast (times are printed and bounded), and models that
//   lie (cycles, repeated ids, the root as a child, 100k-deep chains, a revision that moves on every
//   call, a child count that disagrees with childAt) must never hang, overflow the stack or corrupt
//   the view.
// Callers: CTest (tree fast, no GPU; the painter records into a CPU list).
#include <chrono>
#include <cstdio>
#include <string>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Key;
namespace Mod = r1ui::core::events::Mod;
namespace layout = r1ui::core::layout;
using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

struct Rig {
  explicit Rig(std::shared_ptr<TreeModel> model) : t(500, 600) {
    t.ui.rootStyle().direction = layout::FlexDirection::Column;
    t.ui.rootStyle().alignItems = layout::Align::Start;
    tree = &t.ui.create<TreeView>(t.ui.root());
    tree->style().width = layout::Length::px(300);
    tree->style().height = layout::Length::px(400);
    tree->style().flexShrink = 0.0;
    tree->setModel(std::move(model));
    t.layout();
  }
  double paintMs() {
    r1ui::render::Painter painter;
    const auto start = Clock::now();
    painter.begin(500, 600);
    t.ui.paint(painter);
    t.ui.finishPaint();
    painter.end();
    return msSince(start);
  }
  r1test::TestUi t;
  TreeView* tree = nullptr;
};

void testHundredThousandRows() {
  auto start = Clock::now();
  auto m = std::make_shared<SimpleTreeModel>();
  m->reserve(100'100);
  // 100 groups of 1000 children.
  for (NodeId g = 1; g <= 100; ++g) {
    m->add(kTreeRoot, g, "Group " + std::to_string(g), "frame");
    for (NodeId c = 0; c < 1000; ++c) m->add(g, 1000 + g * 1000 + c, "Layer " + std::to_string(g) + "." + std::to_string(c), "square");
  }
  const double build = msSince(start);
  Rig rig(m);
  auto& tree = *rig.tree;
  R1_EXPECT(tree.rowCount() == 100);
  start = Clock::now();
  tree.expandAll();
  const double expand = msSince(start);
  R1_EXPECT(tree.rowCount() == 100 + 100 * 1000);
  rig.t.layout();
  // Paint at the top, in the middle and at the end: one frame stays cheap because only visible rows are drawn.
  double worstPaint = rig.paintMs();
  tree.scrollTo(tree.maxScroll() / 2);
  worstPaint = std::max(worstPaint, rig.paintMs());
  tree.scrollTo(1e12);
  worstPaint = std::max(worstPaint, rig.paintMs());
  R1_EXPECT(tree.scrollOffset() == tree.maxScroll() && tree.maxScroll() == 100100.0 * 24 - 400);
  // Selection: select all, then keyboard navigation over a hundred thousand rows.
  rig.t.ui.router().focus(tree.id(), r1ui::core::events::FocusReason::Keyboard);
  start = Clock::now();
  R1_EXPECT(rig.t.ui.keyDown(Key::A, Mod::kCtrl));
  const double selectAll = msSince(start);
  R1_EXPECT(tree.selectionCount() == 100100);
  start = Clock::now();
  const std::vector<NodeId> all = tree.selection();
  const double listSelection = msSince(start);
  R1_EXPECT(all.size() == 100100 && all.front() == 1 && all[1] == 2000);
  tree.clearSelection();
  start = Clock::now();
  rig.t.ui.keyDown(Key::Home);
  for (int i = 0; i < 2000; ++i) rig.t.ui.keyDown(Key::Down);
  rig.t.ui.keyDown(Key::End);
  const double navigate = msSince(start);
  R1_EXPECT(tree.selectionCount() == 1 && tree.cursorNode() == 1000 + 100 * 1000 + 999);
  // Shift range over the whole list, then a refresh with the selection intact.
  rig.t.ui.keyDown(Key::Home, Mod::kShift);
  R1_EXPECT(tree.selectionCount() == 100100);
  start = Clock::now();
  tree.refresh();
  const double refresh = msSince(start);
  R1_EXPECT(tree.selectionCount() == 100100);
  // Removing a subtree while everything is selected prunes the selection once.
  int events = 0;
  tree.setOnSelectionChanged([&](TreeView&) { ++events; });
  start = Clock::now();
  m->remove(50);
  tree.refresh();
  const double prune = msSince(start);
  R1_EXPECT(tree.selectionCount() == 100100 - 1001 && events == 1);
  // Scrolling to a node and dragging payloads over many selected rows.
  R1_EXPECT(tree.scrollToNode(1000 + 10 * 1000 + 5));
  std::printf("tree 100k rows: model build %.0f ms, expandAll %.0f ms, worst paint %.2f ms, select all %.0f ms, list selection %.0f ms, 2000 Down keys %.0f ms, refresh %.0f ms, prune %.0f ms\n", build,
              expand, worstPaint, selectAll, listSelection, navigate, refresh, prune);
  R1_EXPECT(build < 4000.0 && expand < 1500.0 && worstPaint < 60.0 && selectAll < 1500.0 && navigate < 4000.0 && refresh < 1500.0 && prune < 2500.0);

  // A flat list of 100k rows drawn as a List.
  auto flat = std::make_shared<SimpleTreeModel>();
  flat->reserve(100'000);
  for (NodeId i = 1; i <= 100'000; ++i) flat->add(kTreeRoot, i, "Item " + std::to_string(i), "file");
  Rig listRig(flat);
  listRig.tree->setAppearance(TreeAppearance::List);
  R1_EXPECT(listRig.tree->rowCount() == 100'000);
  listRig.tree->scrollToNode(99'999, true);
  const double listPaint = listRig.paintMs();
  R1_EXPECT(listPaint < 60.0);
  listRig.t.ui.router().focus(listRig.tree->id(), r1ui::core::events::FocusReason::Keyboard);
  listRig.t.ui.setTime(1);
  listRig.t.ui.textInput('i');
  listRig.t.ui.textInput('t');
  listRig.t.ui.textInput('e');
  listRig.t.ui.textInput('m');
  listRig.t.ui.textInput(' ');
  listRig.t.ui.textInput('9');
  listRig.t.ui.textInput('9');
  listRig.t.ui.textInput('9');
  listRig.t.ui.textInput('9');
  R1_EXPECT(listRig.tree->selection() == std::vector<NodeId>({9999}));
}

// A model whose structure is hostile in a different way per test.
class HostileModel : public TreeModel {
 public:
  enum class Kind { Cycle, Duplicates, RootChild, DeepChain, MovingRevision, LyingCount };
  explicit HostileModel(Kind kind) : kind_(kind) {}
  size_t childCount(NodeId parent) const override {
    switch (kind_) {
      case Kind::Cycle: return 1;  // 0 -> 1 -> 2 -> 1 -> 2 ...
      case Kind::Duplicates: return parent == kTreeRoot ? 5 : 0;
      case Kind::RootChild: return parent == kTreeRoot ? 3 : 0;
      case Kind::DeepChain: return parent < 100000 ? 1 : 0;
      case Kind::MovingRevision: return parent == kTreeRoot ? 200 : 0;
      case Kind::LyingCount: return parent == kTreeRoot ? 1000000 : 0;  // childAt answers for the first three only
    }
    return 0;
  }
  NodeId childAt(NodeId parent, size_t index) const override {
    switch (kind_) {
      case Kind::Cycle: return parent == 1 ? 2 : 1;
      case Kind::Duplicates: return 7;
      case Kind::RootChild: return index == 1 ? kTreeRoot : index + 10;
      case Kind::DeepChain: return parent + 1;
      case Kind::MovingRevision: return index + 1;
      case Kind::LyingCount: return index < 3 ? index + 1 : kTreeRoot;
    }
    return kTreeRoot;
  }
  bool contains(NodeId id) const override { return id != kTreeRoot; }
  std::string_view label(NodeId) const override { return "node"; }
  uint64_t revision() const override { return kind_ == Kind::MovingRevision ? ++counter_ : 1; }

 private:
  Kind kind_;
  mutable uint64_t counter_ = 0;
};

void testHostileModels() {
  for (const auto kind : {HostileModel::Kind::Cycle, HostileModel::Kind::Duplicates, HostileModel::Kind::RootChild, HostileModel::Kind::DeepChain, HostileModel::Kind::MovingRevision,
                          HostileModel::Kind::LyingCount}) {
    auto model = std::make_shared<HostileModel>(kind);
    Rig rig(model);
    auto& tree = *rig.tree;
    const auto start = Clock::now();
    tree.expandAll();
    tree.setExpanded(1, true, true);
    const size_t rows = tree.rowCount();
    R1_EXPECT(rows < 5'000'000);
    rig.paintMs();
    tree.scrollTo(1e12);
    rig.paintMs();
    rig.t.ui.router().focus(tree.id(), r1ui::core::events::FocusReason::Keyboard);
    rig.t.ui.keyDown(Key::End);
    rig.t.ui.keyDown(Key::Home);
    rig.t.ui.keyDown(Key::Right);
    rig.t.ui.keyDown(Key::A, Mod::kCtrl);
    R1_EXPECT(msSince(start) < 10000.0);
    if (kind == HostileModel::Kind::Duplicates) R1_EXPECT(rows == 1);          // a node listed five times is shown once
    if (kind == HostileModel::Kind::RootChild) R1_EXPECT(rows == 2);           // the root as a child is skipped
    if (kind == HostileModel::Kind::Cycle) R1_EXPECT(rows == 2);               // 1 and 2 each once
    if (kind == HostileModel::Kind::DeepChain) R1_EXPECT(rows <= 300);         // the depth limit holds
    if (kind == HostileModel::Kind::LyingCount) R1_EXPECT(rows == 3);          // invalid children are skipped
  }
}

}  // namespace

int main() try {
  testHundredThousandRows();
  testHostileModels();
  return r1test::finish();
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}
