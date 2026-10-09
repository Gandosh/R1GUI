// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for layout persistence (spec 04) and the file boundary: round trips, reset,
//   panels the host no longer has, and a battery of hostile layouts that must be rejected with
//   an error while the caller's layout stays untouched.
// Callers: CTest (label fast). Case names follow the spec's scenario numbers.
#include <string>

#include "TestSupport.h"

using namespace dock_test;

namespace {

// The sandbox's shape: left 2 tabs, centre, right 3 tabs, bottom 2 tabs (9 panels).
Node defaultTree() {
  return Node::split(Axis::Column,
                     {Node::split(Axis::Row, {Node::stack({1, 2}, 0, 1.0), Node::stack({3}, 0, 3.0), Node::stack({4, 5, 6}, 0, 1.2)}, 3.0),
                      Node::stack({7, 8}, 0, 1.0)});
}

DockLayout defaultLayout() { return build(9, defaultTree()); }

LoadResult load(const std::string& text, PanelId panelCount = 9) { return DockLayout::fromJson(text, makePanels(panelCount)); }

void expectRejected(const std::string& text, const char* what) {
  const DockLayout mine = defaultLayout();
  const LoadResult r = load(text);
  expect(!r.ok() && !r.error.empty(), what);
  expect(mine == defaultLayout(), "caller's layout is untouched by a rejected load");
}

std::string stackJson(const std::string& tabs, const std::string& weight = "1", const std::string& active = "0") {
  return R"({"type":"stack","weight":)" + weight + R"(,"tabs":[)" + tabs + R"(],"active":)" + active + "}";
}

std::string docWith(const std::string& mainRoot, const std::string& floating = "", const std::string& version = "1") {
  return R"({"version":)" + version + R"(,"main":{"root":)" + mainRoot + R"(},"floating":[)" + floating + "]}";
}

// Scenario 1 (partial): the host can build its default arrangement; reset = rebuild it.
void layouts_scenario_01_default_and_reset() {
  DockLayout dock = defaultLayout();
  const DockLayout fresh = defaultLayout();
  expect(dock.validate().ok && dock == fresh, "default layout valid and deterministic");
  dock.closePanel(3);
  dock.closePanel(7);
  expect(!(dock == fresh), "edits change the layout");
  dock = defaultLayout();
  expect(dock == fresh, "reset rebuilds the default exactly");
}

// Round trip: serialise, deserialise, equal; serialising again is byte-identical.
void layouts_round_trip_equality() {
  DockLayout dock = defaultLayout();
  DropZone zone;
  zone.preview = {123.5, 45.25, 333.125, 222.0625};
  expect(dock.dock(2, zone).ok, "float a panel");
  zone.preview = {-300, 20, 200, 100};
  expect(dock.dock(5, zone).ok, "float another (negative x is legal)");
  expect(dock.closePanel(8).ok && dock.activateTab(6).ok, "close and activate");
  const std::string json = dock.toJson();
  const LoadResult loaded = load(json);
  expect(loaded.ok() && loaded.droppedPanels == 0, "loads");
  expect(loaded.ok() && *loaded.layout == dock, "round trip equals the original");
  expect(loaded.ok() && loaded.layout->toJson() == json, "second serialisation is byte-identical");
  expect(json.find("\"version\":2") != std::string::npos, "versioned (schema 2)");
}

// Scenario 3: a floating panel keeps its rectangle and front tab across save/load.
void layouts_scenario_03_floating_round_trip() {
  DockLayout dock = build(3, Node::stack({1}));
  DropZone zone;
  zone.preview = {40.5, 80.25, 500, 300};
  dock.dock(2, zone);
  dock.dock(3, DropZone{DropKind::JoinStack, 1, 2, Side::Left, 0, {}});
  expect(dock.areas()[1].root->tabs == std::vector<PanelId>({3, 2}) && dock.areas()[1].root->active == 0, "setup: floating stack [3,2] front 3");
  const LoadResult loaded = load(dock.toJson(), 3);
  expect(loaded.ok() && loaded.layout->areas().size() == 2, "floating area restored");
  expect(loaded.layout->areas()[1].rect == Rect({40.5, 80.25, 500, 300}), "same position and size");
  expect(loaded.layout->areas()[1].root->active == 0 && loaded.layout->areas()[1].root->tabs[0] == 3, "same front tab");
}

// Scenario 4 (partial): a closed panel stays closed after reload and can be docked again.
void layouts_scenario_04_closed_stays_closed() {
  DockLayout dock = defaultLayout();
  dock.closePanel(7);
  const LoadResult loaded = load(dock.toJson());
  expect(loaded.ok() && !loaded.layout->isDocked(7) && loaded.layout->isDocked(8), "closed panel not shown after reload");
  DockLayout again = std::move(*loaded.layout);
  const LayoutResult layout = again.computeLayout({0, 0, 1200, 800});
  DropZone join = again.hitTestDropZone(layout, DragQuery{{20, 10}, 7, {}});
  expect(again.dock(7, join).ok && again.isDocked(7), "reopening docks it again (old slot is not remembered: not covered)");
}

// Scenario 6: loading yields the stored arrangement regardless of the current one (host swaps it in).
void layouts_scenario_06_load_replaces() {
  DockLayout saved = defaultLayout();
  saved.closePanel(4);
  const std::string json = saved.toJson();
  DockLayout current = defaultLayout();
  current.closePanel(1);
  LoadResult r = load(json);
  expect(r.ok(), "loads");
  current = std::move(*r.layout);
  expect(current == saved, "current layout replaced by the loaded one");
}

// Scenario 10: a floating area holding only an unknown panel produces no window.
void layouts_scenario_10_unknown_only_floating() {
  const std::string json = docWith(stackJson("1,2"), R"({"rect":{"x":10,"y":10,"w":300,"h":200},"root":)" + stackJson("77") + "}");
  const LoadResult r = load(json, 3);
  expect(r.ok() && r.layout->areas().size() == 1 && r.droppedPanels == 1, "no floating area, one panel dropped");
}

// Scenario 11: an older (or any other) version is refused; the host then builds its default.
void layouts_scenario_11_version() {
  expectRejected(docWith(stackJson("1"), "", "0"), "version 0 refused");
  expectRejected(docWith(stackJson("1"), "", "3"), "newer version refused");
  expectRejected(docWith(stackJson("1"), "", "1000000000000"), "absurd version refused");
  expectRejected(docWith(stackJson("1"), "", "\"1\""), "version as a string refused");
  expectRejected(docWith(stackJson("1"), "", "1.5"), "fractional version refused");
  expectRejected(R"({"main":{"root":null},"floating":[]})", "missing version refused");
  const LoadResult r = load(docWith(stackJson("1"), "", "0"));
  expect(r.error.find("version") != std::string::npos, "the message names the version");
  expect(load(docWith(stackJson("1"))).ok(), "version 1 accepted");
}

// Panels the host no longer has are dropped and the tree normalised (spec 04 rule 39).
void layouts_missing_panels_dropped() {
  // Panel 99 is unknown. Its stack vanishes, the split with one child left collapses, and a
  // dropped front tab hands the front to the next surviving tab.
  const std::string tree = R"({"type":"split","weight":1,"axis":"row","children":[)" + stackJson("98", "1") + "," + stackJson("1,99,2", "2", "1") + "]}";
  const LoadResult r = load(docWith(tree), 3);
  expect(r.ok() && r.droppedPanels == 2, "two stored panels were dropped");
  const Node& root = *r.layout->areas()[0].root;
  expect(root.kind == Node::Kind::Stack && root.tabs == std::vector<PanelId>({1, 2}), "collapsed to the surviving stack");
  expect(root.active == 1 && root.weight == 1.0, "front tab moved to the next survivor, collapsed node takes the split weight");
  expect(!load(docWith(stackJson("98,99")), 3).ok(), "a layout with no usable panel is refused");
  expect(!load(docWith("null"), 3).ok(), "an all-closed layout is refused (nothing to show)");
}

// Normalisation is idempotent and create() applies it.
void layouts_normalisation_idempotent() {
  Node messy = Node::split(Axis::Row, {Node::split(Axis::Column, {Node::split(Axis::Column, {Node::stack({1}, 0, 2.0)}, 5.0)}, 1.0),
                                       Node::split(Axis::Row, {Node::stack({2}, 0, 1.0), Node::stack({3}, 0, 3.0)}, 2.0),
                                       Node::stack({}, 0, 1.0)});
  DockLayoutResult first = DockLayout::create(makePanels(3), {}, messy);
  expect(first.ok(), "messy tree accepted after normalisation");
  const Node& root = *first.layout->areas()[0].root;
  expect(root.kind == Node::Kind::Split && root.axis == Axis::Row && root.children.size() == 3, "same-axis child spliced, empty stack and single-child splits gone");
  expect(near(root.children[1].weight, 0.5, 1e-9) && near(root.children[2].weight, 1.5, 1e-9), "spliced weights scaled by parent weight / child total (2/4)");
  DockLayoutResult second = DockLayout::create(makePanels(3), {}, root);
  expect(second.ok() && *second.layout == *first.layout, "normalising a normalised tree changes nothing");
  expect(!DockLayout::create(makePanels(3), {}, Node::split(Axis::Row, {Node::stack({1}, 0, -1.0), Node::stack({2})})).ok(), "negative weight refused");
  expect(!DockLayout::create(makePanels(3), {}, Node::split(Axis::Row, {Node::stack({1}, 0, std::nan("")), Node::stack({2})})).ok(), "NaN weight refused");
  expect(!DockLayout::create(makePanels(3), {}, Node::split(Axis::Row, {Node::stack({1}), Node::stack({1})})).ok(), "duplicate panel refused");
  expect(!DockLayout::create(makePanels(3), {}, Node::stack({1, 9})).ok(), "unregistered panel refused");
  std::vector<PanelInfo> dup = makePanels(2);
  dup.push_back(dup[0]);
  expect(!DockLayout::create(dup).ok(), "duplicate registration refused");
  DockConfig bad;
  bad.handleThickness = std::nan("");
  expect(!DockLayout::create(makePanels(1), bad).ok(), "bad config refused");
}

// Hostile layout files.
void layouts_hostile_files() {
  // Missing or extra fields.
  expectRejected("{}", "empty object");
  expectRejected("[]", "array instead of object");
  expectRejected(R"({"version":1,"floating":[]})", "missing main");
  expectRejected(R"({"version":1,"main":{"root":null}})", "missing floating");
  expectRejected(R"({"version":1,"main":{},"floating":[]})", "missing root");
  expectRejected(R"({"version":1,"main":{"root":null,"x":1},"floating":[]})", "unknown member in main");
  expectRejected(R"({"version":1,"main":{"root":null},"floating":[],"extra":true})", "unknown top-level member");
  expectRejected(docWith(R"({"type":"stack","tabs":[1],"active":0})"), "stack without weight");
  expectRejected(docWith(R"({"type":"stack","weight":1,"active":0})"), "stack without tabs");
  expectRejected(docWith(R"({"type":"stack","weight":1,"tabs":[1]})"), "stack without active");
  expectRejected(docWith(R"({"type":"split","weight":1,"children":[)" + stackJson("1") + "]}"), "split without axis");
  expectRejected(docWith(R"({"type":"split","weight":1,"axis":"row"})"), "split without children");
  expectRejected(docWith(R"({"type":"box","weight":1})"), "unknown node type");
  expectRejected(docWith(R"({"weight":1})"), "node without type");
  expectRejected(docWith(R"({"type":"split","weight":1,"axis":"diagonal","children":[)" + stackJson("1") + "]}"), "unknown axis");
  expectRejected(docWith("5"), "root is a number");

  // Bad ratios.
  for (const char* weight : {"0", "-1", "1e-12", "1e12", "\"1\"", "null", "true", "[1]"}) {
    expectRejected(docWith(stackJson("1", weight)), (std::string("bad weight ") + weight).c_str());
  }
  expectRejected(docWith(stackJson("1", "1e999")), "overflowing number");
  expectRejected(docWith(stackJson("1", "NaN")), "NaN literal");

  // Bad ids and tab fields.
  for (const char* id : {"-1", "1.5", "4294967296", "\"1\"", "null", "1e400"}) {
    expectRejected(docWith(stackJson(id)), (std::string("bad panel id ") + id).c_str());
  }
  expectRejected(docWith(stackJson("1,1")), "duplicate id in a stack");
  expectRejected(docWith(R"({"type":"split","weight":1,"axis":"row","children":[)" + stackJson("1") + "," + stackJson("2,1") + "]}"), "duplicate id across stacks");
  expectRejected(docWith(stackJson("1"), R"({"rect":{"x":0,"y":0,"w":200,"h":200},"root":)" + stackJson("1") + "}"), "duplicate id across areas");
  expectRejected(docWith(stackJson("99,99")), "duplicate unknown id");
  expectRejected(docWith(stackJson("1,2", "1", "2")), "active index past the end");
  expectRejected(docWith(stackJson("1,2", "1", "-1")), "negative active index");
  expectRejected(docWith(stackJson("1,2", "1", "0.5")), "fractional active index");
  expectRejected(docWith(stackJson("")), "stack with no tabs");
  expectRejected(docWith(R"({"type":"split","weight":1,"axis":"row","children":[]})"), "split with no children");

  // Floating rectangles.
  const auto floating = [](const std::string& rect) { return R"({"rect":)" + rect + R"(,"root":)" + stackJson("2") + "}"; };
  expectRejected(docWith(stackJson("1"), floating(R"({"x":0,"y":0,"w":-5,"h":100})")), "negative width");
  expectRejected(docWith(stackJson("1"), floating(R"({"x":0,"y":0,"w":1,"h":1})")), "rectangle below the minimum size");
  expectRejected(docWith(stackJson("1"), floating(R"({"x":1e30,"y":0,"w":100,"h":100})")), "absurd coordinate");
  expectRejected(docWith(stackJson("1"), floating(R"({"x":0,"y":0,"w":100})")), "rect missing h");
  expectRejected(docWith(stackJson("1"), floating(R"([0,0,100,100])")), "rect as array");
  expectRejected(docWith(stackJson("1"), R"({"rect":{"x":0,"y":0,"w":100,"h":100},"root":null})"), "floating area without root");
  expectRejected(docWith(stackJson("1"), "7"), "floating entry not an object");

  // Syntax and encoding.
  expectRejected("", "empty file");
  expectRejected(R"({"version":1,"main":{"root":null},"floating":[])", "truncated file");
  expectRejected(std::string("\xff\xfe\x00\x01", 4), "binary garbage");
  expectRejected(docWith(stackJson("1")) + " trailing", "trailing text");
  expectRejected(R"({"version":1,"version":1,"main":{"root":null},"floating":[]})", "duplicate key");
}

// Depth bombs, node floods, and oversized input.
void layouts_resource_bombs() {
  // JSON nesting far beyond anything legal: rejected by the parser limit.
  expectRejected(std::string(100000, '['), "100000 open brackets");
  std::string bomb = docWith(std::string(5000, '{'));
  expectRejected(bomb, "5000 nested objects");

  // A legal-JSON tree nested deeper than kMaxTreeDepth: rejected by the model limit.
  // Single-child splits are legal input; they only count toward depth.
  const auto chain = [](int levels) {
    std::string s = stackJson("1");
    for (int i = 0; i < levels; ++i) s = std::string(R"({"type":"split","weight":1,"axis":")") + (i % 2 == 0 ? "row" : "column") + R"(","children":[)" + s + "]}";
    return s;
  };
  expect(load(docWith(chain(10))).ok(), "a 10-level chain of single-child splits loads (and collapses)");
  expect(load(docWith(chain(10))).layout->areas()[0].root->kind == Node::Kind::Stack, "chain collapsed to the stack");
  expectRejected(docWith(chain(33)), "33 levels exceed the depth limit");
  expectRejected(docWith(chain(60)), "60 levels exceed the depth limit");

  // 100k nodes in one split.
  std::string wide = R"({"type":"split","weight":1,"axis":"row","children":[)";
  for (int i = 0; i < 100000; ++i) {
    if (i > 0) wide += ',';
    wide += stackJson("1");
  }
  wide += "]}";
  expectRejected(docWith(wide), "100000 nodes");

  // Too many floating areas (legal rectangles, distinct panels would be needed; ids repeat so
  // either the area limit or the duplicate check refuses it, never accepts).
  std::string many;
  for (int i = 0; i < 200; ++i) {
    if (i > 0) many += ',';
    many += R"({"rect":{"x":0,"y":0,"w":100,"h":100},"root":)" + stackJson("2") + "}";
  }
  expectRejected(docWith(stackJson("1"), many), "200 floating areas");

  // 5 MB of padding exceeds the input limit.
  expectRejected(docWith(stackJson("1")) + std::string(5 * 1024 * 1024, ' '), "oversized input");
}

// The deepest legal tree survives a round trip.
void layouts_deepest_legal_tree() {
  Node chain = Node::stack({1});
  for (PanelId i = 0; i < 31; ++i) chain = Node::split(i % 2 == 0 ? Axis::Row : Axis::Column, {std::move(chain), Node::stack({2 + i})});
  DockLayoutResult r = DockLayout::create(makePanels(40), {}, chain);
  expect(r.ok(), "depth 32 tree accepted");
  const LoadResult loaded = DockLayout::fromJson(r.layout->toJson(), makePanels(40));
  expect(loaded.ok() && *loaded.layout == *r.layout, "deepest legal tree round-trips");
  Node tooDeep = Node::split(Axis::Row, {std::move(chain), Node::stack({40})});
  expect(!DockLayout::create(makePanels(40), {}, std::move(tooDeep)).ok(), "depth 33 refused at creation");
}

}  // namespace

int main() {
  runCase("layouts_scenario_01_default_and_reset", layouts_scenario_01_default_and_reset);
  runCase("layouts_round_trip_equality", layouts_round_trip_equality);
  runCase("layouts_scenario_03_floating_round_trip", layouts_scenario_03_floating_round_trip);
  runCase("layouts_scenario_04_closed_stays_closed", layouts_scenario_04_closed_stays_closed);
  runCase("layouts_scenario_06_load_replaces", layouts_scenario_06_load_replaces);
  runCase("layouts_scenario_10_unknown_only_floating", layouts_scenario_10_unknown_only_floating);
  runCase("layouts_scenario_11_version", layouts_scenario_11_version);
  runCase("layouts_missing_panels_dropped", layouts_missing_panels_dropped);
  runCase("layouts_normalisation_idempotent", layouts_normalisation_idempotent);
  runCase("layouts_hostile_files", layouts_hostile_files);
  runCase("layouts_resource_bombs", layouts_resource_bombs);
  runCase("layouts_deepest_legal_tree", layouts_deepest_legal_tree);
  return finish("persistence_test");
}
