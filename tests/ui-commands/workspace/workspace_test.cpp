// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tests of the .r1ws workspace container: golden text, round trip with the four parts, optional
//   parts, canonical re-serialisation (byte-stable save/load/save, order and numbers preserved), real
//   parts from the other modules (a custom menu set, a keybinding export), whole-file rejections
//   (format, version, name, part types, size, depth, duplicate keys, invalid UTF-8), mutated and random
//   garbage, atomic file writes on a real directory.
// Callers: CTest (label fast).
#include "../custommenu/MenuFixtures.h"

#include "r1ui/commands/custommenu/CustomMenuIo.h"
#include "r1ui/commands/workspace/Workspace.h"

namespace {

using namespace r1test;
namespace ws = r1ui::commands::workspace;

ws::Workspace sample() {
  ws::Workspace w;
  w.name = "Sculpting";
  w.layout = "{ \"version\": 2, \"main\": {\"root\": null}, \"x\": [1, 2.5, -3e2, true, null, \"s\\u00e9\"] }";
  w.menus = "{\"format\":\"r1ui-custom-menus\",\"version\":1,\"menus\":[]}";
  w.customization = "{\"format\":\"r1ui-customization\",\"version\":1}";
  w.keybindings = "{\"format\":\"r1ui-keybindings\",\"version\":1,\"overrides\":[]}";
  return w;
}

void testGoldenAndRoundTrip() {
  std::string text;
  std::string error;
  R1_EXPECT(ws::exportWorkspace(sample(), text, error));
  const std::string golden =
      "{\"format\":\"r1ui-workspace\",\"version\":1,\"name\":\"Sculpting\","
      "\"layout\":{\"version\":2,\"main\":{\"root\":null},\"x\":[1,2.5,-300,true,null,\"s\xC3\xA9\"]},"
      "\"menus\":{\"format\":\"r1ui-custom-menus\",\"version\":1,\"menus\":[]},"
      "\"customization\":{\"format\":\"r1ui-customization\",\"version\":1},"
      "\"keybindings\":{\"format\":\"r1ui-keybindings\",\"version\":1,\"overrides\":[]}}\n";
  R1_EXPECT(text == golden);
  const ws::WorkspaceParseResult parsed = ws::parseWorkspace(text);
  R1_EXPECT(parsed.ok && parsed.workspace.name == "Sculpting");
  R1_EXPECT(parsed.workspace.layout && *parsed.workspace.layout == "{\"version\":2,\"main\":{\"root\":null},\"x\":[1,2.5,-300,true,null,\"s\xC3\xA9\"]}");
  // Canonical text is stable: exporting what was parsed gives the same file.
  std::string again;
  R1_EXPECT(ws::exportWorkspace(parsed.workspace, again, error) && again == text);
  R1_EXPECT(ws::parseWorkspace(again).workspace == parsed.workspace);

  // Optional parts stay absent.
  ws::Workspace minimal;
  minimal.name = "Only name";
  R1_EXPECT(ws::exportWorkspace(minimal, text, error));
  const auto m = ws::parseWorkspace(text);
  R1_EXPECT(m.ok && !m.workspace.layout && !m.workspace.menus && !m.workspace.customization && !m.workspace.keybindings);
  ws::Workspace layoutOnly = minimal;
  layoutOnly.layout = "{}";
  R1_EXPECT(ws::exportWorkspace(layoutOnly, text, error) && ws::parseWorkspace(text).workspace.layout == std::optional<std::string>("{}"));

  // Number fidelity: shortest round trip survives.
  ws::Workspace numbers = minimal;
  numbers.layout = "{\"a\":0.1,\"b\":1e-7,\"c\":123456789012,\"d\":-0.5}";
  R1_EXPECT(ws::exportWorkspace(numbers, text, error));
  const auto n = ws::parseWorkspace(text);
  R1_EXPECT(n.ok && ws::exportWorkspace(n.workspace, again, error) && again == text);
}

void testRealParts() {
  // The real producers' texts fit the container and come back as equal JSON.
  cm::CustomMenuSet set;
  set.createMenu(cm::MenuKind::Pie, "Pie");
  set.setSlot("menu.1", 0, "tool.move");
  ws::Workspace w;
  w.name = "Real";
  w.menus = cm::exportSet(set);
  w.layout = "{\"version\":2,\"name\":\"L\"}";
  std::string text;
  std::string error;
  R1_EXPECT(ws::exportWorkspace(w, text, error));
  const auto back = ws::parseWorkspace(text);
  R1_EXPECT(back.ok && back.workspace.menus);
  // The menus part is still a valid custom menu store with the same content.
  const cm::SetParseResult menus = cm::parseSet(*back.workspace.menus);
  R1_EXPECT(menus.ok && menus.menus.size() == 1 && menus.menus[0].entries[0].commandId == "tool.move");
}

void testRejections() {
  std::string text;
  std::string error;
  ws::Workspace w = sample();
  w.name = "   ";
  R1_EXPECT(!ws::exportWorkspace(w, text, error) && error.find("name") != std::string::npos);
  w = sample();
  w.layout = "not json";
  R1_EXPECT(!ws::exportWorkspace(w, text, error) && error.find("layout") != std::string::npos);
  w = sample();
  w.menus = "[1,2]";
  R1_EXPECT(!ws::exportWorkspace(w, text, error) && error.find("object") != std::string::npos);
  w = sample();
  w.keybindings = "{\"a\":1,\"a\":2}";
  R1_EXPECT(!ws::exportWorkspace(w, text, error));  // duplicate keys
  w = sample();
  w.customization = "{\"a\":\"\xff\"}";
  R1_EXPECT(!ws::exportWorkspace(w, text, error));  // invalid UTF-8
  w = sample();
  w.layout = std::string(ws::kMaxPartDepth + 5, '[') + std::string(ws::kMaxPartDepth + 5, ']');
  R1_EXPECT(!ws::exportWorkspace(w, text, error));  // too deep (and not an object)
  w.layout = "";
  for (size_t i = 0; i < ws::kMaxPartDepth + 5; ++i) w.layout = "{\"a\":" + *w.layout + "}";
  R1_EXPECT(!ws::exportWorkspace(w, text, error));
  std::string deep = "1";
  for (size_t i = 0; i < ws::kMaxPartDepth + 5; ++i) deep = "{\"a\":" + deep + "}";
  w.layout = deep;
  R1_EXPECT(!ws::exportWorkspace(w, text, error));
  deep = "1";
  for (size_t i = 0; i < ws::kMaxPartDepth - 4; ++i) deep = "{\"a\":" + deep + "}";
  w.layout = deep;
  R1_EXPECT(ws::exportWorkspace(w, text, error));  // deep but within the limit
  R1_EXPECT(ws::parseWorkspace(text).ok);
  // A part over the size limit.
  w = sample();
  text = "sentinel";
  w.layout = "{\"pad\":\"" + std::string(ws::kMaxPartBytes, 'x') + "\"}";
  R1_EXPECT(!ws::exportWorkspace(w, text, error));
  R1_EXPECT(text == "sentinel");  // a refusal leaves the output alone

  const std::string head = "{\"format\":\"r1ui-workspace\",\"version\":1,";
  R1_EXPECT(!ws::parseWorkspace("").ok && !ws::parseWorkspace("[]").ok && !ws::parseWorkspace("{}").ok);
  R1_EXPECT(!ws::parseWorkspace("{\"format\":\"r1ui-customization\",\"version\":1,\"name\":\"x\"}").ok);
  R1_EXPECT(!ws::parseWorkspace("{\"format\":\"r1ui-workspace\",\"name\":\"x\"}").ok);
  R1_EXPECT(!ws::parseWorkspace("{\"format\":\"r1ui-workspace\",\"version\":9,\"name\":\"x\"}").ok);
  R1_EXPECT(!ws::parseWorkspace(head + "\"name\":5}").ok);
  R1_EXPECT(!ws::parseWorkspace(head + "\"name\":\"\"}").ok);
  R1_EXPECT(!ws::parseWorkspace(head + "\"name\":\"x\",\"layout\":[1]}").ok);
  R1_EXPECT(!ws::parseWorkspace(head + "\"name\":\"x\",\"layout\":\"{}\"}").ok);  // a string is not an embedded object
  R1_EXPECT(!ws::parseWorkspace(head + "\"name\":\"x\",\"menus\":null}").ok);
  R1_EXPECT(ws::parseWorkspace(head + "\"name\":\"x\",\"unknown\":[1,2,3]}").ok);  // unknown members are ignored
  R1_EXPECT(!ws::parseWorkspace(std::string(ws::kMaxWorkspaceBytes + 1, ' ')).ok);
  R1_EXPECT(!ws::parseWorkspace(std::string(5000, '{')).ok);
  R1_EXPECT(!ws::parseWorkspace(head + "\"name\":\"x\xff\"}").ok);
}

void testFuzz() {
  std::string seed;
  std::string error;
  R1_EXPECT(ws::exportWorkspace(sample(), seed, error));
  size_t accepted = 0;
  for (uint64_t i = 0; i < 4000; ++i) {
    Rng rng(i + 1);
    std::string t = seed;
    for (size_t k = 0, n = 1 + rng.below(4); k < n; ++k) t = mutate(t, rng);
    const auto r = ws::parseWorkspace(t);
    if (!r.ok) {
      R1_EXPECT(!r.error.empty());
      continue;
    }
    ++accepted;
    // Whatever was accepted exports again and parses back to the same value.
    std::string out;
    std::string why;
    if (!ws::exportWorkspace(r.workspace, out, why) || ws::parseWorkspace(out).workspace != r.workspace) {
      std::fprintf(stderr, "accepted workspace does not round trip (seed %llu)\n", static_cast<unsigned long long>(i));
      R1_EXPECT(false);
    }
  }
  R1_EXPECT(accepted > 50);
  for (uint64_t i = 0; i < 1000; ++i) {
    Rng rng(777 + i);
    R1_EXPECT(!ws::parseWorkspace(randomGarbage(rng, rng.below(300))).ok);
  }
}

void testFiles() {
  TempDir dir("r1ws");
  const auto path = dir.path() / "setup.r1ws";
  std::string error;
  R1_EXPECT(ws::saveWorkspaceFile(sample(), path, error));
  R1_EXPECT(countFiles(dir.path()) == 1);
  const auto loaded = ws::loadWorkspaceFile(path);
  R1_EXPECT(loaded.ok && loaded.workspace.name == "Sculpting" && loaded.workspace.keybindings);
  const std::string firstBytes = slurp(path);
  // A refused save (bad part) leaves the old file byte-identical.
  ws::Workspace bad = sample();
  bad.layout = "{ nope";
  R1_EXPECT(!ws::saveWorkspaceFile(bad, path, error) && slurp(path) == firstBytes && countFiles(dir.path()) == 1);
  // A failing write (target is a directory) leaves nothing behind.
  std::filesystem::create_directories(dir.path() / "dir.r1ws");
  R1_EXPECT(!ws::saveWorkspaceFile(sample(), dir.path() / "dir.r1ws", error) && countFiles(dir.path()) == 2);
  R1_EXPECT(!ws::loadWorkspaceFile(dir.path() / "missing.r1ws").ok);
  spit(dir.path() / "big.r1ws", std::string(ws::kMaxWorkspaceBytes + 1, ' '));
  const auto big = ws::loadWorkspaceFile(dir.path() / "big.r1ws");
  R1_EXPECT(!big.ok && big.error.find("larger") != std::string::npos);
  R1_EXPECT(ws::withWorkspaceExtension("a/setup").extension() == ".r1ws" && ws::withWorkspaceExtension("a/setup.x").extension() == ".x");
}

}  // namespace

int main() {
  testGoldenAndRoundTrip();
  testRealParts();
  testRejections();
  testFuzz();
  testFiles();
  return r1test::finish();
}
