// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the layout manager (slice 5.4) and the layout stores: startup and fallback, the
//   5 s debounced auto-save on a fake clock, suspension, named layouts, reset with confirmation,
//   application modes, import and export, damaged and hostile stored layouts, the file store.
// Why: these are the rules that decide whether a user ever loses an arrangement; they run headless
//   with an in-memory store, a controllable clock and injected write failures.
// Callers: CTest (label fast).
#include <filesystem>
#include <fstream>

#include "TestSupport.h"
#include "r1ui/dock/LayoutManager.h"

using namespace dock_test;

namespace {

class FakeClock final : public IClock {
 public:
  uint64_t nowMs() const override { return now; }
  uint64_t now = 1000;
};

// A stand-in for the DockHost: holds the "on screen" layout.
class FakeTarget final : public ILayoutTarget {
 public:
  FakeTarget() : layout_(make(1)) {}
  const DockLayout& currentLayout() const override { return layout_; }
  Status applyLayout(DockLayout layout) override {
    ++applied;
    if (failApply) return Status::failure("the host refused the layout");
    layout_ = std::move(layout);
    return Status::success();
  }
  std::vector<PanelInfo> panels() const override { return panelList; }
  DockConfig config() const override { return {}; }

  static DockLayout make(PanelId first, PanelId count = 6) {
    DockLayoutResult r = DockLayout::create(makePanels(count), {}, Node::stack({first}));
    return std::move(*r.layout);
  }
  void show(DockLayout layout) { layout_ = std::move(layout); }

  std::vector<PanelInfo> panelList = makePanels(6);
  int applied = 0;
  bool failApply = false;

 private:
  DockLayout layout_;
};

struct Rig {
  MemoryLayoutStore store;
  FakeClock clock;
  FakeTarget target;
  LayoutManager manager{store, clock, target};
  Rig() {
    manager.setDefaultProvider([] { return FakeTarget::make(1); });
  }
  PanelId front() const { return target.currentLayout().areas()[0].root->tabs[0]; }
};

std::string validLayoutText(PanelId first) { return FakeTarget::make(first).toJson(); }

void startup_uses_default_when_nothing_is_stored() {
  Rig rig;
  const LayoutReport report = rig.manager.startup();
  expect(report.ok && report.usedDefault && report.keptAsideAs.empty(), "default applied");
  expect(rig.target.applied == 1 && rig.front() == 1, "the target shows the default");
  expect(rig.store.exists("default", "_active"), "and it is stored as the active layout");
  expect(!rig.manager.dirty(), "clean after startup");
}

void startup_uses_the_stored_active_layout() {
  Rig rig;
  rig.store.write("default", "_active", validLayoutText(4));
  const LayoutReport report = rig.manager.startup();
  expect(report.ok && !report.usedDefault && rig.front() == 4, "stored layout used");
  Rig noDefault;
  noDefault.manager.setDefaultProvider({});
  const LayoutReport none = noDefault.manager.startup();
  expect(!none.ok && !none.error.empty() && noDefault.target.applied == 0, "no stored layout and no default: an error, nothing applied");
}

void damaged_layouts_are_kept_aside() {
  const std::vector<std::string> damaged = {
      "this is not json",
      "",
      R"({"version":3,"main":{"root":null},"floating":[]})",
      R"({"version":2,"main":{"root":null},"floating":[]})",  // nothing usable
      R"({"version":2,"main":{"root":{"type":"stack","weight":1,"tabs":[77],"active":0}},"floating":[]})",
      std::string(R"({"version":2,"main":{"root":)") + std::string(300, '['),
  };
  for (const std::string& bad : damaged) {
    Rig rig;
    rig.store.write("default", "_active", bad);
    const LayoutReport report = rig.manager.startup();
    expect(report.ok && report.usedDefault, "default used");
    expect(report.keptAsideAs == "_corrupt-1", "kept aside");
    expect(rig.store.read("default", "_corrupt-1") == bad, "the damaged text is preserved verbatim");
    expect(rig.store.read("default", "_active").value_or("") != bad, "the active layout is now the default");
    expect(!report.warnings.empty(), "the reason is reported");
  }
  Rig twice;
  twice.store.write("default", "_active", "bad one");
  twice.manager.startup();
  twice.store.write("default", "_active", "bad two");
  const LayoutReport second = twice.manager.startup();
  expect(second.keptAsideAs == "_corrupt-2" && twice.store.read("default", "_corrupt-1") == "bad one", "the first kept layout is not overwritten");
  expect(twice.manager.list().empty(), "kept-aside layouts are hidden from the user list");

  // The host refusing the stored layout is treated the same way.
  Rig refusing;
  refusing.store.write("default", "_active", validLayoutText(3));
  refusing.target.failApply = true;
  const LayoutReport refused = refusing.manager.startup();
  expect(!refused.ok && refusing.store.exists("default", "_corrupt-1"), "an apply failure keeps the file aside and reports");
}

void unpreservable_damage_blocks_autosave() {
  Rig rig;
  rig.store.write("default", "_active", "broken");
  rig.store.setFailWrites(true);
  const LayoutReport report = rig.manager.startup();
  expect(report.ok && report.usedDefault && report.keptAsideAs.empty(), "default applied, nothing could be kept aside");
  rig.store.setFailWrites(false);
  rig.manager.notifyChanged();
  rig.clock.now += 6000;
  rig.manager.tick();
  expect(rig.store.read("default", "_active") == "broken", "the damaged layout is never overwritten by auto-save");
  expect(!rig.manager.flush(true).ok, "not even by a forced flush");
  rig.store.write("default", "mine", validLayoutText(5));
  expect(rig.manager.load("mine").ok, "an explicit load lifts the protection");
  expect(rig.store.read("default", "_active") != std::optional<std::string>("broken"), "and the active layout is written");
}

void autosave_is_debounced() {
  Rig rig;
  rig.manager.startup();
  const size_t base = rig.store.writeCount();
  rig.manager.notifyChanged();
  expect(rig.manager.dirty() && rig.manager.msUntilSave() == 5000, "scheduled 5 s ahead");
  rig.clock.now += 4999;
  rig.manager.tick();
  expect(rig.store.writeCount() == base, "nothing written at 4999 ms");
  rig.clock.now += 1;
  rig.manager.tick();
  expect(rig.store.writeCount() == base + 1 && !rig.manager.dirty(), "written at 5000 ms");
  rig.manager.tick();
  expect(rig.store.writeCount() == base + 1 && !rig.manager.msUntilSave().has_value(), "nothing more to do");

  // A continuous drag: every change restarts the countdown, so there is one save, 5 s after the last.
  for (int i = 0; i < 10; ++i) {
    rig.manager.notifyChanged();
    rig.clock.now += 1000;
    rig.manager.tick();
  }
  expect(rig.store.writeCount() == base + 1, "ten changes a second apart: still no save");
  rig.clock.now += 4000;
  rig.manager.tick();
  expect(rig.store.writeCount() == base + 2, "one save 5 s after the last change");
}

void autosave_suspend_flush_and_failure() {
  Rig rig;
  rig.manager.startup();
  const size_t base = rig.store.writeCount();
  rig.manager.setSuspended(true);
  rig.manager.notifyChanged();
  rig.clock.now += 60000;
  rig.manager.tick();
  expect(rig.store.writeCount() == base && !rig.manager.msUntilSave().has_value(), "suspended (tab drag): nothing is saved");
  rig.manager.setSuspended(false);
  expect(rig.manager.dirty() && rig.manager.msUntilSave() == 5000, "the countdown starts when the drag ends");
  rig.manager.setSuspended(false);
  rig.manager.setSuspended(true);
  rig.manager.setSuspended(false);
  expect(rig.manager.msUntilSave() == 5000, "suspend without a change does not reschedule");

  rig.target.show(FakeTarget::make(2));
  expect(rig.manager.flush().ok && rig.store.read("default", "_active") == rig.target.currentLayout().toJson(), "flush writes at once (application exit)");
  expect(!rig.manager.dirty() && rig.manager.flush().ok && rig.store.writeCount() == base + 1, "flushing a clean layout writes nothing");
  expect(rig.manager.flush(true).ok && rig.store.writeCount() == base + 2, "forced flush always writes");

  rig.store.setFailWrites(true);
  rig.manager.notifyChanged();
  rig.clock.now += 5000;
  rig.manager.tick();
  expect(rig.manager.dirty() && !rig.manager.lastError().empty(), "a failed write keeps the layout dirty");
  expect(rig.manager.msUntilSave() == 5000, "and retries one delay later");
  rig.store.setFailWrites(false);
  rig.clock.now += 5000;
  rig.manager.tick();
  expect(!rig.manager.dirty() && rig.manager.lastError().empty(), "the retry succeeds");
  expect(rig.manager.flush(true).ok, "flush works again");
}

void named_layouts() {
  Rig rig;
  rig.manager.startup();
  rig.target.show(FakeTarget::make(2));
  std::string key;
  expect(rig.manager.saveAs("My Layout", "for modelling", &key).ok && key == "My Layout", "save as");
  expect(!rig.manager.saveAs("My Layout", "", nullptr).ok, "an existing name is refused");
  expect(!rig.manager.saveAs("", "", nullptr).ok && !rig.manager.saveAs("   ", "", nullptr).ok, "empty names refused");
  expect(rig.manager.saveAs("CON", "", &key).ok && key == "layout-1", "a device name cannot be a file name: the layout gets a numbered key");
  expect(rig.manager.saveAs("a/b\\c:d", "", &key).ok && key == "a_b_c_d", "illegal characters become underscores");
  expect(rig.manager.saveAs("../../evil", "", &key).ok && key == "evil", "a traversal attempt becomes a plain name");
  expect(rig.manager.saveAs("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "", &key).ok && key == "layout-2", "a name without file-name characters gets a numbered key");
  expect(rig.manager.saveAs(std::string(1000, 'x'), "", &key).ok && key.size() == 64, "an overlong name is cut");
  std::vector<LayoutSummary> list = rig.manager.list();
  expect(list.size() == 6, "six layouts listed");
  bool found = false;
  for (const LayoutSummary& s : list) {
    if (s.key == "My Layout") found = s.displayName == "My Layout" && s.description == "for modelling" && !s.active;
    if (s.key == "layout-2") expect(s.displayName == "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "the display keeps the original script");
  }
  expect(found, "display name and description listed");

  rig.target.show(FakeTarget::make(5));
  expect(rig.manager.save("My Layout").ok, "overwrite");
  expect(!rig.manager.save("nothing").ok && !rig.manager.save("_active").ok && !rig.manager.save("../x").ok, "overwrite needs an existing user layout");
  rig.target.show(FakeTarget::make(3));
  const LayoutReport loaded = rig.manager.load("My Layout");
  expect(loaded.ok && rig.front() == 5, "load shows the overwritten arrangement");
  expect(rig.manager.activeKey() == "My Layout", "and it becomes the active layout");
  for (const LayoutSummary& s : rig.manager.list()) expect(s.active == (s.key == "My Layout"), "only the loaded layout is flagged");

  std::string renamed;
  expect(rig.manager.rename("My Layout", "Sculpt Layout", &renamed).ok && renamed == "Sculpt Layout", "rename");
  expect(!rig.store.exists("default", "My Layout") && rig.manager.activeKey() == "Sculpt Layout", "old key gone, active follows");
  expect(!rig.manager.rename("Sculpt Layout", "a_b_c_d").ok, "rename onto an existing layout refused");
  expect(!rig.manager.rename("missing", "x").ok && !rig.manager.rename("Sculpt Layout", "").ok, "rename needs a layout and a name");
  std::string copy;
  expect(rig.manager.duplicate("Sculpt Layout", "Sculpt Copy", false, &copy).ok && copy == "Sculpt Copy", "duplicate");
  bool copyOk = false;
  for (const LayoutSummary& s : rig.manager.list()) {
    if (s.key == "Sculpt Copy") copyOk = s.description.empty() && s.displayName == "Sculpt Copy";
  }
  expect(copyOk, "a copy does not carry the original description (spec 04 rule 25)");
  expect(rig.manager.duplicate("Sculpt Layout", "Sculpt Copy 2", true).ok, "keeping the description is possible");
  expect(rig.manager.remove("Sculpt Copy").ok && !rig.manager.remove("Sculpt Copy").ok && !rig.manager.remove("_active").ok, "remove");
  expect(rig.manager.remove("Sculpt Layout").ok && rig.manager.activeKey().empty(), "removing the active layout clears the flag");
}

void load_failures_change_nothing() {
  Rig rig;
  rig.manager.startup();
  rig.target.show(FakeTarget::make(2));
  const std::string before = rig.target.currentLayout().toJson();
  const int applied = rig.target.applied;
  rig.store.write("default", "broken", "{{{");
  rig.store.write("default", "future", R"({"version":9,"main":{"root":null},"floating":[]})");
  rig.store.write("default", "empty", R"({"version":2,"main":{"root":null},"floating":[]})");
  for (const char* key : {"broken", "future", "empty", "missing", "_active", "../x"}) {
    const LayoutReport report = rig.manager.load(key);
    expect(!report.ok && !report.error.empty(), "load refused");
  }
  expect(rig.target.applied == applied && rig.target.currentLayout().toJson() == before, "the current layout is untouched");
  rig.store.write("default", "ok", validLayoutText(4));
  rig.target.failApply = true;
  expect(!rig.manager.load("ok").ok && rig.manager.activeKey().empty(), "a host that refuses leaves the manager state alone");
  rig.target.failApply = false;
  // Unknown panels are dropped and reported, new panels appear at their suggested position.
  rig.target.panelList = makePanels(6);
  rig.store.write("default", "wide",
                  R"({"version":2,"main":{"root":{"type":"stack","weight":1,"tabs":[1,50,2],"active":0}},"floating":[],"panels":[{"id":1,"locked":false},{"id":2,"locked":false},{"id":50,"locked":false}]})");
  rig.target.panelList[2].suggested = {1, true, Side::Right};
  const LayoutReport wide = rig.manager.load("wide");
  expect(wide.ok && wide.droppedPanels == 1 && wide.newPanelsPlaced == 1, "unknown dropped, new placed");
  expect(rig.target.currentLayout().areas()[0].root->tabs == std::vector<PanelId>({1, 2, 3}), "panel 3 joins the stack of 1");
}

void reset_to_default() {
  Rig rig;
  rig.manager.startup();
  rig.target.show(FakeTarget::make(4));
  std::string asked;
  auto no = [&](std::string_view q) {
    asked = std::string(q);
    return false;
  };
  LayoutManager::ResetOutcome out = rig.manager.resetToDefault(no);
  expect(out.cancelled && !out.done && rig.front() == 4 && !asked.empty(), "declined: nothing changes, the question was asked");
  out = rig.manager.resetToDefault({});
  expect(out.cancelled && rig.front() == 4, "no confirmation callback counts as declined");
  out = rig.manager.resetToDefault([](std::string_view) { return true; });
  expect(out.done && !out.cancelled && rig.front() == 1, "confirmed: the default is applied");
  expect(rig.manager.activeKey().empty(), "no named layout is active after a reset");
  Rig bare;
  bare.manager.setDefaultProvider({});
  expect(!bare.manager.resetToDefault([](std::string_view) { return true; }).error.empty(), "no default available is an error");
  rig.target.failApply = true;
  expect(!rig.manager.resetToDefault([](std::string_view) { return true; }).done, "a host failure is reported");
}

void modes_have_separate_layouts() {
  Rig rig;
  rig.manager.startup();
  rig.target.show(FakeTarget::make(2));
  rig.manager.notifyChanged();
  expect(rig.manager.setMode("asset editor").ok, "switch mode");
  expect(rig.store.read("default", "_active") == FakeTarget::make(2).toJson(), "the pending change of the old mode was saved first");
  expect(rig.manager.list().empty() && rig.manager.mode() == "asset editor", "the new mode has its own layouts");
  rig.manager.setDefaultProvider([] { return FakeTarget::make(6); });
  rig.manager.startup();
  expect(rig.front() == 6, "the new mode starts from its own default");
  rig.manager.saveAs("Only here", "", nullptr);
  expect(rig.manager.list().size() == 1 && rig.manager.setMode("default").ok && rig.manager.list().empty(), "named layouts belong to one mode");
  expect(!rig.manager.setMode("").ok && !rig.manager.setMode("../up").ok && !rig.manager.setMode("_hidden").ok && !rig.manager.setMode("NUL").ok,
         "illegal modes refused");
  expect(rig.manager.mode() == "default", "and the mode did not change");
  rig.store.setFailWrites(true);
  rig.manager.notifyChanged();
  expect(!rig.manager.setMode("other").ok && rig.manager.mode() == "default", "a mode switch that cannot save the old layout is refused");
}

void import_and_export() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "r1ui_dock_manager_test";
  fs::remove_all(dir);
  fs::create_directories(dir);

  Rig rig;
  rig.manager.startup();
  rig.target.show(FakeTarget::make(3));
  expect(rig.manager.exportFile(dir / "mine.layout.json", {}, "Exported", "desc").ok, "export the current arrangement");
  expect(!rig.manager.exportFile({}).ok, "no file chosen is an error");
  const LayoutReport imported = rig.manager.importFile(dir / "mine.layout.json");
  expect(imported.ok && imported.key == "mine", "import takes the file stem as the name");
  expect(rig.store.exists("default", "mine") && rig.manager.load("mine").ok && rig.front() == 3, "and the imported layout loads");
  expect(!rig.manager.importFile(dir / "mine.layout.json").ok, "importing again onto an existing name is refused");
  expect(rig.manager.importFile(dir / "mine.layout.json", true).ok, "unless overwrite is asked for");

  // A rejected import keeps everything as it was.
  {
    std::ofstream(dir / "bad.layout.json") << "not a layout";
    std::ofstream(dir / "future.json") << R"({"version":7,"main":{"root":null},"floating":[]})";
  }
  const std::string before = rig.target.currentLayout().toJson();
  const size_t stored = rig.store.list("default").size();
  for (const char* name : {"bad.layout.json", "future.json", "missing.json"}) {
    const LayoutReport r = rig.manager.importFile(dir / name);
    expect(!r.ok && !r.error.empty(), "invalid import rejected with a reason");
  }
  expect(rig.store.list("default").size() == stored && rig.target.currentLayout().toJson() == before, "a rejected import changes nothing");
  expect(!rig.manager.importText("x", std::string(5 * 1024 * 1024, ' ')).ok, "an oversized text is refused");
  expect(!rig.manager.importText("..", "{}").ok && !rig.manager.importText("", validLayoutText(1)).ok, "unusable names refused");

  // Exporting a stored layout copies it; the exported file is a valid layout.
  expect(rig.manager.exportFile(dir / "copy.json", "mine").ok, "export a stored layout");
  expect(!rig.manager.exportFile(dir / "copy2.json", "nope").ok, "a missing layout cannot be exported");
  const auto text = readBoundedFile(dir / "copy.json");
  expect(text && DockLayout::fromJson(*text, makePanels(6)).ok(), "the exported file loads");

  // The file store refuses to import onto itself.
  FileLayoutStore files(dir / "store");
  FakeClock clock;
  FakeTarget target;
  LayoutManager onDisk(files, clock, target);
  target.show(FakeTarget::make(2));
  std::string key;
  expect(onDisk.saveAs("Own", "", &key).ok, "save to disk");
  const LayoutReport self = onDisk.importFile(dir / "store" / "default" / "Own.layout.json");
  expect(!self.ok && self.error.find("itself") != std::string::npos, "importing a layout onto itself is refused");
  fs::remove_all(dir);
}

void file_store() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "r1ui_dock_store_test";
  fs::remove_all(dir);
  FileLayoutStore store(dir);
  expect(store.list("m").empty() && !store.read("m", "a").has_value() && !store.exists("m", "a"), "empty store");
  expect(store.write("m", "b", "two").ok && store.write("m", "a", "one").ok, "write");
  expect(store.list("m") == std::vector<std::string>({"a", "b"}), "sorted keys");
  expect(store.read("m", "a") == "one", "read back");
  expect(store.write("m", "a", "uno").ok && store.read("m", "a") == "uno", "replace");
  expect(!fs::exists(dir / "m" / "a.layout.json.tmp"), "no temporary file is left behind");
  expect(store.rename("m", "a", "c").ok && !store.exists("m", "a") && store.exists("m", "c"), "rename");
  expect(!store.rename("m", "c", "b").ok && !store.rename("m", "zz", "y").ok, "rename onto existing or missing refused");
  expect(store.remove("m", "c").ok && !store.remove("m", "c").ok, "remove");
  for (const char* bad : {"../x", "a/b", "a\\b", "", "CON", "nul.txt", "COM1", ".hidden", "trail.", "sp ace ", "a..b", "a:b", "x*"}) {
    expect(!keyProblem(bad).empty(), "bad key rejected");
    expect(!store.write("m", bad, "x").ok && !store.read("m", bad).has_value(), "bad key never reaches a path");
  }
  expect(!store.write("../escape", "k", "x").ok, "bad scope refused");
  expect(keyProblem("Good name (2)").empty() && keyProblem("_active").empty(), "legal keys");
  expect(keyFromName("  A  B  ") == "A  B" && keyFromName("***").empty() && keyFromName("_x_") == "x", "keyFromName");
  {
    std::ofstream big(dir / "m" / "huge.layout.json", std::ios::binary);
    big << std::string(kMaxLayoutBytes + 1, 'x');
  }
  expect(!store.read("m", "huge").has_value(), "an oversized file reads as missing");
  expect(!store.write("m", "toolarge", std::string(kMaxLayoutBytes + 1, 'x')).ok, "an oversized write is refused");
  fs::remove_all(dir);
  MemoryLayoutStore memory;
  expect(memory.write("a", "k", "v").ok && memory.rename("a", "k", "j").ok && memory.read("a", "j") == "v", "memory store behaves alike");
  expect(!memory.write("a", "../k", "v").ok, "memory store validates keys too");
}

void monitors_on_load() {
  Rig rig;
  rig.manager.startup();
  MonitorSet set;
  set.monitors.push_back({"A", {0, 0, 1920, 1080}, {0, 0, 1920, 1040}, 1.0});
  rig.manager.setMonitorProvider([set] { return set; });
  rig.store.write("default", "away",
                  R"({"version":2,"main":{"root":{"type":"stack","weight":1,"tabs":[1],"active":0}},"floating":[{"rect":{"x":7000,"y":10,"w":400,"h":300},"root":{"type":"stack","weight":1,"tabs":[2],"active":0}}]})");
  const LayoutReport report = rig.manager.load("away");
  expect(report.ok && report.windowsMoved == 1, "the unreachable window is brought back");
  expect(rig.target.currentLayout().areas()[1].rect.x == 760, "centred on the work area");
}

}  // namespace

int main() {
  runCase("startup_uses_default_when_nothing_is_stored", startup_uses_default_when_nothing_is_stored);
  runCase("startup_uses_the_stored_active_layout", startup_uses_the_stored_active_layout);
  runCase("damaged_layouts_are_kept_aside", damaged_layouts_are_kept_aside);
  runCase("unpreservable_damage_blocks_autosave", unpreservable_damage_blocks_autosave);
  runCase("autosave_is_debounced", autosave_is_debounced);
  runCase("autosave_suspend_flush_and_failure", autosave_suspend_flush_and_failure);
  runCase("named_layouts", named_layouts);
  runCase("load_failures_change_nothing", load_failures_change_nothing);
  runCase("reset_to_default", reset_to_default);
  runCase("modes_have_separate_layouts", modes_have_separate_layouts);
  runCase("import_and_export", import_and_export);
  runCase("file_store", file_store);
  runCase("monitors_on_load", monitors_on_load);
  return finish("manager_test");
}
