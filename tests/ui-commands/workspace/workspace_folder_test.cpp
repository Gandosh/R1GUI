// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tests of WorkspaceFolder on a real directory: key rules (hostile names, device names, path
//   tricks), save with both modes and name collisions, list (sorted, damaged files shown but flagged,
//   foreign files ignored, oversize files unread), load, remove, and that no key can leave the folder.
// Callers: CTest (label fast).
#include "../custommenu/MenuFixtures.h"
#include "r1ui/commands/workspace/WorkspaceFolder.h"

namespace {

using namespace r1test;
namespace ws = r1ui::commands::workspace;

ws::Workspace named(const std::string& name, const std::string& layoutText = "{\"v\":1}") {
  ws::Workspace w;
  w.name = name;
  w.layout = layoutText;
  return w;
}

void testKeys() {
  for (const char* good : {"a", "Sculpting", "My Setup (2)", "v1.2-final_x", "a.b"}) R1_EXPECT(ws::isValidWorkspaceKey(good));
  for (const char* bad : {"", " a", "a ", ".a", "a.", "a..b", "../x", "a/b", "a\\b", "a:b", "CON", "con", "nul.txt", "COM1", "lpt9", "a*b", "a?b", "a\"b"}) R1_EXPECT(!ws::isValidWorkspaceKey(bad));
  R1_EXPECT(!ws::isValidWorkspaceKey(std::string(65, 'a')) && ws::isValidWorkspaceKey(std::string(64, 'a')));
  R1_EXPECT(!ws::isValidWorkspaceKey(std::string("a\0b", 3)) && !ws::isValidWorkspaceKey("caf\xC3\xA9"));
  // Whatever the name, the derived key is valid.
  for (const std::string& name : {std::string("Sculpting"), std::string("\xE2\x82\xAC\xE2\x82\xAC"), std::string("..\\..\\evil"), std::string("CON"), std::string("  . . "),
                                  std::string(500, 'q'), std::string("a:b*c?"), std::string("PRN.txt"), std::string("....")}) {
    const std::string key = ws::workspaceKeyFor(name);
    R1_EXPECT(ws::isValidWorkspaceKey(key));
  }
  R1_EXPECT(ws::workspaceKeyFor("Sculpting") == "Sculpting" && ws::workspaceKeyFor("\xE2\x82\xAC") == "workspace");
  for (uint64_t i = 0; i < 500; ++i) {
    Rng rng(i + 3);
    R1_EXPECT(ws::isValidWorkspaceKey(ws::workspaceKeyFor(randomGarbage(rng, 1 + rng.below(80)))));
  }
}

void testSaveListLoadRemove() {
  TempDir dir("wsfolder");
  ws::WorkspaceFolder folder(dir.path() / "workspaces");  // does not exist yet
  R1_EXPECT(folder.list().empty());
  const auto a = folder.save(named("Sculpting"));
  R1_EXPECT(a.ok && a.key == "Sculpting" && folder.exists("Sculpting"));
  R1_EXPECT(std::filesystem::exists(dir.path() / "workspaces" / "Sculpting.r1ws"));
  // NewOnly never overwrites: the same name gets a numbered key.
  const auto b = folder.save(named("Sculpting", "{\"v\":2}"));
  R1_EXPECT(b.ok && b.key == "Sculpting (2)");
  const auto c = folder.save(named("Sculpting", "{\"v\":3}"));
  R1_EXPECT(c.ok && c.key == "Sculpting (3)");
  R1_EXPECT(*folder.load("Sculpting").workspace.layout == "{\"v\":1}");
  // Overwrite replaces the file of that key and creates no extra file.
  const auto o = folder.save(named("Sculpting", "{\"v\":9}"), ws::SaveMode::Overwrite);
  R1_EXPECT(o.ok && o.key == "Sculpting" && *folder.load("Sculpting").workspace.layout == "{\"v\":9}");
  R1_EXPECT(countFiles(dir.path() / "workspaces") == 3);
  // A refused save (bad part) writes nothing and keeps the old file.
  ws::Workspace bad = named("Sculpting");
  bad.menus = "[";
  R1_EXPECT(!folder.save(bad, ws::SaveMode::Overwrite).ok && *folder.load("Sculpting").workspace.layout == "{\"v\":9}");
  R1_EXPECT(!folder.save(named("   ")).ok);
  folder.save(named("alpha"));
  folder.save(named("Zebra"));
  folder.save(named("\xE2\x82\xAC only"));  // key "only", name keeps the euro sign

  // Foreign and damaged files.
  spit(dir.path() / "workspaces" / "notes.txt", "hello");
  spit(dir.path() / "workspaces" / "broken.r1ws", "{ not json");
  spit(dir.path() / "workspaces" / "bad name!.r1ws", "{}");
  const auto list = folder.list();
  std::vector<std::string> keys;
  for (const auto& info : list) keys.push_back(info.key);
  R1_EXPECT(std::find(keys.begin(), keys.end(), "notes") == keys.end() && std::find(keys.begin(), keys.end(), "bad name!") == keys.end());
  R1_EXPECT(list.size() == 7);
  size_t invalid = 0;
  for (const auto& info : list) {
    if (!info.valid) {
      ++invalid;
      R1_EXPECT(info.key == "broken" && !info.error.empty() && info.name.empty());
    } else {
      R1_EXPECT(!info.name.empty() && info.sizeBytes > 0);
    }
  }
  R1_EXPECT(invalid == 1);
  // Sorted by name, case-insensitively; the damaged file sorts by its key.
  std::vector<std::string> names;
  for (const auto& info : list) names.push_back(info.valid ? info.name : info.key);
  R1_EXPECT(names.front() == "alpha" && names.back() == "\xE2\x82\xAC only");

  // Removal.
  std::string error;
  R1_EXPECT(folder.remove("broken", error) && !folder.exists("broken"));
  R1_EXPECT(!folder.remove("broken", error) && !error.empty());
  R1_EXPECT(!folder.remove("../outside", error) && !folder.remove("a/b", error) && !folder.remove("", error));
  R1_EXPECT(folder.remove("Sculpting (2)", error) && folder.list().size() == 5);
  R1_EXPECT(!folder.load("missing").ok && !folder.load("../x").ok && !folder.load("").ok);
}

void testEscapeAttempts() {
  TempDir dir("wsescape");
  spit(dir.path() / "victim.r1ws", "{\"keep\":true}");
  ws::WorkspaceFolder folder(dir.path() / "workspaces");
  std::filesystem::create_directories(dir.path() / "workspaces");
  std::string error;
  R1_EXPECT(!folder.remove("..\\victim", error) && !folder.remove("../victim", error) && !folder.remove("..", error));
  R1_EXPECT(!folder.load("../victim").ok);
  R1_EXPECT(std::filesystem::exists(dir.path() / "victim.r1ws"));
  const auto r = folder.save(named("../victim"));
  R1_EXPECT(r.ok && std::filesystem::exists(dir.path() / "workspaces" / (r.key + ".r1ws")) && slurp(dir.path() / "victim.r1ws") == "{\"keep\":true}");
  // An oversize file is listed as invalid without being read.
  spit(dir.path() / "workspaces" / "huge.r1ws", std::string(ws::kMaxWorkspaceBytes + 1, ' '));
  bool sawHuge = false;
  for (const auto& info : folder.list()) {
    if (info.key == "huge") {
      sawHuge = !info.valid && info.error.find("larger") != std::string::npos;
    }
  }
  R1_EXPECT(sawHuge);
}

}  // namespace

int main() {
  testKeys();
  testSaveListLoadRemove();
  testEscapeAttempts();
  return r1test::finish();
}
