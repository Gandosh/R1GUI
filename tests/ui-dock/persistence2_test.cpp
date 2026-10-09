// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of layout schema version 2 (slice 5.3): round trips with windows, locks, pins,
//   collapse, closed-panel memory and names; golden files (version 1 still readable, version 2
//   byte-exact); validated load with repairs and reports; hostile files (depth and node bombs,
//   NaN/negative/huge values, huge strings, duplicate keys, wrong types, newer versions); monitor
//   fitting on load; new-panel placement.
// Why: layout files are user-editable and shared; a bad one must never reach the tree, never throw
//   and never cost the caller its current layout.
// Callers: CTest (label fast). R1UI_DOCK_GOLDEN_DIR points at tests/ui-dock/golden.
#include <fstream>
#include <iterator>
#include <limits>

#include "TestSupport.h"

using namespace dock_test;

namespace {

std::string readGolden(const char* name) {
  std::ifstream in(std::string(R1UI_DOCK_GOLDEN_DIR) + "/" + name, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  return text;
}

std::vector<PanelInfo> eightPanels() {
  std::vector<PanelInfo> panels;
  for (PanelId id = 1; id <= 8; ++id) {
    PanelInfo p;
    p.id = id;
    p.title = "P" + std::to_string(id);
    panels.push_back(p);
  }
  return panels;
}

// The layout the golden file captures: every v2 feature at least once.
DockLayout canonicalLayout() {
  Node left = Node::stack({1, 2}, 1, 1.5);
  Node middle = Node::split(Axis::Column, {Node::stack({3}), Node::stack({4, 5}, 0, 2.0)}, 2.0);
  Node right = Node::stack({7}, 0, 1.0);
  DockLayoutResult created = DockLayout::create(eightPanels(), {}, Node::split(Axis::Row, {left, middle, right}));
  DockLayout dock = std::move(*created.layout);
  const Rect main{0, 0, 1200, 800};
  dock.setPinned(0, {2}, true, main);
  dock.setCollapsed(0, {0}, true);
  dock.setPanelLocked(7, true);
  DropZone zone;
  zone.preview = {120.5, 80.25, 400, 300};
  dock.dock(6, zone);
  WindowState floating;
  floating.maximized = false;
  floating.monitor = "DISPLAY2";
  floating.monitorIndex = 1;
  floating.dpiScale = 1.5;
  dock.setWindowState(dock.areas()[1].id, floating);
  WindowState mainState;
  mainState.hasRect = true;
  mainState.rect = {50, 40, 1280, 720};
  mainState.maximized = true;
  mainState.monitor = "DISPLAY1";
  mainState.monitorIndex = 0;
  mainState.dpiScale = 1.25;
  dock.setWindowState(kMainAreaId, mainState);
  dock.closePanel(2);
  dock.closePanel(8);  // 8 was never docked: refused, so 8 stays a plain closed panel
  dock.setMeta({"Modelling", "Panels for modelling work"});
  return dock;
}

LoadResult loadV2(const std::string& text, PanelId panelCount = 8) { return DockLayout::fromJson(text, makePanels(panelCount)); }

std::string stack(const std::string& tabs, const std::string& weight = "1", const std::string& active = "0") {
  return R"({"type":"stack","weight":)" + weight + R"(,"tabs":[)" + tabs + R"(],"active":)" + active + "}";
}

std::string doc2(const std::string& root, const std::string& extra = "", const std::string& floating = "") {
  return R"({"version":2,"main":{"root":)" + root + R"(},"floating":[)" + floating + "]" + extra + "}";
}

void expectRejected(const std::string& text, const char* what, PanelId panels = 8) {
  const LoadResult r = loadV2(text, panels);
  if (r.ok() || r.error.empty()) {
    std::fprintf(stderr, "  not rejected: %s\n", what);
    expect(false, what);
  }
}

void v2_round_trip_is_exact() {
  const DockLayout dock = canonicalLayout();
  expect(dock.validate().ok, "canonical layout is valid");
  const std::string json = dock.toJson();
  LoadResult loaded = loadV2(json);
  expect(loaded.ok(), "loads");
  if (!loaded.ok()) {
    std::fprintf(stderr, "%s\n", loaded.error.c_str());
    return;
  }
  expect(*loaded.layout == dock, "round trip equals the original (windows, locks, pins, closed slots, names)");
  expect(loaded.layout->toJson() == json, "second serialisation is byte-identical");
  expect(loaded.warnings.empty() && loaded.repairedValues == 0 && loaded.droppedPanels == 0, "a clean file reports nothing");
  expect(loaded.layout->panel(7)->locked && !loaded.layout->panel(1)->locked, "lock flags persist");
  expect(loaded.layout->areas()[0].root->children[0].collapsed && loaded.layout->areas()[0].root->children[2].pinned, "pin and collapse persist");
  expect(loaded.layout->closedSlot(2) != nullptr && loaded.layout->closedSlot(2)->index == 1, "closed-panel memory persists");
  expect(loaded.layout->meta().name == "Modelling" && loaded.layout->areas()[0].window.maximized, "names and window state persist");
}

void golden_files() {
  const std::string golden = readGolden("layout_v2.json");
  expect(!golden.empty(), "golden v2 file is present");
  const std::string produced = canonicalLayout().toJson();
  if (produced != golden) std::fprintf(stderr, "golden v2 differs. produced:\n%s\n", produced.c_str());
  expect(produced == golden, "toJson matches the golden v2 file byte for byte");
  const LoadResult loaded = loadV2(golden);
  expect(loaded.ok() && *loaded.layout == canonicalLayout(), "the golden v2 file loads to the canonical layout");

  const std::string v1 = readGolden("layout_v1.json");
  expect(!v1.empty(), "golden v1 file is present");
  const LoadResult old = loadV2(v1, 6);
  expect(old.ok() && old.sourceVersion == 1, "version 1 files are still read");
  if (old.ok()) {
    expect(old.layout->areas().size() == 2 && old.layout->areas()[1].rect == Rect({40, 60, 500, 400}), "v1 floating area restored");
    expect(old.layout->areas()[0].root->children.size() == 2, "v1 tree restored");
    expect(old.layout->toJson().find("\"version\":2") != std::string::npos, "and written back as version 2");
  }
}

void version_handling() {
  expectRejected(R"({"version":3,"main":{"root":null},"floating":[]})", "version 3");
  const LoadResult newer = loadV2(R"({"version":3,"main":{"root":null},"floating":[]})");
  expect(newer.error.find("unsupported layout version") != std::string::npos && !newer.layout.has_value(), "clean message, no layout");
  expectRejected(R"({"version":0,"main":{"root":null},"floating":[]})", "version 0");
  expectRejected(R"({"version":-2,"main":{"root":null},"floating":[]})", "negative version");
  expectRejected(R"({"version":2.5,"main":{"root":null},"floating":[]})", "fractional version");
  expectRejected(R"({"version":"2","main":{"root":null},"floating":[]})", "string version");
  expectRejected(R"({"main":{"root":null},"floating":[]})", "missing version");
  expectRejected(R"({"version":1,"name":"x","main":{"root":null},"floating":[]})", "version 1 has no name member");
}

void repairs_are_reported() {
  const std::string text = doc2(R"({"type":"split","weight":-4,"axis":"row","children":[)" + stack("1,1,2,99,5", "0", "7") + "," + stack("3", "1e30") + "]}");
  const LoadResult r = loadV2(text, 6);
  expect(r.ok(), "repairable file loads");
  if (!r.ok()) return;
  expect(r.duplicatePanels == 1 && r.droppedPanels == 1, "one duplicate, one unknown dropped");
  expect(r.repairedValues >= 3 && !r.warnings.empty(), "weights and the active index were repaired and reported");
  const Node& root = *r.layout->areas()[0].root;
  expect(root.children[0].tabs == std::vector<PanelId>({1, 2, 5}), "tabs after drops");
  expect(root.children[0].active == 2, "active index clamped to the last tab");
  expect(root.children[0].weight >= kMinWeight && root.children[1].weight <= kMaxWeight && root.weight >= kMinWeight, "weights repaired into range");
  expect(r.layout->validate().ok, "result is valid");
  expectRejected(R"({"version":1,"main":{"root":{"type":"stack","weight":-1,"tabs":[1],"active":0}},"floating":[]})", "v1 stays strict about weights");
  expectRejected(R"({"version":1,"main":{"root":{"type":"stack","weight":1,"tabs":[1,1],"active":0}},"floating":[]})", "v1 stays strict about duplicates");
}

void floating_and_window_repairs() {
  const std::string text = doc2("null", "",
                                R"({"rect":{"x":-1e30,"y":5,"w":-10,"h":0},"root":)" + stack("1") +
                                    R"(,"window":{"maximized":true,"monitor":"DP-1","monitorIndex":3000,"dpi":-2,"rect":{"x":0,"y":0,"w":-1,"h":10}}})");
  const LoadResult r = loadV2(text);
  expect(r.ok(), "out-of-range floating rectangle is repaired");
  if (!r.ok()) return;
  const Area& a = r.layout->areas()[1];
  expect(a.rect.w >= 64 && a.rect.h >= 64 && a.rect.x >= -kMaxCoordinate, "rectangle clamped");
  expect(a.window.maximized && a.window.monitor == "DP-1" && a.window.monitorIndex == -1 && a.window.dpiScale == 1.0 && !a.window.hasRect,
         "window state repaired field by field");
  expect(r.repairedValues >= 4, "each repair counted");
}

void strings_are_bounded() {
  const std::string huge(3 * 1024 * 1024, 'x');
  const LoadResult r = loadV2(R"({"version":2,"name":")" + huge + R"(","description":"a\u0001b\nc","main":{"root":)" + stack("1") +
                              R"(,"window":{"monitor":")" + std::string(2000, 'm') + R"("}},"floating":[]})");
  expect(r.ok(), "huge strings do not reject the file");
  if (!r.ok()) return;
  expect(r.layout->meta().name.size() == kMaxNameBytes, "name cut to the limit");
  expect(r.layout->meta().description == "a b c", "control characters become spaces");
  expect(r.layout->areas()[0].window.monitor.size() == kMaxNameBytes, "monitor name cut");
  const std::string multibyte = "\xE2\x82\xAC";  // the euro sign, 3 bytes
  std::string name;
  for (int i = 0; i < 200; ++i) name += multibyte;
  const LoadResult cut = loadV2(R"({"version":2,"name":")" + name + R"(","main":{"root":)" + stack("1") + R"(},"floating":[]})");
  expect(cut.ok() && cut.layout->meta().name.size() % 3 == 0 && cut.layout->meta().name.size() <= kMaxNameBytes, "cut on a UTF-8 boundary");
}

void hostile_structure() {
  std::string deep;
  for (int i = 0; i < 5000; ++i) deep += R"({"type":"split","weight":1,"axis":"row","children":[)";
  expectRejected(doc2(deep), "depth bomb");
  std::string nested = stack("1");
  for (int i = 0; i < 40; ++i) {
    nested = std::string(R"({"type":"split","weight":1,"axis":")") + (i % 2 == 0 ? "row" : "column") + R"(","children":[)" + nested + "," + stack("2") + "]}";
  }
  expectRejected(doc2(nested), "40 tree levels exceed the limit of 32");
  std::string many = R"({"type":"split","weight":1,"axis":"row","children":[)";
  for (int i = 0; i < 100000; ++i) many += (i ? "," : "") + stack("1");
  many += "]}";
  expectRejected(doc2(many), "100k nodes");
  std::string some = R"({"type":"split","weight":1,"axis":"row","children":[)";
  for (int i = 0; i < 5000; ++i) some += (i ? "," : "") + stack("1");
  some += "]}";
  expectRejected(doc2(some), "5000 nodes exceed the node limit");
  expectRejected(doc2(stack("1")) + "x", "trailing garbage");
  expectRejected(doc2(stack("1")).substr(0, 30), "truncated file");
  expectRejected("", "empty file");
  expectRejected("[]", "array instead of object");
  expectRejected("null", "null");
  expectRejected(R"({"version":2,"version":2,"main":{"root":null},"floating":[]})", "duplicate key");
  expectRejected(R"({"version":2,"main":{"root":)" + stack("1") + R"(,"root":null},"floating":[]})", "duplicate key in a nested object");
  expectRejected(doc2(R"({"type":"stack","weight":1,"tabs":[1],"active":0,"extra":1})"), "unknown member");
  expectRejected(doc2(R"({"type":"cube","weight":1})"), "unknown node type");
  expectRejected(doc2(R"({"type":"split","weight":1,"axis":"diagonal","children":[]})"), "unknown axis");
  expectRejected(doc2(R"({"type":"stack","weight":"1","tabs":[1],"active":0})"), "weight as a string");
  expectRejected(doc2(R"({"type":"stack","weight":1,"tabs":"1","active":0})"), "tabs as a string");
  expectRejected(doc2(R"({"type":"stack","weight":1,"tabs":[1],"active":0,"pinned":"x"})"), "pinned as a string");
  expectRejected(doc2(R"({"type":"stack","weight":1,"tabs":[1],"active":0,"collapsed":1})"), "collapsed as a number");
  expectRejected(R"({"version":2,"main":{"root":null},"floating":{}})", "floating as an object");
  expectRejected(R"({"version":2,"main":{"root":null,"window":5},"floating":[]})", "window as a number");
  expectRejected(R"({"version":2,"main":{"root":null,"window":{"zoom":1}},"floating":[]})", "unknown window member");
  expectRejected(R"({"version":2,"main":{"root":null},"floating":[],"panels":{}})", "panels as an object");
  expectRejected(R"({"version":2,"main":{"root":null},"floating":[],"panels":[5]})", "panel entry as a number");
  expectRejected(R"({"version":2,"main":{"root":null},"floating":[],"closed":"x"})", "closed as a string");
  expectRejected(R"({"version":2,"name":5,"main":{"root":null},"floating":[]})", "name as a number");
  expectRejected(doc2("null"), "no usable panel at all");
  expectRejected(doc2(stack("77")), "only unknown panels");
}

void hostile_values() {
  const LoadResult frac = loadV2(doc2(stack("1,2.5,-3,4294967296,\"x\",null,true,3")));
  expect(frac.ok() && frac.layout->areas()[0].root->tabs == std::vector<PanelId>({1, 3}), "invalid ids dropped in version 2");
  expect(frac.repairedValues == 6, "each invalid id counted");
  std::string floatings;
  for (int i = 0; i < 65; ++i) floatings += std::string(i ? "," : "") + R"({"rect":{"x":0,"y":0,"w":100,"h":100},"root":)" + stack("2") + "}";
  expectRejected(doc2(stack("1"), "", floatings), "65 floating areas");
  expectRejected(R"({"version":2,"main":{"root":null},"floating":[{"rect":{"x":0,"y":0,"w":1e999,"h":1},"root":)" + stack("1") + "}]}", "number that overflows a double");
  expectRejected(R"({"version":2,"main":{"root":null},"floating":[{"rect":{"x":0,"y":0,"w":10},"root":)" + stack("1") + "}]}", "rect without h");
  expectRejected(std::string(R"({"version":2,"main":{"root":)") + stack("1") + R"(},"floating":[]})" + std::string(10, '\0'), "NUL bytes after the document");
  expectRejected("{\"version\":2,\"name\":\"\xff\xfe\",\"main\":{\"root\":null},\"floating\":[]}", "invalid UTF-8");
  std::string deepJson(200, '[');
  expectRejected(deepJson, "deeply nested arrays");
}

void panels_list_and_new_panels() {
  std::vector<PanelInfo> panels = makePanels(5);
  panels[2].suggested = {1, true, Side::Right};    // 3 joins 1
  panels[3].suggested = {2, false, Side::Bottom};  // 4 below 2
  const std::string file = R"({"version":2,"main":{"root":{"type":"split","weight":1,"axis":"row","children":[)" + stack("1") + "," + stack("2") +
                           R"(]},"window":{"maximized":false,"monitor":"","monitorIndex":-1,"dpi":1}},"floating":[],"panels":[{"id":1,"locked":false},{"id":2,"locked":true},{"id":5,"locked":false}],"closed":[]})";
  const LoadResult r = DockLayout::fromJson(file, panels);
  expect(r.ok(), "loads");
  if (!r.ok()) return;
  expect(r.newPanelsPlaced == 2, "two new panels have a suggestion");
  expect(r.layout->isDocked(3) && r.layout->isDocked(4), "both placed");
  expect(r.layout->locate(3)->tab == 1 && r.layout->locate(1)->area == kMainAreaId, "3 joined 1's stack");
  expect(!r.layout->isDocked(5) && r.layout->closedSlot(5) == nullptr, "5 is listed and was closed by the user: stays closed");
  expect(r.layout->panel(2)->locked, "the stored lock flag wins over the registry default");
  LoadOptions off;
  off.placeNewPanels = false;
  const LoadResult plain = DockLayout::fromJson(file, panels, {}, off);
  expect(plain.ok() && !plain.layout->isDocked(3), "placement can be switched off");
  // A suggestion whose anchor is not open leaves the panel closed.
  panels[3].suggested = {5, true, Side::Right};
  const LoadResult blocked = DockLayout::fromJson(file, panels);
  expect(blocked.ok() && !blocked.layout->isDocked(4) && blocked.newPanelsPlaced == 1, "anchor closed: not placed");
  // Version 1 files have no panel list, so nothing counts as new.
  const LoadResult v1 = DockLayout::fromJson(R"({"version":1,"main":{"root":)" + stack("1") + R"(},"floating":[]})", panels);
  expect(v1.ok() && v1.newPanelsPlaced == 0, "v1: no placement");
}

void closed_slots_are_validated() {
  const std::string good = R"({"panel":3,"neighbours":[1,99,3],"index":1,"anchor":99,"side":"left","floating":false})";
  const std::string badSide = R"({"panel":3,"neighbours":[],"index":0,"anchor":0,"side":"up","floating":false})";
  const std::string dockedOne = R"({"panel":1,"neighbours":[],"index":0,"anchor":0,"side":"left","floating":false})";
  const std::string unknown = R"({"panel":42,"neighbours":[],"index":0,"anchor":0,"side":"left","floating":false})";
  const std::string badRect = R"({"panel":4,"neighbours":[],"index":0,"anchor":0,"side":"left","floating":true,"rect":{"x":0,"y":0,"w":-1,"h":1}})";
  const std::string extra = R"({"panel":5,"neighbours":[],"index":0,"anchor":0,"side":"left","floating":false,"x":1})";
  const std::string text = doc2(stack("1,2"), R"(,"closed":[)" + good + "," + badSide + "," + dockedOne + "," + unknown + "," + badRect + "," + extra + "]");
  const LoadResult r = loadV2(text, 6);
  expect(r.ok(), "damaged closed records do not reject the file");
  if (!r.ok()) return;
  expect(r.layout->closedSlots().size() == 1, "only the good record (and the docked-panel one, dropped at commit) survive");
  const ClosedSlot* slot = r.layout->closedSlot(3);
  expect(slot != nullptr && slot->neighbours == std::vector<PanelId>({1}) && slot->anchor == 0, "unknown neighbours and anchors filtered");
  expect(r.repairedValues >= 4, "four damaged records counted");
  const std::string dup = doc2(stack("1"), R"(,"closed":[)" + good + "," + good + "]");
  const LoadResult twice = loadV2(dup, 6);
  expect(twice.ok() && twice.layout->closedSlots().size() == 1, "a panel's slot appears once");
}

void monitors_applied_on_load() {
  MonitorSet set;
  set.monitors.push_back({"A", {0, 0, 1920, 1080}, {0, 0, 1920, 1040}, 1.0});
  LoadOptions options;
  options.monitors = &set;
  const std::string text = doc2(stack("1"), "",
                                R"({"rect":{"x":9000,"y":50,"w":400,"h":300},"root":)" + stack("2") + "}");
  const LoadResult r = DockLayout::fromJson(text, makePanels(4), {}, options);
  expect(r.ok() && r.windowsMoved == 1, "one window moved");
  if (r.ok()) expect(r.layout->areas()[1].rect == Rect({760, 370, 400, 300}), "centred on the work area");
  const LoadResult kept = DockLayout::fromJson(text, makePanels(4));
  expect(kept.ok() && kept.windowsMoved == 0 && kept.layout->areas()[1].rect.x == 9000, "without monitors nothing moves");
  MonitorSet empty;
  options.monitors = &empty;
  const LoadResult unknown = DockLayout::fromJson(text, makePanels(4), {}, options);
  expect(unknown.ok() && unknown.windowsMoved == 0, "an empty monitor set cannot judge");
}

void random_corruption_never_escapes() {
  const std::string base = canonicalLayout().toJson();
  uint32_t seed = 12345;
  const auto next = [&]() {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
  };
  size_t accepted = 0;
  for (int i = 0; i < 400; ++i) {
    std::string mutated = base;
    const int edits = 1 + static_cast<int>(next() % 4);
    for (int e = 0; e < edits; ++e) {
      const size_t at = next() % mutated.size();
      switch (next() % 4) {
        case 0: mutated[at] = static_cast<char>(next() % 256); break;
        case 1: mutated.erase(at, 1 + next() % 8); break;
        case 2: mutated.insert(at, 1, "{}[]\",:-0123456789etruflsn"[next() % 26]); break;
        default: mutated.resize(at); break;
      }
      if (mutated.empty()) break;
    }
    const LoadResult r = DockLayout::fromJson(mutated, eightPanels());
    if (r.ok()) {
      ++accepted;
      expect(r.layout->validate().ok, "an accepted mutation always yields a valid layout");
      expect(DockLayout::fromJson(r.layout->toJson(), eightPanels()).ok(), "and can be saved and loaded again");
    } else {
      expect(!r.error.empty() && !r.layout.has_value(), "a rejected mutation has an error and no layout");
    }
  }
  std::fprintf(stderr, "  %zu of 400 mutations were still loadable\n", accepted);
}

}  // namespace

int main() {
  runCase("v2_round_trip_is_exact", v2_round_trip_is_exact);
  runCase("golden_files", golden_files);
  runCase("version_handling", version_handling);
  runCase("repairs_are_reported", repairs_are_reported);
  runCase("floating_and_window_repairs", floating_and_window_repairs);
  runCase("strings_are_bounded", strings_are_bounded);
  runCase("hostile_structure", hostile_structure);
  runCase("hostile_values", hostile_values);
  runCase("panels_list_and_new_panels", panels_list_and_new_panels);
  runCase("closed_slots_are_validated", closed_slots_are_validated);
  runCase("monitors_applied_on_load", monitors_applied_on_load);
  runCase("random_corruption_never_escapes", random_corruption_never_escapes);
  return finish("persistence2_test");
}
