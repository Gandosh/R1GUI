// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of customization persistence (customize/CustomizationIo.h): a golden JSON text, the
//   round trip (export -> parse -> equal delta, same effective layout), strict validated loading
//   (unknown fields ignored, unknown commands kept as missing, duplicates repaired with a report,
//   bad rectangles, depth, size and entry limits, invalid UTF-8, hostile JSON), rejection keeping the
//   previous state and the corrupt file aside, atomic writes on a real directory, the user store with
//   the workspace override, import and export, save-on-commit and live apply through the version.
// Callers: CTest (label fast).
#include <filesystem>
#include <fstream>
#include <random>

#include "CustomizeFixtures.h"

namespace {

using namespace r1test;
namespace fs = std::filesystem;

cz::Customization busy() {
  cz::Customization c = makeCustomization();
  c.hideEntry("edit.copy");
  c.renameLabel("menu.view", "Display \"quoted\" \xC3\xA9");
  c.move("edit.paste", {"edit.clip", "edit.cut", cz::Side::Before});
  const std::string menu = c.addUserMenu("Mine").id;
  c.addCommand(menu, "edit.undo");
  c.addHeading(menu, "Tools");
  c.addUserToolbar("Extra", cz::Orientation::Vertical);
  c.setToolbarSizeStep("tb.main", cz::SizeStep::Large);
  c.setToolbarGap("tb.main", 4.5);
  c.setPanelSnap("fp.main", true, 4);
  c.placeButton("fp.main", "edit.redo", {10, 20, 60, 30});
  c.moveButton("fp.undo", 100, 50);
  return c;
}

void testGolden() {
  cz::Customization c = makeCustomization();
  c.hideEntry("edit.copy");
  c.renameLabel("menu.view", "Display");
  c.move("edit.paste", {"edit.clip", "edit.cut", cz::Side::Before});
  const std::string menu = c.addUserMenu("Mine").id;  // um1, us2
  c.addCommand(menu, "edit.undo");                      // u3
  c.setToolbarGap("tb.main", 4);
  c.moveButton("fp.undo", 100, 50);
  const std::string golden =
      "{\"format\":\"r1ui-customization\",\n"
      "\"version\":1,\n"
      "\"serial\":3,\n"
      "\"edits\":[\n"
      "{\"node\":\"edit.copy\",\"hidden\":true},\n"
      "{\"node\":\"fp.undo\",\"rect\":[100,50,64,32]},\n"
      "{\"node\":\"menu.view\",\"label\":\"Display\"}],\n"
      "\"moves\":[\n"
      "{\"node\":\"edit.paste\",\"parent\":\"edit.clip\",\"anchor\":\"edit.cut\",\"side\":\"before\"}],\n"
      "\"added\":[\n"
      "{\"id\":\"um1\",\"kind\":\"menu\",\"label\":\"Mine\",\"parent\":\"menubar\",\"anchor\":\"\",\"side\":\"end\"},\n"
      "{\"id\":\"us2\",\"kind\":\"section\",\"parent\":\"um1\",\"anchor\":\"\",\"side\":\"end\"},\n"
      "{\"id\":\"u3\",\"kind\":\"command\",\"command\":\"edit.undo\",\"parent\":\"us2\",\"anchor\":\"\",\"side\":\"end\"}],\n"
      "\"toolbars\":[],\n"
      "\"panels\":[],\n"
      "\"toolbarEdits\":[\n"
      "{\"id\":\"tb.main\",\"gap\":4}],\n"
      "\"panelEdits\":[]\n}\n";
  const std::string text = cz::exportDelta(c.userDelta());
  if (text != golden) std::fprintf(stderr, "--- expected\n%s--- actual\n%s", golden.c_str(), text.c_str());
  R1_EXPECT(text == golden);
}

void testRoundTrip() {
  cz::Customization c = busy();
  const std::string text = cz::exportDelta(c.userDelta());
  const cz::ParseResult parsed = cz::parseDelta(text);
  R1_EXPECT(parsed.ok && parsed.issues.empty() && parsed.skipped == 0);
  R1_EXPECT(parsed.delta == c.userDelta());
  R1_EXPECT(cz::exportDelta(parsed.delta) == text);  // stable
  cz::Customization reloaded = makeCustomization();
  reloaded.setUserDelta(parsed.delta);
  R1_EXPECT(cz::exportDelta(reloaded.userDelta()) == text);
  R1_EXPECT(sectionIds(reloaded.effective().layout, "edit.clip") == sectionIds(c.effective().layout, "edit.clip"));
  R1_EXPECT(cz::findNode(reloaded.effective().layout, "menu.view")->shownLabel() == "Display \"quoted\" \xC3\xA9");
  R1_EXPECT(cz::validateLayout(reloaded.effective().layout).empty());
  // The empty delta round trips too.
  R1_EXPECT(cz::parseDelta(cz::exportDelta({})).ok && cz::parseDelta(cz::exportDelta({})).delta.empty());
}

std::string file(const std::string& body) { return "{\"format\":\"r1ui-customization\",\"version\":1," + body + "}"; }

void testWholeFileRejection() {
  const auto rejected = [](const std::string& text) { return !cz::parseDelta(text).ok; };
  R1_EXPECT(rejected(""));
  R1_EXPECT(rejected("not json"));
  R1_EXPECT(rejected("[]"));
  R1_EXPECT(rejected("{\"format\":\"other\",\"version\":1}"));
  R1_EXPECT(rejected("{\"format\":\"r1ui-customization\"}"));
  R1_EXPECT(rejected("{\"format\":\"r1ui-customization\",\"version\":2}"));
  R1_EXPECT(cz::parseDelta("{\"format\":\"r1ui-customization\",\"version\":2}").error.find("newer") != std::string::npos);
  R1_EXPECT(rejected("{\"format\":\"r1ui-customization\",\"version\":1} trailing"));
  R1_EXPECT(rejected(file("\"edits\":[{\"node\":\"a\",\"label\":\"\xFF\xFE\"}]")));         // invalid UTF-8
  R1_EXPECT(rejected(file("\"edits\":[{\"node\":\"a\",\"node\":\"b\",\"hidden\":true}]"))); // duplicate JSON keys
  R1_EXPECT(rejected(file("\"edits\":[{\"node\":\"a\",\"hidden\":1e999}]")));              // a number that is not finite
  std::string deep = "[";
  for (int i = 0; i < 200; ++i) deep += "[";
  R1_EXPECT(rejected(file("\"edits\":" + deep)));
  std::string nested = file("\"x\":");
  for (int i = 0; i < 20; ++i) nested.insert(nested.size() - 1, "{\"a\":");
  R1_EXPECT(rejected(nested));
  // Nothing usable in a file that has entries.
  R1_EXPECT(rejected(file("\"edits\":[{\"node\":\"bad id\",\"hidden\":true},{\"hidden\":true}]")));
  // Too many entries.
  std::string many = "\"edits\":[";
  for (size_t i = 0; i < cz::kMaxNodes + 1; ++i) many += (i ? "," : "") + std::string("{\"node\":\"n") + std::to_string(i) + "\",\"hidden\":true}";
  many += "]";
  const cz::ParseResult over = cz::parseDelta(file(many));
  R1_EXPECT(!over.ok && over.error.find("too many") != std::string::npos);
  // Exactly at the limit is accepted.
  many = "\"edits\":[";
  for (size_t i = 0; i < cz::kMaxNodes; ++i) many += (i ? "," : "") + std::string("{\"node\":\"n") + std::to_string(i) + "\",\"hidden\":true}";
  many += "]";
  const cz::ParseResult atLimit = cz::parseDelta(file(many));
  R1_EXPECT(atLimit.ok && atLimit.delta.edits.size() == cz::kMaxNodes);
  // Larger than the byte limit: refused before parsing.
  R1_EXPECT(rejected(std::string(cz::kMaxFileBytes + 1, ' ')));
}

void testEntryRepairs() {
  const cz::ParseResult r = cz::parseDelta(file(
      "\"future\":{\"x\":1},"
      "\"edits\":["
      "{\"node\":\"edit.copy\",\"hidden\":true,\"unknown\":[1,2]},"            // unknown fields are ignored
      "{\"node\":\"edit.copy\",\"hidden\":false},"                             // duplicate: the first wins
      "{\"node\":\"fp.undo\",\"rect\":[0,0,-5,10]},"                           // negative size: skipped
      "{\"node\":\"fp.save\",\"rect\":[0,0,4,40]},"                            // below the minimum size: skipped
      "{\"node\":\"menu.file\",\"label\":\"A\\tB\\nC\"},"                      // control characters are replaced
      "5,"                                                                     // not an object
      "{\"node\":\"ghost.node\",\"hidden\":true}],"                            // unknown nodes are kept (reported when applied)
      "\"moves\":[{\"node\":\"a\",\"parent\":\"p\",\"side\":\"sideways\"}],"   // unknown side: end
      "\"added\":["
      "{\"id\":\"u1\",\"kind\":\"command\",\"command\":\"no.such.command\",\"parent\":\"file.main\"},"  // unknown command kept
      "{\"id\":\"u1\",\"kind\":\"separator\",\"parent\":\"file.main\"},"      // duplicate id
      "{\"id\":\"u2\",\"kind\":\"banana\",\"parent\":\"file.main\"},"          // unknown kind
      "{\"id\":\"u3\",\"kind\":\"button\",\"command\":\"x\",\"parent\":\"fp.main\"},"   // a free button without a rect
      "{\"id\":\"u4\",\"kind\":\"command\",\"parent\":\"file.main\"}],"        // a command without a command id
      "\"toolbars\":[{\"id\":\"ut1\",\"title\":\"T\",\"gap\":999,\"sizeStep\":\"huge\"}],"
      "\"panels\":[{\"id\":\"up1\",\"title\":\"P\",\"width\":-1,\"height\":100},{\"id\":\"up2\",\"title\":\"P\",\"width\":100,\"height\":100,\"grid\":0}],"
      "\"toolbarEdits\":[{\"id\":\"tb.main\",\"gap\":-3}],"
      "\"panelEdits\":[{\"id\":\"fp.main\",\"snap\":true,\"grid\":9}]"));
  R1_EXPECT(r.ok);
  R1_EXPECT(r.delta.edits.size() == 3 && r.delta.edits.at("edit.copy").hidden == true && r.delta.edits.count("ghost.node") == 1);
  R1_EXPECT(r.delta.edits.at("menu.file").label == "A B C");
  R1_EXPECT(r.delta.moves.size() == 1 && r.delta.moves[0].to.side == cz::Side::End);
  R1_EXPECT(r.delta.added.size() == 1 && r.delta.added[0].node.commandId == "no.such.command");
  R1_EXPECT(r.delta.userToolbars.size() == 1 && r.delta.userToolbars[0].gap == cz::kDefaultToolbarGap && r.delta.userToolbars[0].sizeStep == cz::SizeStep::Medium);
  R1_EXPECT(r.delta.userPanels.size() == 1 && r.delta.userPanels[0].grid == cz::kDefaultGridSize);
  R1_EXPECT(r.delta.toolbarEdits.empty() && r.delta.panelEdits.size() == 1);
  R1_EXPECT(r.skipped >= 8 && r.issues.size() >= 10);

  // Unknown command ids load and show up as missing markers, not as errors.
  cz::Customization c(builtinV1(), [](const std::string& id) { return id != "no.such.command"; });
  c.setUserDelta(r.delta);
  R1_EXPECT(c.effective().report.has(cz::ReportEntry::Code::MissingCommand, "u1"));
  R1_EXPECT(c.editView().layout.menuBar.menus[0].children[0].children.back().missing);
}

void testRandomGarbage() {
  // Mutated valid files and random bytes never crash and never give an illegal effective layout.
  const std::string good = cz::exportDelta(busy().userDelta());
  std::mt19937 rng(7);
  int accepted = 0;
  for (int i = 0; i < 600; ++i) {
    std::string text = good;
    const int edits = 1 + static_cast<int>(rng() % 6);
    for (int k = 0; k < edits; ++k) {
      switch (rng() % 4) {
        case 0: text[rng() % text.size()] = static_cast<char>(rng() & 0xFF); break;
        case 1: text.erase(rng() % text.size(), rng() % 20); break;
        case 2: text.insert(rng() % text.size(), std::string(1 + rng() % 8, "{}[]\",:0-e"[rng() % 10])); break;
        default: text.resize(rng() % (text.size() + 1)); break;
      }
      if (text.empty()) text = "x";
    }
    const cz::ParseResult r = cz::parseDelta(text);
    if (!r.ok) continue;
    ++accepted;
    cz::Customization c = makeCustomization();
    c.setUserDelta(r.delta);
    R1_EXPECT(cz::validateLayout(c.effective().layout).empty());
    R1_EXPECT(cz::validateLayout(c.editView().layout).empty());
  }
  R1_EXPECT(accepted > 0);
  for (int i = 0; i < 200; ++i) {
    std::string noise(rng() % 400, '\0');
    for (char& ch : noise) ch = static_cast<char>(rng() & 0xFF);
    R1_EXPECT(!cz::parseDelta(noise).ok);
  }
}

void testStoresAndLayers() {
  cz::MemoryTextStore user, workspace;
  cz::Customization c = makeCustomization();
  cz::CustomizationStorage storage(c, user, &workspace);
  R1_EXPECT(storage.load().ok && c.userDelta().empty());  // nothing stored yet: defaults stay

  c.beginEditSession();
  c.hideEntry("edit.copy");
  c.renameLabel("menu.view", "Mine");
  R1_EXPECT(!user.contents());   // not written until the session is committed
  c.commitSession();
  R1_EXPECT(user.contents().has_value() && storage.lastError().empty());

  // A fresh start loads it back; the workspace layer wins over the user layer.
  workspace.setContents(cz::exportDelta([] { cz::Delta d; d.edits["menu.view"].label = "Workspace"; return d; }()));
  cz::Customization fresh = makeCustomization();
  cz::CustomizationStorage storage2(fresh, user, &workspace);
  const cz::LoadReport report = storage2.load();
  R1_EXPECT(report.ok && report.userLoaded && report.workspaceLoaded);
  R1_EXPECT(!cz::findNode(fresh.editView().layout, "edit.copy")->visible && cz::findNode(fresh.effective().layout, "menu.view")->shownLabel() == "Workspace");

  // A rejected file keeps the previous state and is moved aside.
  const cz::Delta keptDelta = fresh.userDelta();
  user.setContents("{ this is not json");
  const cz::LoadReport bad = storage2.load();
  R1_EXPECT(!bad.ok && !bad.userLoaded && bad.error.find("user file rejected") != std::string::npos);
  R1_EXPECT(!bad.userKeptAside.empty() && user.asides().size() == 1 && user.asides()[0] == "{ this is not json" && !user.contents());
  R1_EXPECT(fresh.userDelta() == keptDelta && !cz::findNode(fresh.editView().layout, "edit.copy")->visible);
  R1_EXPECT(!storage2.lastError().empty());
  // The workspace layer is rejected on its own.
  workspace.setContents("[]");
  const cz::LoadReport badWs = storage2.load();
  R1_EXPECT(!badWs.ok && !badWs.workspaceKeptAside.empty());
}

void testFileStore() {
  const fs::path dir = fs::temp_directory_path() / "r1ui_customize_io_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  const fs::path path = dir / "nested" / "menus.json";
  cz::FileTextStore store(path);
  R1_EXPECT(!store.load().exists && store.load().error.empty());
  std::string error;
  R1_EXPECT(store.save("{\"a\":1}", error) && error.empty());
  R1_EXPECT(fs::exists(path) && !fs::exists(fs::path(path.string() + ".tmp")));  // atomic: no temporary file stays
  R1_EXPECT(store.load().exists && store.load().text == "{\"a\":1}");
  R1_EXPECT(store.save("{\"a\":2}", error) && store.load().text == "{\"a\":2}");

  // A corrupt file is kept aside under a different name, never overwritten by a second one.
  {
    std::ofstream(path, std::ios::binary) << "garbage";
  }
  cz::Customization c = makeCustomization();
  cz::CustomizationStorage storage(c, store);
  const cz::LoadReport report = storage.load();
  R1_EXPECT(!report.ok && report.userKeptAside == path.string() + ".corrupt-1" && fs::exists(report.userKeptAside) && !fs::exists(path));
  {
    std::ofstream(path, std::ios::binary) << "garbage again";
  }
  R1_EXPECT(storage.load().userKeptAside == path.string() + ".corrupt-2");

  // Save from the storage and load into another instance.
  c.beginEditSession();
  c.hideEntry("file.save");
  c.commitSession();
  R1_EXPECT(fs::exists(path));
  cz::Customization other = makeCustomization();
  cz::CustomizationStorage storage2(other, store);
  R1_EXPECT(storage2.load().ok && !cz::findNode(other.editView().layout, "file.save")->visible);

  // A file over the size limit is refused without being read.
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.seekp(static_cast<std::streamoff>(cz::kMaxFileBytes) + 10);
    out.put('x');
  }
  const cz::TextLoad big = store.load();
  R1_EXPECT(big.exists && !big.error.empty() && big.text.empty());
  R1_EXPECT(!storage2.load().ok);
  // Saving into a directory that cannot be created reports instead of throwing.
  { std::ofstream(dir / "plain", std::ios::binary) << "x"; }
  cz::FileTextStore blocked(dir / "plain" / "inner.json");
  R1_EXPECT(!blocked.save("x", error) && !error.empty());
  fs::remove_all(dir, ec);
}

void testImportExport() {
  cz::Customization a = busy();
  const std::string exported = cz::exportCustomization(a);
  cz::Customization b = makeCustomization();
  b.hideEntry("file.save");
  R1_EXPECT(cz::importCustomization(b, exported, cz::ImportMode::Merge).ok);
  R1_EXPECT(!cz::findNode(b.editView().layout, "file.save")->visible && !cz::findNode(b.editView().layout, "edit.copy")->visible);  // merged
  R1_EXPECT(cz::importCustomization(b, exported, cz::ImportMode::Replace).ok);
  R1_EXPECT(cz::findNode(b.editView().layout, "file.save")->visible && b.userDelta() == a.userDelta());                              // replaced
  const cz::Delta before = b.userDelta();
  const uint64_t version = b.version();
  const cz::LoadReport bad = cz::importCustomization(b, "{\"format\":\"nope\"}", cz::ImportMode::Replace);
  R1_EXPECT(!bad.ok && !bad.error.empty() && b.userDelta() == before && b.version() == version);  // a rejected import changes nothing
}

void testLiveApply() {
  cz::Customization c = makeCustomization();
  int rebuilds = 0;
  c.subscribe([&] { ++rebuilds; });
  cz::MemoryTextStore store;
  cz::CustomizationStorage storage(c, store);
  c.hideEntry("edit.copy");
  c.addUserMenu("Mine");
  R1_EXPECT(rebuilds == 2);
  store.setContents(cz::exportDelta({}));
  storage.load();
  R1_EXPECT(rebuilds == 3 && c.userDelta().empty());  // loading is a live change too
  c.hideEntry("edit.copy");
  c.resetAll();
  R1_EXPECT(rebuilds == 5);
}

}  // namespace

int main() {
  testGolden();
  testRoundTrip();
  testWholeFileRejection();
  testEntryRepairs();
  testRandomGarbage();
  testStoresAndLayers();
  testFileStore();
  testImportExport();
  testLiveApply();
  return r1test::finish();
}
