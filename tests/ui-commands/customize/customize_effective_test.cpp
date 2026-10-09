// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of effectiveLayout (customize/Delta.h): identity for an empty delta, hide, rename,
//   moves with every placement, locked menus, missing commands, anchor loss, the product-update
//   simulation (built-in layout v1 -> v2 keeps the user's changes), user nodes, hostile deltas (cycles,
//   illegal parents, duplicate and unknown ids, depth and node limits) and property-style fuzzing
//   (the result is always a legal layout; hide then show is the identity; the relative order of
//   untouched nodes is preserved).
// Callers: CTest (label fast).
#include <random>

#include "CustomizeFixtures.h"

namespace {

using namespace r1test;
using cz::ReportEntry;

cz::EffectiveResult eff(const cz::LayoutSet& b, const cz::Delta& d, bool keepHidden = false, cz::CommandExists exists = {}) {
  cz::EffectiveOptions o;
  o.keepHidden = keepHidden;
  o.exists = std::move(exists);
  return cz::effectiveLayout(b, d, o);
}

void testIdentity() {
  const cz::LayoutSet b = builtinV1();
  R1_EXPECT(cz::validateLayout(b).empty());
  const cz::EffectiveResult r = eff(b, {});
  R1_EXPECT(r.report.clean());
  R1_EXPECT(sectionIds(r.layout, "edit.clip") == "edit.cut,edit.copy,edit.paste,edit.more");
  R1_EXPECT(cz::countNodes(r.layout) == cz::countNodes(b));
  R1_EXPECT(r.layout.menuBar.menus[3].locked && r.layout.menuBar.menus[3].children[0].locked);  // the lock reaches the subtree
  R1_EXPECT(r.layout.toolbars.size() == 1 && r.layout.toolbars[0].gap == cz::kDefaultToolbarGap && r.layout.toolbars[0].sizeStep == cz::SizeStep::Medium);
  R1_EXPECT(cz::validateLayout(r.layout).empty());
}

void testHideRenameMove() {
  const cz::LayoutSet b = builtinV1();
  cz::Delta d;
  d.edits["edit.copy"].hidden = true;
  d.edits["menu.view"].label = "Display";
  d.edits["edit.cut"].label = "Cut it";
  cz::EffectiveResult r = eff(b, d);
  R1_EXPECT(r.report.clean());
  R1_EXPECT(sectionIds(r.layout, "edit.clip") == "edit.cut,edit.paste,edit.more");  // hidden entry dropped
  R1_EXPECT(nodeNamed(r.layout, "menu.view")->shownLabel() == "Display" && nodeNamed(r.layout, "menu.view")->label == "View");
  R1_EXPECT(nodeNamed(r.layout, "edit.cut")->shownLabel() == "Cut it");
  r = eff(b, d, true);
  R1_EXPECT(sectionIds(r.layout, "edit.clip") == "edit.cut,edit.copy,edit.paste,edit.more" && !nodeNamed(r.layout, "edit.copy")->visible);

  // Every placement kind.
  const auto order = [&](cz::Placement to) {
    cz::Delta m;
    m.moves.push_back({"edit.paste", std::move(to)});
    return sectionIds(eff(b, m).layout, "edit.clip");
  };
  R1_EXPECT(order({"edit.clip", "", cz::Side::Start}) == "edit.paste,edit.cut,edit.copy,edit.more");
  R1_EXPECT(order({"edit.clip", "edit.cut", cz::Side::Before}) == "edit.paste,edit.cut,edit.copy,edit.more");
  R1_EXPECT(order({"edit.clip", "edit.cut", cz::Side::After}) == "edit.cut,edit.paste,edit.copy,edit.more");
  R1_EXPECT(order({"edit.clip", "", cz::Side::End}) == "edit.cut,edit.copy,edit.more,edit.paste");
  // Across sections and menus; a section dragged to another menu with its entries.
  cz::Delta m;
  m.moves.push_back({"edit.paste", {"file.main", "file.save", cz::Side::Before}});
  m.moves.push_back({"edit.clip", {"menu.file", "file.main", cz::Side::After}});
  const cz::EffectiveResult moved = eff(b, m);
  R1_EXPECT(moved.report.clean());
  R1_EXPECT(sectionIds(moved.layout, "file.main") == "file.open,edit.paste,file.save");
  R1_EXPECT(join(childIds(nodeNamed(moved.layout, "menu.file")->children)) == "file.main,edit.clip");
  R1_EXPECT(sectionIds(moved.layout, "edit.clip") == "edit.cut,edit.copy,edit.more");
  R1_EXPECT(cz::validateLayout(moved.layout).empty());
}

void testLockedAndIllegal() {
  const cz::LayoutSet b = builtinV1();
  cz::Delta d;
  d.edits["help.about"].hidden = true;      // inside the locked Help menu
  d.edits["menu.help"].label = "Aide";
  d.moves.push_back({"edit.undo", {"help.main", "", cz::Side::End}});   // into a locked menu
  d.moves.push_back({"help.about", {"file.main", "", cz::Side::End}});  // out of a locked menu
  const cz::EffectiveResult r = eff(b, d);
  R1_EXPECT(r.report.count(ReportEntry::Code::Locked) == 4);
  R1_EXPECT(sectionIds(r.layout, "help.main") == "help.about" && nodeNamed(r.layout, "menu.help")->shownLabel() == "Help");
  R1_EXPECT(sectionIds(r.layout, "edit.history") == "edit.undo,edit.redo");

  cz::Delta bad;
  bad.moves.push_back({"edit.more", {"edit.more.main", "", cz::Side::End}});         // a node into its own subtree
  bad.moves.push_back({"edit.clip", {"edit.more", "", cz::Side::End}});              // a section into a sub-menu inside it
  bad.moves.push_back({"file.open", {"menu.file", "", cz::Side::End}});              // a command directly in a menu
  bad.moves.push_back({"tb.undo", {"file.main", "", cz::Side::End}});                // toolbar item into a menu
  bad.moves.push_back({"edit.undo", {"nowhere", "", cz::Side::End}});                // unknown parent
  bad.moves.push_back({"ghost", {"file.main", "", cz::Side::End}});                  // unknown node
  bad.moves.push_back({"menubar", {"file.main", "", cz::Side::End}});                // a container
  const cz::EffectiveResult rb = eff(b, bad);
  R1_EXPECT(rb.report.count(ReportEntry::Code::Cycle) == 2);
  R1_EXPECT(rb.report.count(ReportEntry::Code::IllegalParent) == 3);
  R1_EXPECT(rb.report.count(ReportEntry::Code::MissingParent) == 1 && rb.report.count(ReportEntry::Code::MissingNode) == 1);
  R1_EXPECT(cz::countNodes(rb.layout) == cz::countNodes(b) && sectionIds(rb.layout, "file.main") == "file.open,file.save");
  R1_EXPECT(cz::validateLayout(rb.layout).empty());
}

void testMissingCommandsAndAnchors() {
  const cz::LayoutSet b = builtinV1();
  const auto exists = [](const std::string& c) { return c != "edit.copy" && c != "tool.rect" && c != "tool.ellipse"; };
  cz::EffectiveResult r = eff(b, {}, false, exists);
  R1_EXPECT(r.report.count(ReportEntry::Code::MissingCommand) == 3);
  R1_EXPECT(sectionIds(r.layout, "edit.clip") == "edit.cut,edit.paste,edit.more");
  R1_EXPECT(nodeNamed(r.layout, "tb.shapes") == nullptr);  // a group without commands is dropped
  cz::EffectiveOptions keep;
  keep.keepMissing = true;
  keep.exists = exists;
  r = cz::effectiveLayout(b, {}, keep);
  R1_EXPECT(nodeNamed(r.layout, "edit.copy")->missing && sectionIds(r.layout, "edit.clip") == "edit.cut,edit.copy,edit.paste,edit.more");

  // An anchor that no longer exists: the node goes to the end of the same parent and it is reported.
  cz::Delta d;
  d.moves.push_back({"file.open", {"edit.clip", "was.removed", cz::Side::Before}});
  r = eff(b, d);
  R1_EXPECT(r.report.has(ReportEntry::Code::MissingAnchor, "file.open"));
  R1_EXPECT(sectionIds(r.layout, "edit.clip") == "edit.cut,edit.copy,edit.paste,edit.more,file.open");
}

void testProductUpdate() {
  // The user hid edit.copy, moved edit.paste to the top of the clipboard section, renamed Edit, moved
  // view.rulers before edit.cut (another menu) and added a user menu with two entries.
  cz::Delta d;
  d.edits["edit.copy"].hidden = true;
  d.edits["menu.edit"].label = "Editing";
  d.moves.push_back({"edit.paste", {"edit.clip", "", cz::Side::Start}});
  d.moves.push_back({"view.rulers", {"edit.clip", "edit.cut", cz::Side::Before}});
  cz::Node menu = cz::Node::menu("um1", "Mine");
  menu.user = true;
  cz::Node section = cz::Node::section("us2", "");
  section.user = true;
  cz::Node mine = cmdNode("u3", "edit.redo");
  mine.user = true;
  d.added.push_back({menu, {"menubar", "", cz::Side::End}});
  d.added.push_back({section, {"um1", "", cz::Side::End}});
  d.added.push_back({mine, {"us2", "", cz::Side::End}});
  d.serial = 3;

  const cz::LayoutSet v1 = builtinV1();
  const cz::EffectiveResult r1 = eff(v1, d);
  R1_EXPECT(r1.report.clean());
  R1_EXPECT(sectionIds(r1.layout, "edit.clip") == "edit.paste,view.rulers,edit.cut,edit.more");

  // v2: copy now before cut (hidden anyway), redo removed (the user's menu entry refers to it), select all
  // added after paste, view gets zoom.
  const cz::LayoutSet v2 = builtinV2();
  const cz::EffectiveResult r2 = eff(v2, d, false, [](const std::string& c) { return c != "edit.redo"; });
  R1_EXPECT(sectionIds(r2.layout, "edit.clip") == "edit.paste,view.rulers,edit.cut,edit.selectAll,edit.more");  // new entry at its default place
  R1_EXPECT(sectionIds(r2.layout, "view.main") == "view.grid,view.zoom");  // rulers left, zoom is new
  R1_EXPECT(sectionIds(r2.layout, "edit.history") == "edit.undo");           // redo is gone from the built-in
  R1_EXPECT(r2.report.has(ReportEntry::Code::MissingCommand, "u3"));         // the user's own reference shows as missing
  R1_EXPECT(sectionIds(r2.layout, "us2").empty());                           // ... and is dropped from the effective tree
  R1_EXPECT(nodeNamed(r2.layout, "menu.edit")->shownLabel() == "Editing");   // the rename survives
  R1_EXPECT(r2.layout.menuBar.menus.back().id == "um1");                     // the user menu survives, last
  R1_EXPECT(!nodeNamed(eff(v2, d, true).layout, "edit.copy")->visible);      // the hide still applies to the reordered entry
  R1_EXPECT(r2.layout.toolbars[0].items.size() == 6 && cz::validateLayout(r2.layout).empty());

  // A built-in entry the user moved is removed by the update: the move is reported, nothing else changes.
  cz::LayoutSet v3 = builtinV1();
  auto& view = v3.menuBar.menus[2].children[0].children;
  view.erase(std::remove_if(view.begin(), view.end(), [](const cz::Node& n) { return n.id == "view.rulers"; }), view.end());
  const cz::EffectiveResult r3 = eff(v3, d);
  R1_EXPECT(r3.report.has(ReportEntry::Code::MissingNode, "view.rulers"));
  R1_EXPECT(sectionIds(r3.layout, "edit.clip") == "edit.paste,edit.cut,edit.more");
}

void testSettingsAndContainers() {
  const cz::LayoutSet b = builtinV1();
  cz::Delta d;
  d.toolbarEdits["tb.main"].sizeStep = cz::SizeStep::Large;
  d.toolbarEdits["tb.main"].gap = 6;
  d.panelEdits["fp.main"].snap = true;
  d.edits["fp.undo"].rect = cz::Rect{16, 16, 80, 40};
  d.toolbarEdits["ghost"].gap = 4;
  d.toolbarEdits["tb.main"].gap = 100;  // out of range is reported and ignored
  cz::ToolbarLayout user;
  user.id = "ut9";
  user.title = "Mine";
  user.user = true;
  d.userToolbars.push_back(user);
  d.added.push_back({cmdNode("u10", "tool.select"), {"ut9", "", cz::Side::End}});
  d.added.push_back({cmdNode("u11", "tool.select"), {"tb.main", "tb.sep1", cz::Side::After}});
  const cz::EffectiveResult r = eff(b, d);
  R1_EXPECT(r.layout.toolbars[0].sizeStep == cz::SizeStep::Large && r.layout.toolbars[0].gap == cz::kDefaultToolbarGap);
  R1_EXPECT(r.report.has(ReportEntry::Code::Invalid, "tb.main") && r.report.has(ReportEntry::Code::MissingNode, "ghost"));
  R1_EXPECT(r.layout.panels[0].snap && nodeNamed(r.layout, "fp.undo")->rect == (cz::Rect{16, 16, 80, 40}));
  R1_EXPECT(r.layout.toolbars.size() == 2 && r.layout.toolbars[1].id == "ut9" && r.layout.toolbars[1].items.size() == 1);
  R1_EXPECT(join(childIds(r.layout.toolbars[0].items)) == "tb.select,tb.pen,tb.sep1,u11,tb.undo,tb.shapes");
  R1_EXPECT(cz::validateLayout(r.layout).empty());

  // Ids that collide with built-in ids are skipped.
  cz::Delta clash;
  clash.added.push_back({cmdNode("file.open", "x"), {"file.main", "", cz::Side::End}});
  clash.userToolbars.push_back([] { cz::ToolbarLayout t; t.id = "tb.main"; return t; }());
  const cz::EffectiveResult rc = eff(b, clash);
  R1_EXPECT(rc.report.count(ReportEntry::Code::DuplicateId) == 2 && cz::countNodes(rc.layout) == cz::countNodes(b));
}

void testLimits() {
  // Depth: a chain of sub-menus is cut at kMaxDepth and the layout stays legal.
  cz::LayoutSet b;
  cz::Node menu = cz::Node::menu("m", "M", {cz::Node::section("s0", "")});
  b.menuBar.menus.push_back(menu);
  cz::Delta d;
  std::string section = "s0";
  for (int i = 0; i < 40; ++i) {
    const std::string sub = "sub" + std::to_string(i), next = "sec" + std::to_string(i);
    d.added.push_back({cz::Node::submenu(sub, "S"), {section, "", cz::Side::End}});
    d.added.push_back({cz::Node::section(next, ""), {sub, "", cz::Side::End}});
    section = next;
  }
  const cz::EffectiveResult r = eff(b, d);
  R1_EXPECT(r.report.count(ReportEntry::Code::Limit) > 0 && cz::validateLayout(r.layout).empty());

  // Node count: a built-in set beyond the limit is cut, quickly.
  cz::LayoutSet big;
  cz::Node bigSection = cz::Node::section("bs", "");
  for (size_t i = 0; i < cz::kMaxNodes + 500; ++i) bigSection.children.push_back(cz::Node::separator("n" + std::to_string(i)));
  big.menuBar.menus.push_back(cz::Node::menu("bm", "B", {std::move(bigSection)}));
  const cz::EffectiveResult rb = eff(big, {});
  R1_EXPECT(cz::countNodes(rb.layout) == cz::kMaxNodes && rb.report.count(ReportEntry::Code::Limit) > 0);
}

// ---- property tests -----------------------------------------------------------------------------

std::vector<std::string> allIds(const cz::LayoutSet& set) {
  std::vector<std::string> out;
  const auto walk = [&](const auto& self, const std::vector<cz::Node>& nodes) -> void {
    for (const cz::Node& n : nodes) {
      out.push_back(n.id);
      self(self, n.children);
    }
  };
  walk(walk, set.menuBar.menus);
  for (const cz::ToolbarLayout& t : set.toolbars) walk(walk, t.items);
  for (const cz::FreeFormPanelLayout& p : set.panels) walk(walk, p.buttons);
  return out;
}

void testPropertyFuzz() {
  const cz::LayoutSet b = builtinV1();
  std::vector<std::string> ids = allIds(b);
  for (const char* extra : {"menubar", "tb.main", "fp.main", "ghost", "um1", "u2"}) ids.push_back(extra);
  const cz::Side sides[] = {cz::Side::End, cz::Side::Start, cz::Side::Before, cz::Side::After};
  for (uint32_t seed = 1; seed <= 400; ++seed) {
    std::mt19937 rng(seed);
    const auto pick = [&] { return ids[rng() % ids.size()]; };
    cz::Delta d;
    const int ops = static_cast<int>(rng() % 25);
    for (int i = 0; i < ops; ++i) {
      switch (rng() % 6) {
        case 0: d.edits[pick()].hidden = (rng() & 1) != 0; break;
        case 1: d.edits[pick()].label = "L" + std::to_string(rng() % 9); break;
        case 2: d.moves.push_back({pick(), {pick(), pick(), sides[rng() % 4]}}); break;
        case 3: {
          const cz::Kind kinds[] = {cz::Kind::Command, cz::Kind::Section, cz::Kind::Menu, cz::Kind::Submenu, cz::Kind::Separator, cz::Kind::Group, cz::Kind::FreeButton};
          cz::Node n = cz::Node::command("g" + std::to_string(i), "x");
          n.kind = kinds[rng() % 7];
          n.rect = {0, 0, 32, 32};
          d.added.push_back({n, {pick(), pick(), sides[rng() % 4]}});
          ids.push_back(n.id);
          break;
        }
        case 4: d.edits[pick()].rect = cz::Rect{double(rng() % 500), double(rng() % 500), double(rng() % 100), double(rng() % 100)}; break;
        default: d.toolbarEdits[(rng() & 1) ? "tb.main" : "x"].gap = double(rng() % 50); break;
      }
    }
    for (const bool keep : {false, true}) {
      const cz::EffectiveResult r = eff(b, d, keep);
      const std::vector<std::string> problems = cz::validateLayout(r.layout);
      if (!problems.empty()) std::fprintf(stderr, "seed %u: %s\n", seed, problems.front().c_str());
      R1_EXPECT(problems.empty());
      std::vector<std::string> out = allIds(r.layout);
      std::sort(out.begin(), out.end());
      R1_EXPECT(std::adjacent_find(out.begin(), out.end()) == out.end());  // ids stay unique
    }
  }

  // Hide then show is the identity; hiding never changes the order of the other nodes.
  for (const std::string& id : allIds(b)) {
    if (nodeNamed(b, id)->locked) continue;
    cz::Delta hide;
    hide.edits[id].hidden = true;
    const cz::EffectiveResult shown = eff(b, hide, true);
    R1_EXPECT(allIds(shown.layout) == allIds(b));
    cz::Delta restored = hide;
    restored.edits[id].hidden = false;
    R1_EXPECT(allIds(eff(b, restored).layout) == allIds(b));
  }

  // Moves keep the relative order of every node that was not moved.
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    std::mt19937 rng(seed * 7919u);
    const std::vector<std::string> before = allIds(b);
    cz::Delta d;
    const std::string moved = before[rng() % before.size()];
    d.moves.push_back({moved, {"edit.clip", "", cz::Side::End}});
    const cz::EffectiveResult r = eff(b, d);
    if (!r.report.clean()) continue;  // an illegal request is skipped, nothing to compare
    std::vector<std::string> a = allIds(b), c = allIds(r.layout);
    const cz::Node* movedNode = nodeNamed(b, moved);
    std::vector<std::string> subtree;
    const auto collect = [&](const auto& self, const cz::Node& n) -> void {
      subtree.push_back(n.id);
      for (const cz::Node& k : n.children) self(self, k);
    };
    collect(collect, *movedNode);
    const auto strip = [&](std::vector<std::string>& v) {
      v.erase(std::remove_if(v.begin(), v.end(), [&](const std::string& s) { return std::find(subtree.begin(), subtree.end(), s) != subtree.end(); }), v.end());
    };
    strip(a);
    strip(c);
    R1_EXPECT(a == c);
  }
}

}  // namespace

int main() {
  testIdentity();
  testHideRenameMove();
  testLockedAndIllegal();
  testMissingCommandsAndAnchors();
  testProductUpdate();
  testSettingsAndContainers();
  testLimits();
  testPropertyFuzz();
  return r1test::finish();
}
