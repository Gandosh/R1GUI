// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the action data layer: sanitising (invalid UTF-8, control characters, limits, blank
//   category), the query parser and matcher (case, all terms required, label / id / description /
//   category, term and byte limits), the highlight finder, the registry conversion (hidden commands,
//   both shortcuts, live overrides, label sort, enabled state) and the "commands without a
//   description" check.
// Callers: CTest (label fast).
#include <algorithm>

#include "TestSupport.h"
#include "r1ui/commands/Overrides.h"
#include "r1ui/commands/Text.h"
#include "r1ui/widgets/actions/ActionModel.h"

namespace {

using namespace r1ui::widgets;
namespace cmd = r1ui::commands;
using r1ui::core::events::Key;
namespace Mod = r1ui::core::events::Mod;

cmd::CommandDef command(const std::string& id, const std::string& label, const std::string& description, const std::string& category = {}, cmd::ChordSequence primary = {},
                        cmd::ChordSequence alternate = {}) {
  cmd::CommandDef def;
  def.id = id;
  def.label = label;
  def.description = description;
  def.category = category;
  def.defaultChords = {primary, alternate};
  def.execute = [](const cmd::ExecuteArgs&) { return cmd::ExecuteResult::handled(); };
  return def;
}

cmd::ChordSequence ctrl(char c) { return cmd::ChordSequence::single({static_cast<Key>(c), Mod::kCtrl, false}); }

void testSanitize() {
  ActionInfo a;
  a.id = "a.b";
  a.label = std::string("Bad \xFF\xFE text\x01 end");
  a.description = std::string(10000, 'x');
  a.category = "   ";
  a.icon = "circle";
  a.shortcut = "Ctrl+A";
  const ActionInfo s = sanitizedAction(a);
  R1_EXPECT(cmd::isValidUtf8(s.label) && s.label.find('\xFF') == std::string::npos && s.label.find('\x01') == std::string::npos);
  R1_EXPECT(s.label.find("\xEF\xBF\xBD") != std::string::npos);  // U+FFFD replaces invalid bytes
  R1_EXPECT(s.description.size() == kMaxActionDescriptionBytes);
  R1_EXPECT(s.category == "General");
  // A multi-byte character is never cut in half at the limit.
  a.description = std::string(kMaxActionDescriptionBytes - 1, 'a') + "\xC3\xA9";
  R1_EXPECT(cmd::isValidUtf8(sanitizedAction(a).description) && sanitizedAction(a).description.size() == kMaxActionDescriptionBytes - 1);
  a.label = std::string(1000, 'L');
  R1_EXPECT(sanitizedAction(a).label.size() == kMaxActionLabelBytes);
  a.id = std::string(500, 'i');
  R1_EXPECT(sanitizedAction(a).id.size() == kMaxActionIdBytes);
  R1_EXPECT(sanitizedAction(ActionInfo{}).category == "General");
}

void testQuery() {
  R1_EXPECT(parseActionQuery("").empty() && parseActionQuery("   \t ").empty());
  const ActionQuery q = parseActionQuery("  Copy  PASTE ");
  R1_EXPECT((q.terms == std::vector<std::string>{"copy", "paste"}));
  ActionInfo a;
  a.id = "edit.copyPaste";
  a.label = "Duplicate";
  a.description = "Make a copy next to the original";
  a.category = "Edit";
  const std::string hay = actionHaystack(a);
  R1_EXPECT(matchesAction(parseActionQuery("dup"), hay));            // label
  R1_EXPECT(matchesAction(parseActionQuery("COPYPASTE"), hay));      // id, case-insensitive
  R1_EXPECT(matchesAction(parseActionQuery("original"), hay));       // description
  R1_EXPECT(matchesAction(parseActionQuery("edit"), hay));           // category
  R1_EXPECT(matchesAction(parseActionQuery("dup orig edit"), hay));  // every term in a different field
  R1_EXPECT(!matchesAction(parseActionQuery("dup missing"), hay));   // one missing term fails the row
  R1_EXPECT(matchesAction(parseActionQuery(""), hay));
  // Limits: 40 terms are cut at the term limit, an enormous query at the byte limit; both stay valid.
  std::string many;
  for (int i = 0; i < 40; ++i) many += "t" + std::to_string(i) + " ";
  R1_EXPECT(parseActionQuery(many).terms.size() == kMaxActionQueryTerms);
  R1_EXPECT(parseActionQuery(std::string(100000, 'q')).terms.size() == 1 && parseActionQuery(std::string(100000, 'q')).terms[0].size() == kMaxActionQueryBytes);
  // Non-ASCII text matches bytewise and ASCII folding does not touch it.
  a.label = "Gr\xC3\xB6\xC3\x9F" "e";
  R1_EXPECT(matchesAction(parseActionQuery("gr\xC3\xB6\xC3\x9F"), actionHaystack(a)));
}

void testFind() {
  R1_EXPECT(findFolded("Hello World", "world") == 6);
  R1_EXPECT(findFolded("Hello World", "hello") == 0);
  R1_EXPECT(findFolded("Hello", "") == std::string_view::npos);
  R1_EXPECT(findFolded("Hi", "hello") == std::string_view::npos);
  R1_EXPECT(findFolded("", "a") == std::string_view::npos);
  R1_EXPECT(foldActionText("AbC\xC3\x84") == "abc\xC3\x84");
}

void testRegistry() {
  cmd::CommandRegistry reg;
  cmd::KeybindingOverrides over(reg);
  cmd::Keymap map(reg, over);
  R1_EXPECT(actionsFromRegistry(reg, map).empty());  // an empty registry gives an empty list
  R1_EXPECT(reg.add(command("edit.undo", "Undo", "Take back the last change", "Edit", ctrl('Z'), ctrl('U'))).ok);
  R1_EXPECT(reg.add(command("file.save", "Save", "Write the file", "File", ctrl('S'))).ok);
  R1_EXPECT(reg.add(command("misc.plain", "Alpha", "No category or chord")).ok);
  cmd::CommandDef hidden = command("zzz.hidden", "Hidden", "Not for the editor");
  hidden.hiddenFromEditor = true;
  R1_EXPECT(reg.add(std::move(hidden)).ok);
  cmd::CommandDef off = command("misc.off", "Disabled thing", "Cannot run now");
  off.enabled = [] { return false; };
  R1_EXPECT(reg.add(std::move(off)).ok);

  std::vector<ActionInfo> all = actionsFromRegistry(reg, map);
  R1_EXPECT(all.size() == 4);  // hidden skipped
  R1_EXPECT(all[0].id == "edit.undo" && all[0].shortcut == "Ctrl+Z, Ctrl+U" && all[0].category == "Edit");
  R1_EXPECT(all[2].category == "General" && all[2].shortcut.empty());
  R1_EXPECT(!all[3].enabled);
  R1_EXPECT(actionsFromRegistry(reg, map, {true, false, true}).size() == 5);
  R1_EXPECT(actionsFromRegistry(reg, map, {false, false, false})[0].shortcut == "Ctrl+Z");  // primary only
  const std::vector<ActionInfo> sorted = actionsFromRegistry(reg, map, {false, true, true});
  R1_EXPECT(sorted[0].label == "Alpha" && sorted[1].label == "Disabled thing" && sorted.back().label == "Undo");
  // Live: an override changes the shortcut text.
  over.set("file.save", 0, ctrl('W'));
  R1_EXPECT(actionsFromRegistry(reg, map)[1].shortcut == "Ctrl+W");
  over.set("file.save", 0, std::nullopt);
  R1_EXPECT(actionsFromRegistry(reg, map)[1].shortcut.empty());
}

void testWithoutDescription() {
  cmd::CommandRegistry reg;
  R1_EXPECT(commandsWithoutDescription(reg).empty());
  reg.add(command("a.one", "One", "Has text"));
  reg.add(command("a.two", "Two", ""));
  reg.add(command("a.three", "Three", "  \t "));
  reg.add(command("a.four", "Four", "x"));
  const std::vector<std::string> missing = commandsWithoutDescription(reg);
  R1_EXPECT((missing == std::vector<std::string>{"a.two", "a.three"}));
}

}  // namespace

int main() {
  testSanitize();
  testQuery();
  testFind();
  testRegistry();
  testWithoutDescription();
  return r1test::finish();
}
