// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of CommandRegistry: registration and its refusals (duplicate, empty and invalid ids,
//   empty label, bad icon, unknown context, invalid chord, radio without a group), sanitising of huge
//   and invalid-UTF-8 text, 10 000 commands, removal, enumeration order and categories, the context
//   tree (chain, ancestors, descendants, limits), the version counter and listeners (re-entrant
//   subscribe, unsubscribe and touch from a listener).
// Callers: CTest (label fast).
#include <chrono>

#include "TestSupport.h"
#include "r1ui/commands/Text.h"

namespace {

using namespace r1test;

void testRegistration() {
  CommandRegistry reg;
  R1_EXPECT(reg.add(makeCommand("edit.undo", "Undo")).ok);
  R1_EXPECT(reg.size() == 1);
  const RegisterResult dup = reg.add(makeCommand("edit.undo", "Other"));
  R1_EXPECT(!dup.ok && dup.error == RegisterError::DuplicateId);
  R1_EXPECT(reg.find("edit.undo")->label == "Undo");  // the second declaration is ignored
  R1_EXPECT(reg.add(makeCommand("", "x")).error == RegisterError::InvalidId);
  R1_EXPECT(reg.add(makeCommand("has space", "x")).error == RegisterError::InvalidId);
  R1_EXPECT(reg.add(makeCommand(std::string(129, 'a'), "x")).error == RegisterError::InvalidId);
  R1_EXPECT(reg.add(makeCommand("a.empty", "")).error == RegisterError::EmptyLabel);
  R1_EXPECT(reg.add(makeCommand("a.blank", "   ")).error == RegisterError::EmptyLabel);
  R1_EXPECT(reg.add(makeCommand("a.ctrl", "\n\t")).error == RegisterError::EmptyLabel);
  R1_EXPECT(reg.add(makeCommand("a.ctx", "x", "nowhere")).error == RegisterError::UnknownContext);
  CommandDef icon = makeCommand("a.icon", "x");
  icon.icon = "../etc";
  R1_EXPECT(reg.add(icon).error == RegisterError::InvalidIcon);
  CommandDef radio = makeCommand("a.radio", "x");
  radio.kind = CommandKind::Radio;
  R1_EXPECT(reg.add(radio).error == RegisterError::MissingRadioGroup);
  radio.radioGroup = "tools";
  R1_EXPECT(reg.add(radio).ok);
  CommandDef bad = makeCommand("a.chord", "x");
  bad.defaultChords[0] = ChordSequence::single(chord(Key::Unknown));
  R1_EXPECT(reg.add(bad).error == RegisterError::InvalidChord);
  R1_EXPECT(reg.size() == 2);  // refused declarations leave no trace
  CommandDef emptyContext = makeCommand("a.noctx", "x");
  emptyContext.context.clear();
  R1_EXPECT(reg.add(emptyContext).ok && reg.find("a.noctx")->context == kGlobalContext);
}

void testSanitising() {
  CommandRegistry reg;
  CommandDef huge = makeCommand("huge", std::string(1000000, 'L'));
  huge.description = std::string(1000000, 'D');
  huge.category = std::string(5000, 'C');
  R1_EXPECT(reg.add(huge).ok);
  const CommandDef* c = reg.find("huge");
  R1_EXPECT(c->label.size() == kMaxLabelBytes && c->description.size() == kMaxDescriptionBytes && c->category.size() == kMaxCategoryBytes);
  R1_EXPECT(reg.add(makeCommand("utf", "bad\xFF\xC0\x80 label\xE2\x82")).ok);
  R1_EXPECT(isValidUtf8(reg.find("utf")->label));
  CommandDef nl = makeCommand("nl", "line one\nline two");
  nl.category = "";
  R1_EXPECT(reg.add(nl).ok && reg.find("nl")->label == "line one line two" && reg.find("nl")->category == "General");
}

void testManyCommands() {
  CommandRegistry reg;
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 10000; ++i) {
    CommandDef def = makeCommand("cmd." + std::to_string(i), "Command " + std::to_string(i));
    def.category = "cat" + std::to_string(i % 10);
    R1_EXPECT(reg.add(std::move(def)).ok);
  }
  R1_EXPECT(reg.size() == 10000);
  R1_EXPECT(reg.commands().size() == 10000 && reg.commands().front()->id == "cmd.0" && reg.commands().back()->id == "cmd.9999");
  R1_EXPECT(reg.categories().size() == 10 && reg.inCategory("cat3").size() == 1000);
  for (int i = 0; i < 10000; i += 2) R1_EXPECT(reg.remove("cmd." + std::to_string(i)));
  R1_EXPECT(reg.size() == 5000 && reg.commands().front()->id == "cmd.1");
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  R1_EXPECT(seconds < 5.0);
  R1_EXPECT(!reg.remove("cmd.0"));
}

void testOrderAndGroups() {
  CommandRegistry reg;
  reg.add(makeCommand("b", "B"));
  reg.add(makeCommand("a", "A"));
  reg.remove("b");
  reg.add(makeCommand("b", "B again"));
  R1_EXPECT(reg.commands().size() == 2 && reg.commands()[0]->id == "a" && reg.commands()[1]->id == "b");  // registration order, a re-added id goes last
  R1_EXPECT(reg.serialOf("a") < reg.serialOf("b") && reg.serialOf("zzz") == 0);
  for (const char* id : {"tool.pen", "tool.hand"}) {
    CommandDef d = makeCommand(id, id);
    d.kind = CommandKind::Radio;
    d.radioGroup = "tools";
    reg.add(d);
  }
  R1_EXPECT(reg.inRadioGroup("tools").size() == 2 && reg.inRadioGroup("none").empty());
}

void testContexts() {
  CommandRegistry reg;
  R1_EXPECT(reg.context(kGlobalContext) && reg.context(kWindowContext) && reg.context(kTextContext)->textEntry);
  R1_EXPECT(reg.addContext("viewport", kWindowContext));
  R1_EXPECT(reg.addContext("layers", kWindowContext));
  R1_EXPECT(reg.addContext("viewport.tools", "viewport"));
  R1_EXPECT(!reg.addContext("viewport", kWindowContext));       // duplicate
  R1_EXPECT(!reg.addContext("x", "missing"));                   // unknown parent
  R1_EXPECT(!reg.addContext("bad name", kWindowContext));       // invalid name
  R1_EXPECT(!reg.addContext("", kWindowContext));
  const std::vector<std::string> chain = reg.contextChain("viewport.tools");
  R1_EXPECT(chain == (std::vector<std::string>{"viewport.tools", "viewport", "window", "global"}));
  R1_EXPECT(reg.contextChain("nope").empty());
  R1_EXPECT(reg.isAncestor("viewport", "viewport.tools") && !reg.isAncestor("viewport.tools", "viewport") && !reg.isAncestor("layers", "viewport"));
  R1_EXPECT(!reg.isAncestor("viewport", "viewport"));
  R1_EXPECT(reg.descendantsOf("viewport") == (std::vector<std::string>{"viewport.tools"}));
  R1_EXPECT(reg.descendantsOf(kGlobalContext).size() == 5);
  // The depth limit stops a chain from growing without bound.
  std::string parent = "layers";
  int added = 0;
  for (int i = 0; i < 200; ++i) {
    const std::string name = "deep" + std::to_string(i);
    if (!reg.addContext(name, parent)) break;
    parent = name;
    ++added;
  }
  R1_EXPECT(added > 50 && added < 200);
  R1_EXPECT(reg.contextChain(parent).size() <= static_cast<size_t>(kMaxContextDepth));
  CommandDef def = makeCommand("v.cmd", "V", "viewport");
  R1_EXPECT(reg.add(def).ok);
}

void testVersionAndListeners() {
  CommandRegistry reg;
  const uint64_t v0 = reg.version();
  int calls = 0;
  const auto id = reg.subscribe([&] { ++calls; });
  R1_EXPECT(id != 0);
  reg.add(makeCommand("x", "X"));
  R1_EXPECT(reg.version() > v0 && calls == 1);
  reg.add(makeCommand("x", "dup"));  // a refused declaration does not notify
  R1_EXPECT(calls == 1);
  reg.touch();
  R1_EXPECT(calls == 2);
  reg.unsubscribe(id);
  reg.touch();
  R1_EXPECT(calls == 2);
  R1_EXPECT(reg.subscribe(nullptr) == 0);

  // A listener may unsubscribe itself, subscribe another and read the registry.
  int second = 0;
  CommandRegistry::ListenerId self = 0;
  self = reg.subscribe([&] {
    reg.unsubscribe(self);
    reg.subscribe([&] { ++second; });
    R1_EXPECT(reg.find("x") != nullptr);
  });
  reg.touch();
  reg.touch();
  R1_EXPECT(second >= 1);

  // A listener that keeps touching is cut off instead of recursing forever.
  CommandRegistry loop;
  int rounds = 0;
  loop.subscribe([&] {
    ++rounds;
    loop.touch();
  });
  loop.touch();
  R1_EXPECT(rounds == kMaxNotifyRounds);
}

}  // namespace

int main() {
  testRegistration();
  testSanitising();
  testManyCommands();
  testOrderAndGroups();
  testContexts();
  testVersionAndListeners();
  return r1test::finish();
}
