// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for TreeView and SimpleTreeModel: model editing rules, row flattening and
//   expansion, measured row geometry (24 px rows, 16 px per level), pointer and keyboard selection
//   (spec 08 rules 45-65 and scenarios 1, 2, 3, 11), scrolling and the scrollbar, hover actions,
//   inline rename (F2, slow click, validation, commit and cancel), drag and drop zones and
//   handlers, edge auto-scroll, type-to-search, selection persistence across model changes, and
//   handlers that destroy the view or disable it mid-gesture.
// Callers: CTest (tree fast, no GPU).
#include <cmath>
#include <string>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Button;
using r1ui::core::events::Key;
namespace Mod = r1ui::core::events::Mod;
namespace layout = r1ui::core::layout;
using r1ui::core::tree::WidgetId;

// Ids of the sample tree (the reference's layer tree plus a few more rows):
//  1 Frame
//    2 Rectangle
//    3 Rectangle
//    4 Frame
//      5 Rectangle
//  6 Ellipse
//  7 Group
//    8 Text
std::shared_ptr<SimpleTreeModel> sampleModel() {
  auto m = std::make_shared<SimpleTreeModel>();
  m->add(kTreeRoot, 1, "Frame", "frame");
  m->add(1, 2, "Rectangle", "square");
  m->add(1, 3, "Rectangle", "square");
  m->add(1, 4, "Frame", "frame");
  m->add(4, 5, "Rectangle", "square");
  m->add(kTreeRoot, 6, "Ellipse", "circle");
  m->add(kTreeRoot, 7, "Group", "group");
  m->add(7, 8, "Text", "type");
  return m;
}

struct Rig {
  explicit Rig(std::shared_ptr<SimpleTreeModel> model = sampleModel(), double w = 260, double h = 240) : t(500, 600), m(std::move(model)) {
    t.ui.rootStyle().direction = layout::FlexDirection::Column;
    t.ui.rootStyle().alignItems = layout::Align::Start;
    tree = &t.ui.create<TreeView>(t.ui.root());
    tree->style().width = layout::Length::px(w);
    tree->style().height = layout::Length::px(h);
    tree->style().flexShrink = 0.0;
    tree->setModel(m);
    tree->setOnSelectionChanged([this](TreeView&) { ++selectionEvents; });
    t.layout();
  }
  layout::Rect area() { return t.ui.absRect(tree->id()); }
  // Window coordinates of the middle of a row (x offset 100 is on the label).
  double rowY(size_t row) {
    const layout::Rect r = tree->rowRect(row);
    return r.y + r.h / 2.0;
  }
  double labelX() { return area().x + 120; }
  void clickRow(size_t row, uint8_t mods = 0, Button b = Button::Left) {
    if (autoTime) t.ui.setTime(clock += 1000);  // separate clicks are never a double click
    t.ui.pointerMove(labelX(), rowY(row), mods);
    t.ui.pointerDown(labelX(), rowY(row), b, mods);
    t.ui.pointerUp(labelX(), rowY(row), b, mods);
  }
  void paint() {
    r1ui::render::Painter painter;
    painter.begin(500, 600);
    t.ui.paint(painter);
    t.ui.finishPaint();
    painter.end();
  }
  r1test::TestUi t;
  std::shared_ptr<SimpleTreeModel> m;
  TreeView* tree = nullptr;
  int selectionEvents = 0;
  uint64_t clock = 100;
  bool autoTime = true;
};

std::vector<NodeId> ids(std::initializer_list<NodeId> v) { return v; }

void testModel() {
  SimpleTreeModel m;
  const uint64_t r0 = m.revision();
  R1_EXPECT(m.add(kTreeRoot, 10, "A") && m.add(10, 11, "B") && m.add(10, 12, "C", "", {}, 0));
  R1_EXPECT(m.childCount(10) == 2 && m.childAt(10, 0) == 12 && m.childAt(10, 1) == 11 && m.revision() > r0);
  R1_EXPECT(!m.add(kTreeRoot, 10, "dup") && !m.add(kTreeRoot, 0, "zero") && !m.add(99, 13, "orphan"));
  R1_EXPECT(m.contains(11) && !m.contains(0) && !m.contains(99));
  R1_EXPECT(m.parentOf(11) == 10 && m.indexInParent(11) == 1 && m.indexInParent(99) == static_cast<size_t>(-1));
  // Moves: into a descendant is rejected, a valid move reorders, indices clamp.
  R1_EXPECT(!m.move(10, 11, 0) && !m.move(10, 10, 0) && !m.move(99, kTreeRoot, 0) && !m.move(10, 99, 0));
  R1_EXPECT(m.move(11, kTreeRoot, 0) && m.childAt(kTreeRoot, 0) == 11 && m.parentOf(11) == kTreeRoot && m.childCount(10) == 1);
  R1_EXPECT(m.move(12, kTreeRoot, 1000) && m.childAt(kTreeRoot, 2) == 12);
  const uint64_t r1 = m.revision();
  R1_EXPECT(m.setLabel(10, "same") && m.revision() > r1);
  const uint64_t r2 = m.revision();
  R1_EXPECT(m.setLabel(10, "same") && m.revision() == r2);  // no change, no revision
  R1_EXPECT(!m.setLabel(99, "x") && !m.setFlags(99, {}) && !m.setIcon(99, "x"));
  m.setLabel(10, std::string(100000, 'x'));
  R1_EXPECT(m.label(10).size() == SimpleTreeModel::kMaxLabelBytes);
  m.add(10, 20, "child");
  m.add(20, 21, "grandchild");
  R1_EXPECT(m.remove(10) && !m.contains(20) && !m.contains(21) && !m.remove(10));
  R1_EXPECT(m.size() == 2);  // 11 and 12 remain
  R1_EXPECT(m.label(99).empty() && m.icon(99).empty() && m.childAt(99, 0) == kTreeRoot && m.childCount(99) == 0);
}

void testRowsAndExpansion() {
  Rig rig;
  auto& tree = *rig.tree;
  R1_EXPECT(tree.rowCount() == 3);  // collapsed: Frame, Ellipse, Group
  R1_EXPECT(tree.nodeAtRow(0) == 1 && tree.nodeAtRow(1) == 6 && tree.nodeAtRow(2) == 7 && tree.nodeAtRow(3) == kTreeRoot);
  R1_EXPECT(tree.setExpanded(1, true));
  R1_EXPECT(tree.rowCount() == 6 && tree.nodeAtRow(1) == 2 && tree.nodeAtRow(3) == 4 && tree.depthOfRow(1) == 1 && tree.depthOfRow(3) == 1);
  R1_EXPECT(!tree.setExpanded(1, true));            // already expanded
  R1_EXPECT(!tree.setExpanded(6, true));            // a leaf cannot expand
  R1_EXPECT(!tree.setExpanded(99, true));           // unknown id
  R1_EXPECT(tree.setExpanded(4, true) && tree.rowCount() == 7 && tree.depthOfRow(4) == 2);
  tree.collapseAll();
  R1_EXPECT(tree.rowCount() == 3);
  tree.expandAll();
  R1_EXPECT(tree.rowCount() == 8 && tree.expandedNodes() == std::vector<NodeId>({1, 4, 7}));
  R1_EXPECT(tree.setExpanded(1, false, true) && tree.rowCount() == 4 && !tree.isExpanded(4));  // recursive collapse closes 4 too
  tree.setExpandedNodes({1, 99, 7});
  R1_EXPECT(tree.rowCount() == 7 && !tree.isExpanded(99));
  // Expansion survives model changes; removed nodes drop out.
  rig.m->add(1, 9, "Late");
  R1_EXPECT(tree.rowCount() == 8);
  rig.m->remove(7);
  R1_EXPECT(tree.rowCount() == 6 && tree.rowOfNode(8) == std::nullopt && tree.rowOfNode(9).value() == 4);
  // Row geometry: 24 px rows, 16 px of indent per level, 16 px disclosure box, 12 px icon.
  const layout::Rect a = rig.area();
  const layout::Rect r0 = tree.rowRect(0);
  const layout::Rect r1 = tree.rowRect(1);
  R1_EXPECT(r0.x == a.x && r0.y == a.y && r0.h == 24 && r1.y == a.y + 24 && r0.w == 260);
  R1_EXPECT(tree.disclosureRect(0).x == a.x && tree.disclosureRect(0).w == 16 && tree.disclosureRect(0).h == 16 && tree.disclosureRect(0).y == a.y + 4);
  R1_EXPECT(tree.disclosureRect(3).x == a.x + 16);
  R1_EXPECT(tree.actionRect(0, RowAction::ToggleVisibility).x == a.x + 260 - 4 - 16);
  R1_EXPECT(tree.actionRect(0, RowAction::ToggleLock).x == a.x + 260 - 4 - 16 - 4 - 16);
  rig.paint();
}

void testPointerSelection() {
  Rig rig;
  auto& tree = *rig.tree;
  tree.expandAll();
  rig.t.layout();
  // rows: 0 Frame,1 Rect,2 Rect,3 Frame,4 Rect,5 Ellipse,6 Group,7 Text
  rig.clickRow(2);
  R1_EXPECT(tree.selection() == ids({3}) && tree.cursorNode() == 3 && tree.anchorNode() == 3 && rig.selectionEvents == 1);
  rig.clickRow(4, Mod::kShift);  // scenario 1: Shift-click adds the range, the anchor stays
  R1_EXPECT(tree.selection() == ids({3, 4, 5}) && tree.anchorNode() == 3);
  rig.clickRow(0, Mod::kShift);  // from the same anchor: rows 0..2
  R1_EXPECT(tree.selection() == ids({1, 2, 3, 4, 5}));  // existing selection outside the span is kept
  rig.clickRow(6, Mod::kCtrl);
  R1_EXPECT(tree.isSelected(7) && tree.selectionCount() == 6);
  rig.clickRow(6, Mod::kCtrl);
  R1_EXPECT(!tree.isSelected(7));
  rig.clickRow(5);
  R1_EXPECT(tree.selection() == ids({6}));
  // Scenario 2: press on a selected row of a multi selection keeps it during the press, collapses on release.
  tree.setSelection({2, 3, 5});
  rig.selectionEvents = 0;
  const double x = rig.labelX(), y = rig.rowY(2);
  rig.t.ui.pointerMove(x, y);
  rig.t.ui.pointerDown(x, y);
  R1_EXPECT(tree.selectionCount() == 3);
  rig.t.ui.pointerMove(x + 3, y);
  rig.t.ui.pointerUp(x + 3, y);
  R1_EXPECT(tree.selection() == ids({3}) && rig.selectionEvents == 1);
  // Press on empty space clears; with Ctrl nothing happens.
  const layout::Rect a = rig.area();
  rig.t.ui.pointerMove(a.x + 100, a.y + 230);
  rig.t.ui.pointerDown(a.x + 100, a.y + 230, Button::Left, Mod::kCtrl);
  rig.t.ui.pointerUp(a.x + 100, a.y + 230, Button::Left, Mod::kCtrl);
  R1_EXPECT(tree.selectionCount() == 1);
  rig.t.ui.pointerDown(a.x + 100, a.y + 230);
  rig.t.ui.pointerUp(a.x + 100, a.y + 230);
  R1_EXPECT(tree.selectionCount() == 0);
  // Disclosure: toggles without selecting; Shift toggles the whole subtree.
  const layout::Rect d = tree.disclosureRect(0);
  rig.t.ui.pointerMove(d.x + 8, d.y + 8);
  rig.t.ui.pointerDown(d.x + 8, d.y + 8);
  rig.t.ui.pointerUp(d.x + 8, d.y + 8);
  R1_EXPECT(!tree.isExpanded(1) && tree.selectionCount() == 0 && tree.rowCount() == 4);
  rig.t.ui.pointerDown(d.x + 8, d.y + 8, Button::Left, Mod::kShift);
  rig.t.ui.pointerUp(d.x + 8, d.y + 8, Button::Left, Mod::kShift);
  R1_EXPECT(tree.isExpanded(1) && tree.isExpanded(4) && tree.rowCount() == 8);
  // Double click: activates, or toggles when nothing handles it.
  NodeId activated = 0;
  rig.t.ui.setTime(1000);
  rig.t.ui.pointerMove(x, rig.rowY(3));
  for (int i = 0; i < 2; ++i) {
    rig.t.ui.pointerDown(x, rig.rowY(3));
    rig.t.ui.pointerUp(x, rig.rowY(3));
  }
  R1_EXPECT(!tree.isExpanded(4));  // toggled
  tree.setOnActivate([&](NodeId n) { activated = n; });
  rig.t.ui.setTime(5000);
  for (int i = 0; i < 2; ++i) {
    rig.t.ui.pointerDown(x, rig.rowY(3));
    rig.t.ui.pointerUp(x, rig.rowY(3));
  }
  R1_EXPECT(activated == 4 && !tree.isExpanded(4));
  // Context menu: selects the row when it is not selected and reports it; empty space reports none.
  NodeId ctx = 99;
  tree.setOnContextMenu([&](NodeId n, double, double) { ctx = n; });
  tree.clearSelection();
  rig.clickRow(tree.rowOfNode(6).value(), 0, Button::Right);
  R1_EXPECT(ctx == 6 && tree.selection() == ids({6}));
  tree.setSelection({2, 3});
  rig.clickRow(2, 0, Button::Right);
  R1_EXPECT(tree.selectionCount() == 2);  // an already selected row keeps the whole selection
  // Single mode ignores modifiers.
  tree.setSelectionMode(TreeSelectionMode::Single);
  R1_EXPECT(tree.selectionCount() == 1);
  rig.clickRow(tree.rowOfNode(6).value(), Mod::kCtrl);
  rig.clickRow(tree.rowOfNode(8).value(), Mod::kShift);
  R1_EXPECT(tree.selectionCount() == 1 && tree.isSelected(8));
  tree.setSelectionMode(TreeSelectionMode::None);
  R1_EXPECT(tree.selectionCount() == 0);
  rig.clickRow(1);
  R1_EXPECT(tree.selectionCount() == 0);
}

void testKeyboardSelection() {
  Rig rig;
  auto& tree = *rig.tree;
  tree.expandAll();
  rig.t.layout();
  R1_EXPECT(rig.t.ui.router().focus(tree.id(), r1ui::core::events::FocusReason::Keyboard));
  R1_EXPECT(rig.t.ui.keyDown(Key::Down) && tree.selection() == ids({1}));
  R1_EXPECT(rig.t.ui.keyDown(Key::Down) && tree.selection() == ids({2}));
  R1_EXPECT(rig.t.ui.keyDown(Key::End) && tree.selection() == ids({8}));
  R1_EXPECT(rig.t.ui.keyDown(Key::Down) && tree.selection() == ids({8}));  // used, nothing moves
  R1_EXPECT(rig.t.ui.keyDown(Key::Home) && tree.selection() == ids({1}));
  // Shift range (replaces), Ctrl+Shift range (adds), Ctrl add.
  rig.t.ui.keyDown(Key::Down);
  rig.t.ui.keyDown(Key::Down, Mod::kShift);
  rig.t.ui.keyDown(Key::Down, Mod::kShift);
  rig.t.ui.keyDown(Key::Down, Mod::kShift);
  R1_EXPECT(tree.selection() == ids({2, 3, 4, 5}) && tree.anchorNode() == 2);  // scenario 11 shape
  rig.t.ui.keyDown(Key::Down, Mod::kCtrl | Mod::kShift);
  R1_EXPECT(tree.selection() == ids({2, 3, 4, 5, 6}));
  rig.t.ui.keyDown(Key::Home);
  rig.t.ui.keyDown(Key::Down, Mod::kCtrl);
  rig.t.ui.keyDown(Key::Down, Mod::kCtrl);
  R1_EXPECT(tree.selection() == ids({1, 2, 3}) && tree.cursorNode() == 3);
  // Space selects the cursor row when it is not selected, Ctrl+Space toggles; Alt disables the keys.
  rig.t.ui.keyDown(Key::Home);
  rig.t.ui.keyDown(Key::Space, Mod::kCtrl);
  R1_EXPECT(tree.selectionCount() == 0);
  R1_EXPECT(rig.t.ui.keyDown(Key::Space) && tree.selection() == ids({1}));
  R1_EXPECT(!rig.t.ui.keyDown(Key::Space));  // already selected: not used
  R1_EXPECT(!rig.t.ui.keyDown(Key::Down, Mod::kAlt) && tree.selection() == ids({1}));
  R1_EXPECT(rig.t.ui.keyDown(Key::A, Mod::kCtrl) && tree.selectionCount() == 8);
  // Left / Right (scenario 10 and rule 37 / 38).
  rig.t.ui.keyDown(Key::Home);
  tree.collapseAll();
  R1_EXPECT(rig.t.ui.keyDown(Key::Right) && tree.isExpanded(1) && tree.selection() == ids({1}));
  R1_EXPECT(rig.t.ui.keyDown(Key::Right) && tree.selection() == ids({2}));   // first child
  R1_EXPECT(rig.t.ui.keyDown(Key::Left) && tree.selection() == ids({1}));    // leaf: its parent
  R1_EXPECT(rig.t.ui.keyDown(Key::Left) && !tree.isExpanded(1));             // expanded: collapse
  R1_EXPECT(rig.t.ui.keyDown(Key::Left) && tree.selection() == ids({1}));    // top level collapsed: nothing to do
  // Page keys move by the whole rows of the viewport (240 / 24 = 10 rows).
  auto big = std::make_shared<SimpleTreeModel>();
  for (NodeId i = 1; i <= 100; ++i) big->add(kTreeRoot, i, "Item " + std::to_string(i));
  rig.tree->setModel(big);
  rig.t.layout();
  rig.t.ui.keyDown(Key::Home);
  R1_EXPECT(tree.wholeRowsVisible() == 10);
  rig.t.ui.keyDown(Key::PageDown);
  R1_EXPECT(tree.selection() == ids({11}));
  R1_EXPECT(tree.scrollOffset() > 0.0);
  rig.t.ui.keyDown(Key::PageUp);
  R1_EXPECT(tree.selection() == ids({1}) && tree.scrollOffset() == 0.0);
  rig.t.ui.keyDown(Key::End);
  R1_EXPECT(tree.selection() == ids({100}) && tree.scrollOffset() == tree.maxScroll());
  // Enter activates the cursor row.
  NodeId activated = 0;
  tree.setOnActivate([&](NodeId n) { activated = n; });
  rig.t.ui.keyDown(Key::Enter);
  R1_EXPECT(activated == 100);
  // Not selectable rows are skipped.
  NodeFlags locked;
  locked.selectable = false;
  big->setFlags(50, locked);
  tree.select(49);
  rig.t.ui.keyDown(Key::Down);
  R1_EXPECT(tree.selection() == ids({51}));
  R1_EXPECT(!tree.select(50));
  // An empty tree does not use the keys.
  rig.tree->setModel(nullptr);
  R1_EXPECT(!rig.t.ui.keyDown(Key::Down) && !rig.t.ui.keyDown(Key::A, Mod::kCtrl));
}

void testScrolling() {
  auto m = std::make_shared<SimpleTreeModel>();
  for (NodeId i = 1; i <= 200; ++i) m->add(kTreeRoot, i, "Row " + std::to_string(i));
  Rig rig(m);
  auto& tree = *rig.tree;
  R1_EXPECT(tree.maxScroll() == 200 * 24 - 240);
  rig.paint();
  R1_EXPECT(tree.verticalBarVisible());
  const layout::Rect a = rig.area();
  R1_EXPECT(tree.rowRect(0).w == 250);  // 10 px reserved for the scrollbar
  R1_EXPECT(rig.t.ui.wheel(a.x + 50, a.y + 50, 0, -2));
  R1_EXPECT(tree.scrollOffset() == 96.0);
  R1_EXPECT(rig.t.ui.wheel(a.x + 50, a.y + 50, 0, 1) && tree.scrollOffset() == 48.0);
  R1_EXPECT(rig.t.ui.wheel(a.x + 50, a.y + 50, 0, 100));
  R1_EXPECT(tree.scrollOffset() == 0.0 && !rig.t.ui.wheel(a.x + 50, a.y + 50, 0, 1));
  R1_EXPECT(tree.scrollToNode(100) && tree.scrollOffset() == 100 * 24 - 240);  // bottom aligned, the least movement
  R1_EXPECT(!tree.scrollToNode(100));  // already fully visible
  R1_EXPECT(tree.scrollToNode(10, true) && tree.scrollOffset() == 9 * 24 + 12 - 120);  // not visible: centred
  R1_EXPECT(!tree.scrollToNode(9999) && !tree.scrollTo(std::numeric_limits<double>::quiet_NaN()));
  R1_EXPECT(tree.scrollTo(1e300) && tree.scrollOffset() == tree.maxScroll());
  // Thumb drag: track at the right edge, 10 px wide.
  tree.scrollTo(0);
  rig.paint();
  const double tx = a.x + 260 - 5;
  rig.t.ui.pointerMove(tx, a.y + 5);
  rig.t.ui.pointerDown(tx, a.y + 5);
  rig.t.ui.pointerMove(tx, a.y + 60);
  R1_EXPECT(tree.scrollOffset() > 500.0);
  rig.t.ui.pointerMove(tx, a.y + 100000);
  R1_EXPECT(tree.scrollOffset() == tree.maxScroll());
  rig.t.ui.pointerUp(tx, a.y + 100000);
  // Resizing the view re-clamps the offset.
  tree.style().height = layout::Length::px(2000);
  tree.requestLayout();
  rig.t.layout();
  R1_EXPECT(tree.scrollOffset() == tree.maxScroll());
  // Auto height: the widget is as tall as its rows.
  Rig auto_(sampleModel(), 260, 0);
  auto_.tree->setAutoHeight(true);
  auto_.tree->style().height = layout::Length::autoValue();
  auto_.tree->requestLayout();
  auto_.tree->expandAll();
  auto_.t.layout();
  R1_EXPECT(auto_.area().h == 8 * 24);
}

void testActions() {
  auto m = sampleModel();
  NodeFlags hidden;
  hidden.hidden = true;
  m->setFlags(6, hidden);
  NodeFlags locked;
  locked.locked = true;
  m->setFlags(7, locked);
  Rig rig(m);
  auto& tree = *rig.tree;
  std::vector<std::pair<NodeId, RowAction>> actions;
  tree.setOnAction([&](NodeId n, RowAction a) { actions.emplace_back(n, a); });
  rig.paint();
  // Hover over a plain row shows the actions; clicking the eye reports it and does not select.
  const layout::Rect eye = tree.actionRect(0, RowAction::ToggleVisibility);
  rig.t.ui.pointerMove(rig.labelX(), rig.rowY(0));
  R1_EXPECT(tree.tooltipText().empty());
  rig.t.ui.pointerMove(eye.x + 8, eye.y + 8);
  R1_EXPECT(tree.tooltipText() == "Hide");
  rig.t.ui.pointerDown(eye.x + 8, eye.y + 8);
  rig.t.ui.pointerUp(eye.x + 8, eye.y + 8);
  R1_EXPECT(actions.size() == 1 && actions[0].first == 1 && actions[0].second == RowAction::ToggleVisibility && tree.selectionCount() == 0);
  const layout::Rect lock = tree.actionRect(0, RowAction::ToggleLock);
  rig.t.ui.pointerMove(lock.x + 8, lock.y + 8);
  R1_EXPECT(tree.tooltipText() == "Lock");
  rig.t.ui.pointerDown(lock.x + 8, lock.y + 8);
  rig.t.ui.pointerUp(lock.x + 8, lock.y + 8);
  R1_EXPECT(actions.size() == 2 && actions[1].second == RowAction::ToggleLock);
  // Press on an action, release elsewhere: no report.
  rig.t.ui.pointerDown(eye.x + 8, eye.y + 8);
  rig.t.ui.pointerMove(rig.labelX(), rig.rowY(1));
  rig.t.ui.pointerUp(rig.labelX(), rig.rowY(1));
  R1_EXPECT(actions.size() == 2);
  // Hidden rows show "Show"; their actions exist even without hover.
  const layout::Rect hiddenEye = tree.actionRect(1, RowAction::ToggleVisibility);
  rig.t.ui.pointerMove(hiddenEye.x + 8, hiddenEye.y + 8);
  R1_EXPECT(tree.tooltipText() == "Show");
  const layout::Rect lockedLock = tree.actionRect(2, RowAction::ToggleLock);
  rig.t.ui.pointerMove(lockedLock.x + 8, lockedLock.y + 8);
  R1_EXPECT(tree.tooltipText() == "Unlock");
  // A node without actions has none.
  NodeFlags none;
  none.hasActions = false;
  m->setFlags(1, none);
  rig.t.ui.pointerMove(eye.x + 8, eye.y + 8);
  R1_EXPECT(tree.tooltipText().empty());
  rig.paint();
}

void testRename() {
  Rig rig;
  auto& tree = *rig.tree;
  tree.expandAll();
  rig.t.layout();
  std::vector<std::pair<NodeId, std::string>> renames;
  tree.setOnRename([&](NodeId n, std::string_view text) {
    renames.emplace_back(n, std::string(text));
    if (text == "taken") return RenameResult{false, "That name is taken"};
    rig.m->setLabel(n, std::string(text));
    return RenameResult{};
  });
  rig.clickRow(1);  // select "Rectangle" (id 2)
  R1_EXPECT(rig.t.ui.keyDown(static_cast<Key>(113)));  // F2
  R1_EXPECT(tree.renaming() && tree.renameEditor().node() == 2 && tree.renameEditor().text() == "Rectangle");
  rig.t.layout();
  const layout::Rect field = tree.renameFieldRect();
  const layout::Rect row = tree.rowRect(1);
  R1_EXPECT(row.h == 26 && field.h == 18 && field.y == row.y + 4 && field.x == row.x + 52 && field.x + field.w == row.x + row.w);
  R1_EXPECT(tree.rowRect(2).y == tree.rowRect(1).y + 26);  // rows below move down 2 px
  rig.paint();
  // Typing replaces the selected text; Escape cancels.
  rig.t.ui.textInput('X');
  R1_EXPECT(tree.renameEditor().text() == "X");
  rig.t.ui.keyDown(Key::Escape);
  R1_EXPECT(!tree.renaming() && renames.empty() && rig.m->label(2) == "Rectangle" && tree.rowRect(2).y == tree.rowRect(1).y + 24);
  // Invalid names keep the field open with the error; the next edit clears it; a valid name commits.
  tree.beginRename(2);
  rig.t.ui.keyDown(Key::Backspace);
  rig.t.ui.keyDown(Key::Enter);  // empty name
  R1_EXPECT(tree.renaming() && !tree.renameEditor().error().empty() && renames.empty());
  for (const char c : std::string("taken")) rig.t.ui.textInput(static_cast<char32_t>(c));
  rig.t.ui.keyDown(Key::Enter);
  R1_EXPECT(tree.renaming() && tree.renameEditor().error() == "That name is taken" && renames.size() == 1);
  rig.t.ui.pointerMove(rig.labelX(), rig.rowY(1));
  R1_EXPECT(tree.tooltipText() == "That name is taken");
  rig.paint();
  rig.t.ui.keyDown(Key::A, Mod::kCtrl);
  for (const char c : std::string("Box")) rig.t.ui.textInput(static_cast<char32_t>(c));
  R1_EXPECT(tree.renameEditor().error().empty());
  rig.t.ui.keyDown(Key::Enter);
  R1_EXPECT(!tree.renaming() && rig.m->label(2) == "Box" && renames.back().second == "Box");
  // Committing an unchanged text does not call the handler.
  const size_t before = renames.size();
  tree.beginRename(2);
  rig.t.ui.keyDown(Key::Enter);
  R1_EXPECT(!tree.renaming() && renames.size() == before);
  // Slow click: a second click on the label of the selected row, 500 ms later, starts the rename.
  rig.autoTime = false;
  rig.t.ui.setTime(10000);
  tree.clearSelection();
  rig.clickRow(1);
  rig.t.ui.setTime(12000);
  rig.clickRow(1);  // already selected before this press: arms the timer
  rig.t.ui.setTime(12499);
  tree.advance(12499);
  R1_EXPECT(!tree.renaming());
  rig.t.ui.setTime(12500);
  tree.advance(12500);
  R1_EXPECT(tree.renaming() && tree.renameEditor().node() == 2);
  tree.cancelRename();
  // A double click within that time cancels it and runs the open action instead.
  NodeId activated = 0;
  tree.setOnActivate([&](NodeId n) { activated = n; });
  rig.t.ui.setTime(20000);
  rig.clickRow(1);
  rig.t.ui.setTime(20100);
  rig.t.ui.pointerDown(rig.labelX(), rig.rowY(1));  // the second click of a double click
  rig.t.ui.pointerUp(rig.labelX(), rig.rowY(1));
  tree.advance(21000);
  R1_EXPECT(!tree.renaming() && activated == 2);
  // Not renamable rows, unknown rows and focus loss.
  NodeFlags fixed;
  fixed.renamable = false;
  rig.m->setFlags(3, fixed);
  R1_EXPECT(!tree.beginRename(3) && !tree.beginRename(99));
  tree.beginRename(2);
  rig.t.ui.textInput('Z');
  rig.t.ui.router().clearFocus();  // focus loss commits a valid text (rule 80)
  R1_EXPECT(!tree.renaming() && rig.m->label(2) == "Z");
  tree.beginRename(2);
  rig.t.ui.keyDown(Key::Backspace);
  rig.t.ui.router().clearFocus();  // an invalid (empty) text is discarded
  R1_EXPECT(!tree.renaming() && rig.m->label(2) == "Z");
  // A click elsewhere commits too; a click in the field moves the caret instead.
  tree.beginRename(2);
  rig.t.layout();
  const layout::Rect f2 = tree.renameFieldRect();
  rig.t.ui.pointerMove(f2.x + 30, f2.y + 9);
  rig.t.ui.pointerDown(f2.x + 30, f2.y + 9);
  rig.t.ui.pointerUp(f2.x + 30, f2.y + 9);
  R1_EXPECT(tree.renaming());
  rig.clickRow(4);
  R1_EXPECT(!tree.renaming());
  // The renamed node disappears from the model: the editor closes.
  tree.beginRename(5);
  R1_EXPECT(tree.renaming());
  rig.m->remove(5);
  tree.refresh();
  R1_EXPECT(!tree.renaming());
  // A handler that destroys the tree while committing.
  Rig doomed;
  const WidgetId id = doomed.tree->id();
  doomed.tree->setOnRename([&](NodeId, std::string_view) {
    doomed.t.ui.destroy(id);
    return RenameResult{};
  });
  doomed.tree->beginRename(1);
  doomed.t.ui.textInput('Q');
  doomed.t.ui.keyDown(Key::Enter);
  R1_EXPECT(!doomed.t.ui.alive(id));
  doomed.t.layout();
}

void testDragAndDrop() {
  Rig rig;
  auto& tree = *rig.tree;
  tree.expandAll();
  rig.t.layout();
  // rows: 0 Frame(1),1 Rect(2),2 Rect(3),3 Frame(4),4 Rect(5),5 Ellipse(6),6 Group(7),7 Text(8)
  std::vector<DropRequest> drops;
  tree.setDropHandlers(nullptr, [&](const DropRequest& r) { drops.push_back(r); });
  const layout::Rect a = rig.area();
  const double x = rig.labelX();
  // Press on the Ellipse, move 5 px: no drag yet (strictly more than the threshold is needed).
  rig.t.ui.pointerMove(x, rig.rowY(5));
  rig.t.ui.pointerDown(x, rig.rowY(5));
  rig.t.ui.pointerMove(x, rig.rowY(5) + 5);
  R1_EXPECT(!tree.dragging());
  // Drag over the upper edge of the first row: above Frame.
  const double top0 = tree.rowRect(0).y;
  rig.t.ui.pointerMove(x, top0 + 2);
  R1_EXPECT(tree.dragging() && tree.dropPreview().valid == true && tree.dropPreview().target == 1 && tree.dropPreview().zone == DropZone::Above);
  rig.paint();
  rig.t.ui.pointerMove(x, top0 + 12);  // the middle: onto
  R1_EXPECT(tree.dropPreview().zone == DropZone::Onto && tree.dropPreview().target == 1);
  rig.t.ui.pointerMove(x, top0 + 22);  // the lower 6 px of a 24 px row: below
  R1_EXPECT(tree.dropPreview().zone == DropZone::Below);
  rig.t.ui.pointerMove(x, top0 + 24 - 6.5);
  R1_EXPECT(tree.dropPreview().zone == DropZone::Onto);  // the band is 6 px (a quarter of the row)
  rig.t.ui.pointerMove(x, top0 + 24 - 5.5);
  R1_EXPECT(tree.dropPreview().zone == DropZone::Below);
  // Dropping onto a row calls the handler and expands the target (rule 23).
  tree.setExpanded(1, false);
  rig.t.layout();
  rig.t.ui.pointerMove(x, tree.rowRect(0).y + 12);
  rig.t.ui.pointerUp(x, tree.rowRect(0).y + 12);
  R1_EXPECT(drops.size() == 1 && drops[0].nodes == std::vector<NodeId>({6}) && drops[0].target == 1 && drops[0].zone == DropZone::Onto);
  R1_EXPECT(tree.isExpanded(1) && !tree.dragging());
  // A drag cannot target its own subtree or itself.
  rig.t.layout();
  const NodeId frame = 1;
  tree.setSelection({frame});
  rig.t.ui.pointerMove(x, rig.rowY(0));
  rig.t.ui.pointerDown(x, rig.rowY(0));
  rig.t.ui.pointerMove(x, rig.rowY(0) + 30);  // over the first child
  R1_EXPECT(tree.dragging() && !tree.dropPreview().valid);
  rig.t.ui.pointerMove(x, rig.rowY(6));  // over "Group": allowed
  R1_EXPECT(tree.dropPreview().valid && tree.dropPreview().target == 7);
  // The payload of a multi selection drops descendants of other dragged nodes.
  rig.t.ui.keyDown(Key::Escape);  // not focused through keyboard yet: focus is on the tree after the press
  R1_EXPECT(!tree.dragging());
  rig.t.ui.pointerUp(x, rig.rowY(6));
  drops.clear();
  tree.expandAll();
  rig.t.layout();
  tree.setSelection({1, 2, 6});
  rig.t.ui.pointerMove(x, rig.rowY(5));
  rig.t.ui.pointerDown(x, rig.rowY(5));
  rig.t.ui.pointerMove(x, rig.rowY(6) + 2);
  R1_EXPECT(tree.dragging());
  rig.t.ui.pointerMove(x, rig.rowY(7) + 6);
  rig.t.ui.pointerUp(x, rig.rowY(7) + 6);
  R1_EXPECT(drops.size() == 1 && drops[0].nodes == std::vector<NodeId>({1, 6}));  // node 2 travels with its parent 1
  // The accept handler vetoes zones.
  drops.clear();
  tree.setDropHandlers([](const DropRequest& r) { return r.zone != DropZone::Onto; }, [&](const DropRequest& r) { drops.push_back(r); });
  tree.setSelection({6});
  rig.t.layout();
  rig.t.ui.pointerMove(x, rig.rowY(5));
  rig.t.ui.pointerDown(x, rig.rowY(5));
  rig.t.ui.pointerMove(x, rig.rowY(0));  // the middle of the row: onto, which the handler vetoes
  R1_EXPECT(tree.dragging() && !tree.dropPreview().valid);
  rig.t.ui.pointerMove(x, rig.rowY(0) - 10);
  R1_EXPECT(tree.dropPreview().valid && tree.dropPreview().zone == DropZone::Above);
  rig.t.ui.pointerMove(x, rig.rowY(0));
  rig.t.ui.pointerUp(x, rig.rowY(0));
  R1_EXPECT(drops.empty());  // dropped on a vetoed zone: nothing
  // Dragging an unselected row selects it first and drags it alone; a node that is not draggable never drags.
  NodeFlags still;
  still.draggable = false;
  rig.m->setFlags(8, still);
  tree.clearSelection();
  rig.t.ui.pointerMove(x, rig.rowY(7));
  rig.t.ui.pointerDown(x, rig.rowY(7));
  rig.t.ui.pointerMove(x, rig.rowY(7) - 30);
  R1_EXPECT(!tree.dragging() && tree.isSelected(8));
  rig.t.ui.pointerUp(x, rig.rowY(7) - 30);
  // Dropping below the last row targets the last row below; above the first row targets it above.
  tree.setDropHandlers(nullptr, [&](const DropRequest& r) { drops.push_back(r); });
  tree.setSelection({6});
  rig.t.ui.pointerMove(x, rig.rowY(5));
  rig.t.ui.pointerDown(x, rig.rowY(5));
  rig.t.ui.pointerMove(x, a.y + 235);
  R1_EXPECT(tree.dropPreview().valid && tree.dropPreview().target == 8 && tree.dropPreview().zone == DropZone::Below);
  rig.t.ui.pointerMove(x, a.y - 20);
  R1_EXPECT(tree.dropPreview().valid && tree.dropPreview().target == 1 && tree.dropPreview().zone == DropZone::Above);
  rig.paint();
  rig.t.ui.pointerUp(x, a.y - 20);
  R1_EXPECT(drops.size() == 1 && drops[0].zone == DropZone::Above);
  // A model change during the drag cancels it.
  rig.t.layout();
  tree.setSelection({6});
  rig.t.ui.pointerMove(x, rig.rowY(5));
  rig.t.ui.pointerDown(x, rig.rowY(5));
  rig.t.ui.pointerMove(x, rig.rowY(5) - 40);
  R1_EXPECT(tree.dragging());
  rig.m->add(kTreeRoot, 50, "Late");
  tree.refresh();
  R1_EXPECT(!tree.dragging());
  rig.t.ui.pointerUp(x, rig.rowY(5) - 40);
  // Disabled in the middle of a drag.
  rig.t.layout();
  tree.setSelection({6});
  rig.t.ui.pointerMove(x, rig.rowY(5));
  rig.t.ui.pointerDown(x, rig.rowY(5));
  rig.t.ui.pointerMove(x, rig.rowY(5) - 40);
  R1_EXPECT(tree.dragging());
  tree.setEnabled(false);
  R1_EXPECT(!tree.dragging());
  rig.t.ui.pointerUp(x, rig.rowY(5) - 40);
  tree.setEnabled(true);
}

void testAutoScroll() {
  auto m = std::make_shared<SimpleTreeModel>();
  for (NodeId i = 1; i <= 300; ++i) m->add(kTreeRoot, i, "Row " + std::to_string(i));
  Rig rig(m);
  auto& tree = *rig.tree;
  tree.setDropHandlers(nullptr, [](const DropRequest&) {});
  tree.select(3);
  const layout::Rect a = rig.area();
  const double x = rig.labelX();
  rig.t.ui.pointerMove(x, rig.rowY(2));
  rig.t.ui.pointerDown(x, rig.rowY(2));
  rig.t.ui.pointerMove(x, a.y + 100);
  R1_EXPECT(tree.dragging());
  tree.advance(1000);
  tree.advance(1010);
  R1_EXPECT(tree.scrollOffset() == 0.0);  // in the middle: no scrolling
  rig.t.ui.pointerMove(x, a.y + 240);     // at the bottom edge
  tree.advance(1020);
  tree.advance(1070);                     // 50 ms at 600 px/s = 30 px
  R1_EXPECT(std::abs(tree.scrollOffset() - 36.0) < 1.5);  // 10 ms + 50 ms at 600 px/s
  tree.advance(2000);                     // a long gap counts as 50 ms
  R1_EXPECT(std::abs(tree.scrollOffset() - 66.0) < 3.0);
  rig.t.ui.pointerMove(x, a.y + 240 - 15);  // half way into the zone: half speed
  const double mid = tree.scrollOffset();
  tree.advance(2010);
  tree.advance(2060);
  R1_EXPECT(std::abs((tree.scrollOffset() - mid) - 18.0) < 2.0);  // 300 px/s over 60 ms
  rig.t.ui.pointerMove(x, a.y + 100);
  const double settled = tree.scrollOffset();
  tree.advance(2200);
  R1_EXPECT(tree.scrollOffset() == settled);
  rig.t.ui.pointerMove(x, a.y - 10);       // beyond the top edge scrolls up
  tree.advance(2210);
  tree.advance(2260);
  R1_EXPECT(tree.scrollOffset() < settled);
  rig.t.ui.pointerUp(x, a.y - 10);
  rig.paint();
}

void testTypeAheadAndPersistence() {
  Rig rig;
  auto& tree = *rig.tree;
  tree.expandAll();
  rig.t.layout();
  rig.t.ui.router().focus(tree.id(), r1ui::core::events::FocusReason::Keyboard);
  rig.t.ui.setTime(100);
  R1_EXPECT(rig.t.ui.textInput('r') && tree.selection() == ids({2}));  // the first "Rectangle"
  rig.t.ui.setTime(300);
  rig.t.ui.textInput('e');
  R1_EXPECT(tree.selection() == ids({2}));  // already matches "re": stays
  rig.t.ui.setTime(400);
  rig.t.ui.textInput('c');
  rig.t.ui.textInput('t');
  R1_EXPECT(tree.selection() == ids({2}));
  rig.t.ui.setTime(5000);  // the prefix resets after 2 s
  rig.t.ui.textInput('E');
  R1_EXPECT(tree.selection() == ids({6}));
  rig.t.ui.setTime(5100);
  rig.t.ui.textInput('x');
  R1_EXPECT(tree.selection() == ids({6}));  // "Ex" matches nothing: unchanged
  rig.t.ui.setTime(9000);
  rig.t.ui.textInput('f');  // wraps around from the cursor to the first Frame
  R1_EXPECT(tree.selection() == ids({1}));
  R1_EXPECT(rig.t.ui.textInput('z') && tree.selection() == ids({1}));  // no match: unchanged
  R1_EXPECT(!rig.t.ui.textInput('a', Mod::kCtrl));
  rig.paint();

  // Selection persistence: removed nodes drop out with one callback, survivors stay.
  tree.setSelection({2, 3, 6, 8});
  rig.selectionEvents = 0;
  rig.m->remove(6);
  rig.m->remove(3);
  tree.refresh();
  R1_EXPECT(tree.selection() == ids({2, 8}) && rig.selectionEvents == 1);
  tree.refresh();
  R1_EXPECT(rig.selectionEvents == 1);  // nothing changed: no callback
  // Selected nodes inside a collapsed branch stay selected (rule 74) and are listed after the visible ones.
  tree.collapseAll();
  R1_EXPECT(tree.selectionCount() == 2 && tree.selection() == ids({2, 8}));
  // Reordering keeps the selection (ids, not positions).
  rig.m->move(8, kTreeRoot, 0);
  R1_EXPECT(tree.selection() == ids({8, 2}) || tree.selection() == ids({2, 8}));
  R1_EXPECT(tree.isSelected(8) && tree.isSelected(2));
  // A completely new model clears everything.
  tree.setModel(sampleModel());
  R1_EXPECT(tree.selectionCount() == 0 && tree.cursorNode() == kTreeRoot && tree.scrollOffset() == 0.0);
  // Cursor and anchor that vanish are forgotten.
  tree.expandAll();
  tree.select(5);
  tree.model();
  auto m2 = std::dynamic_pointer_cast<SimpleTreeModel>(tree.model());
  m2->remove(4);
  tree.refresh();
  R1_EXPECT(tree.cursorNode() == kTreeRoot && tree.anchorNode() == kTreeRoot && tree.selectionCount() == 0);
}

void testListAppearanceAndHostile() {
  auto m = std::make_shared<SimpleTreeModel>();
  for (NodeId i = 1; i <= 5; ++i) m->add(kTreeRoot, i, i == 3 ? std::string("\xFF\xFE bad \xC3") : "Item " + std::to_string(i), "file");
  Rig rig(m);
  rig.tree->setAppearance(TreeAppearance::List);
  R1_EXPECT(rig.tree->disclosureRect(0).w == 0);
  rig.paint();
  // Hostile geometry and input.
  rig.tree->style().width = layout::Length::px(0);
  rig.tree->style().height = layout::Length::px(0);
  rig.tree->requestLayout();
  rig.t.layout();
  rig.paint();
  R1_EXPECT(rig.tree->scrollOffset() >= 0.0);
  rig.tree->style().width = layout::Length::px(260);
  rig.tree->style().height = layout::Length::px(100);
  rig.tree->requestLayout();
  rig.t.layout();
  rig.t.ui.wheel(std::nan(""), 5, 0, -1);
  rig.t.ui.wheel(rig.area().x + 5, rig.area().y + 5, std::nan(""), std::nan(""));
  rig.t.ui.pointerMove(rig.area().x + 5, rig.area().y + 5);
  rig.t.ui.pointerLeftWindow();
  // Tooltip for a truncated label shows the full text.
  rig.tree->style().width = layout::Length::px(60);
  rig.tree->requestLayout();
  rig.t.layout();
  rig.t.ui.pointerMove(rig.area().x + 3, rig.rowY(0));
  R1_EXPECT(rig.tree->tooltipText() == "Item 1");
  rig.tree->style().width = layout::Length::px(260);
  rig.tree->requestLayout();
  rig.t.layout();
  rig.t.ui.pointerMove(rig.area().x + 3, rig.rowY(0));
  R1_EXPECT(rig.tree->tooltipText().empty());
  // Destroying the tree from the selection callback.
  Rig doomed;
  const WidgetId id = doomed.tree->id();
  doomed.tree->setOnSelectionChanged([&](TreeView&) { doomed.t.ui.destroy(id); });
  doomed.clickRow(0);
  R1_EXPECT(!doomed.t.ui.alive(id));
  doomed.t.layout();
  doomed.t.ui.pointerMove(10, 10);
  doomed.t.ui.keyDown(Key::Down);
}

}  // namespace

int main() try {
  testModel();
  testRowsAndExpansion();
  testPointerSelection();
  testKeyboardSelection();
  testScrolling();
  testActions();
  testRename();
  testDragAndDrop();
  testAutoScroll();
  testTypeAheadAndPersistence();
  testListAppearanceAndHostile();
  return r1test::finish();
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}
