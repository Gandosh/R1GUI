// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of CommandRouter: precedence (inner context before outer, global last), a disabled
//   command bubbling to the outer binding, repeat gating, text-entry swallowing with Ctrl chords
//   passing, exact modifier matching, sequences with a fake clock (pending observer, timeout at exactly
//   1.5 s, Escape cancel, unmatched second key, prefix conflicts), key-release triggers, momentary
//   commands (press, release, repeats, forced release), execution through menus (execute()), the
//   re-entrancy guard, exceptions thrown by commands, suppression during drag, unknown contexts, live
//   rebinding through the overrides and modifier-only presses.
// Callers: CTest (label fast).
#include <stdexcept>

#include "TestSupport.h"
#include "r1ui/commands/CommandRouter.h"
#include "r1ui/commands/Conflicts.h"

namespace {

using namespace r1test;

struct Fixture {
  CommandRegistry reg;
  KeybindingOverrides over{reg};
  Keymap map{reg, over};
  ManualClock clock;
  CommandRouter router{reg, map, clock};
  std::vector<std::string> global{kGlobalContext};
  std::vector<std::string> layers{"layers"};
  std::vector<std::string> text{kTextContext};

  Fixture() { reg.addContext("layers", kWindowContext); }
};

KeyInput down(Key key, uint8_t mods = 0, bool repeat = false) { return KeyInput{key, mods, repeat, false}; }
KeyInput up(Key key, uint8_t mods = 0) { return KeyInput{key, mods, false, true}; }

void testPrecedence() {
  Fixture f;
  int globalRuns = 0;
  int layerRuns = 0;
  f.reg.add(makeCommand("global.delete", "Delete selection", kGlobalContext, seq(Key::Delete), {}, &globalRuns));
  f.reg.add(makeCommand("layers.delete", "Remove layer", "layers", seq(Key::Delete), {}, &layerRuns));
  RouteResult r = f.router.handleKey(down(Key::Delete), f.layers);
  R1_EXPECT(r.consumed && r.commandId == "layers.delete" && layerRuns == 1 && globalRuns == 0);  // spec 01 scenario 6
  r = f.router.handleKey(down(Key::Delete), f.global);
  R1_EXPECT(r.consumed && r.commandId == "global.delete" && globalRuns == 1 && layerRuns == 1);
  r = f.router.handleKey(down(Key::Delete), {});  // no stack: global is still consulted
  R1_EXPECT(r.consumed && r.commandId == "global.delete");
  r = f.router.handleKey(down(Key::Delete, Mod::kShift), f.global);  // exact modifiers
  R1_EXPECT(!r.consumed && r.commandId.empty());
  const std::vector<std::string> unknown{"nonsense", "layers"};  // unknown names are skipped
  R1_EXPECT(f.router.handleKey(down(Key::Delete), unknown).commandId == "layers.delete");
  R1_EXPECT(!f.router.handleKey(down(Key::Unknown, Mod::kCtrl), f.global).consumed);  // a modifier alone is never a chord
}

void testDisabledBubbles() {
  Fixture f;
  bool layerEnabled = false;
  int globalRuns = 0;
  CommandDef layer = makeCommand("layers.delete", "Remove layer", "layers", seq(Key::Delete));
  layer.enabled = [&] { return layerEnabled; };
  f.reg.add(layer);
  f.reg.add(makeCommand("global.delete", "Delete selection", kGlobalContext, seq(Key::Delete), {}, &globalRuns));
  RouteResult r = f.router.handleKey(down(Key::Delete), f.layers);
  R1_EXPECT(r.consumed && r.commandId == "global.delete" && globalRuns == 1);  // spec 01 scenario 7
  layerEnabled = true;
  r = f.router.handleKey(down(Key::Delete), f.layers);
  R1_EXPECT(r.commandId == "layers.delete" && globalRuns == 1);

  Fixture g;  // disabled everywhere: nothing runs, the key is unused and the refusal is reported
  CommandDef only = makeCommand("only", "Only", kGlobalContext, seq(kG, Mod::kCtrl));
  only.enabled = [] { return false; };
  g.reg.add(only);
  r = g.router.handleKey(down(kG, Mod::kCtrl), g.global);
  R1_EXPECT(!r.consumed && r.commandId == "only" && r.result.status == ExecuteResult::Status::Refused && r.result.reason == "disabled");

  CommandDef passes = makeCommand("passes", "Passes", "layers", seq(kH));  // "not handled" lets the key travel on
  passes.execute = [](const ExecuteArgs&) { return ExecuteResult::notHandled(); };
  f.reg.add(passes);
  int outer = 0;
  f.reg.add(makeCommand("outer.h", "Outer", kGlobalContext, seq(kH), {}, &outer));
  R1_EXPECT(f.router.handleKey(down(kH), f.layers).commandId == "outer.h" && outer == 1);

  CommandDef hidden = makeCommand("hidden", "Hidden", kGlobalContext, seq(kJ));  // hidden commands still match
  hidden.visible = [] { return false; };
  f.reg.add(hidden);
  R1_EXPECT(f.router.handleKey(down(kJ), f.global).consumed);
}

void testRepeatGating() {
  Fixture f;
  int once = 0;
  int many = 0;
  f.reg.add(makeCommand("once", "Once", kGlobalContext, seq(kG, Mod::kCtrl), {}, &once));
  CommandDef rep = makeCommand("many", "Many", kGlobalContext, seq(kF5), {}, &many);
  rep.repeatable = true;
  f.reg.add(rep);
  R1_EXPECT(f.router.handleKey(down(kG, Mod::kCtrl), f.global).consumed);
  for (int i = 0; i < 5; ++i) R1_EXPECT(!f.router.handleKey(down(kG, Mod::kCtrl, true), f.global).consumed);  // repeats keep travelling outward
  R1_EXPECT(once == 1);
  R1_EXPECT(f.router.handleKey(down(kF5), f.global).consumed);
  for (int i = 0; i < 4; ++i) R1_EXPECT(f.router.handleKey(down(kF5, 0, true), f.global).consumed);
  R1_EXPECT(many == 5);
}

void testTextEntry() {
  Fixture f;
  int plain = 0;
  int save = 0;
  int del = 0;
  f.reg.add(makeCommand("view.frame", "Frame", kGlobalContext, seq(kF), {}, &plain));
  f.reg.add(makeCommand("file.save", "Save", kGlobalContext, seq(kS, Mod::kCtrl), {}, &save));
  f.reg.add(makeCommand("edit.delete", "Delete", kGlobalContext, seq(Key::Delete), {}, &del));
  RouteResult r = f.router.handleKey(down(kF), f.text);
  R1_EXPECT(!r.consumed && r.swallowedByText && plain == 0);  // spec 01 scenario 4
  R1_EXPECT(f.router.handleKey(down(kF, Mod::kShift), f.text).swallowedByText);
  R1_EXPECT(f.router.handleKey(down(Key::Space), f.text).swallowedByText);
  R1_EXPECT(f.router.handleKey(down(Key::Digit0), f.text).swallowedByText);
  r = f.router.handleKey(down(kS, Mod::kCtrl), f.text);
  R1_EXPECT(r.consumed && save == 1 && !r.swallowedByText);  // spec 01 scenario 5
  R1_EXPECT(f.router.handleKey(down(Key::Delete), f.text).consumed && del == 1);  // not a printable key
  R1_EXPECT(f.router.handleKey(down(kF), f.global).consumed && plain == 1);   // outside the text field it runs
  // The text context's own bindings are found first.
  int textUndo = 0;
  int globalUndo = 0;
  f.reg.add(makeCommand("text.undo", "Text undo", kTextContext, seq(kZ, Mod::kCtrl), {}, &textUndo));
  f.reg.add(makeCommand("edit.undo", "Undo", kGlobalContext, seq(kZ, Mod::kCtrl), {}, &globalUndo));
  f.router.handleKey(down(kZ, Mod::kCtrl), f.text);
  R1_EXPECT(textUndo == 1 && globalUndo == 0);
  // A nested text field inside a panel context still swallows.
  const std::vector<std::string> both{kTextContext, "layers"};
  R1_EXPECT(f.router.handleKey(down(kF), both).swallowedByText);
}

void testSequences() {
  Fixture f;
  int copyRuns = 0;
  int other = 0;
  f.reg.add(makeCommand("seq.copy", "Copy line", kGlobalContext, seq2(kK, Mod::kCtrl, kC, Mod::kCtrl), {}, &copyRuns));
  f.reg.add(makeCommand("seq.other", "Other", kGlobalContext, seq2(kK, Mod::kCtrl, kD, Mod::kCtrl), {}, &other));
  std::vector<PendingState> seen;
  f.router.setPendingObserver([&](const PendingState& s) { seen.push_back(s); });
  f.clock.set(1000);

  RouteResult r = f.router.handleKey(down(kK, Mod::kCtrl), f.global);
  R1_EXPECT(r.consumed && r.pendingStarted && f.router.pending());
  R1_EXPECT(seen.size() == 1 && seen[0].active && seen[0].text == "Ctrl+K" && seen[0].continuations.size() == 2 && seen[0].commandLabels.size() == 2);
  R1_EXPECT(f.router.pendingRemainingMs() == 1500);
  f.clock.advance(400);
  R1_EXPECT(f.router.handleKey(down(Key::Unknown, Mod::kCtrl), f.global).consumed == false && f.router.pending());  // a modifier press changes nothing
  r = f.router.handleKey(down(kC, Mod::kCtrl), f.global);
  R1_EXPECT(r.consumed && r.commandId == "seq.copy" && copyRuns == 1 && !f.router.pending());
  R1_EXPECT(seen.size() == 2 && !seen[1].active);

  // Exactly 1.5 s is a timeout; 1.499 s is not.
  f.clock.set(5000);
  f.router.handleKey(down(kK, Mod::kCtrl), f.global);
  f.clock.advance(1499);
  f.router.tick();
  R1_EXPECT(f.router.pending() && f.router.pendingRemainingMs() == 1);
  f.clock.advance(1);
  f.router.tick();
  R1_EXPECT(!f.router.pending() && !seen.back().active);
  // A timeout noticed by the next key: that key is handled as a fresh first chord.
  f.router.handleKey(down(kK, Mod::kCtrl), f.global);
  f.clock.advance(2000);
  r = f.router.handleKey(down(kC, Mod::kCtrl), f.global);
  R1_EXPECT(!r.consumed && copyRuns == 1 && !f.router.pending());

  // Escape cancels, an unmatched second key aborts and is consumed, repeats are ignored.
  f.router.handleKey(down(kK, Mod::kCtrl), f.global);
  r = f.router.handleKey(down(Key::Escape), f.global);
  R1_EXPECT(r.consumed && r.sequenceAborted && !f.router.pending());
  f.router.handleKey(down(kK, Mod::kCtrl), f.global);
  r = f.router.handleKey(down(kX, Mod::kCtrl), f.global);
  R1_EXPECT(r.consumed && r.sequenceAborted && !f.router.pending() && copyRuns == 1);
  f.router.handleKey(down(kK, Mod::kCtrl), f.global);
  R1_EXPECT(f.router.handleKey(down(kC, Mod::kCtrl, true), f.global).consumed && f.router.pending());
  f.router.cancelPending();
  R1_EXPECT(!f.router.pending());
  // A held first chord does not start sequences by repeating.
  R1_EXPECT(!f.router.handleKey(down(kK, Mod::kCtrl, true), f.global).consumed && !f.router.pending());

  // The first step is not a single binding, so a plain key elsewhere is untouched.
  R1_EXPECT(!f.router.handleKey(down(kK), f.global).consumed);
  // A command refused at the end of a sequence still consumes the keys.
  Fixture g;
  CommandDef disabled = makeCommand("seq.off", "Off", kGlobalContext, seq2(kK, Mod::kCtrl, kC, Mod::kCtrl));
  disabled.enabled = [] { return false; };
  g.reg.add(disabled);
  g.router.handleKey(down(kK, Mod::kCtrl), g.global);
  r = g.router.handleKey(down(kC, Mod::kCtrl), g.global);
  R1_EXPECT(r.consumed && r.result.status == ExecuteResult::Status::Refused && !g.router.pending());
  // Text-entry swallowing does not apply to Ctrl sequences, and a sequence may continue with a plain key.
  Fixture h;
  int plainSecond = 0;
  h.reg.add(makeCommand("seq.p", "P", kGlobalContext, seq2(kK, Mod::kCtrl, kP), {}, &plainSecond));
  R1_EXPECT(h.router.handleKey(down(kK, Mod::kCtrl), h.text).pendingStarted);
  R1_EXPECT(h.router.handleKey(down(kP), h.text).consumed && plainSecond == 1);
  // Suppression (a drag) cancels a pending sequence.
  h.router.handleKey(down(kK, Mod::kCtrl), h.global);
  h.router.setSuppressed(true);
  R1_EXPECT(!h.router.pending() && !h.router.handleKey(down(kK, Mod::kCtrl), h.global).consumed);
  h.router.setSuppressed(false);
  R1_EXPECT(h.router.handleKey(down(kK, Mod::kCtrl), h.global).pendingStarted);
  // The timeout is clamped.
  h.router.setSequenceTimeoutMs(1);
  R1_EXPECT(h.router.sequenceTimeoutMs() == 100);
  h.router.setSequenceTimeoutMs(10'000'000);
  R1_EXPECT(h.router.sequenceTimeoutMs() == 60000);
}

void testKeyUpAndMomentary() {
  Fixture f;
  int upRuns = 0;
  KeyChord release = chord(kR, Mod::kCtrl);
  release.onKeyUp = true;
  f.reg.add(makeCommand("on.release", "On release", kGlobalContext, ChordSequence::single(release), {}, &upRuns));
  R1_EXPECT(!f.router.handleKey(down(kR, Mod::kCtrl), f.global).consumed && upRuns == 0);
  R1_EXPECT(f.router.handleKey(up(kR, Mod::kCtrl), f.global).consumed && upRuns == 1);
  R1_EXPECT(!f.router.handleKey(up(kR), f.global).consumed);

  std::vector<ExecutePhase> phases;
  CommandDef pan = makeCommand("tool.pan", "Pan (hold)", kGlobalContext, seq(Key::Space, Mod::kCtrl));
  pan.kind = CommandKind::Momentary;
  pan.execute = [&](const ExecuteArgs& args) {
    phases.push_back(args.phase);
    return ExecuteResult::handled();
  };
  f.reg.add(pan);
  R1_EXPECT(f.router.handleKey(down(Key::Space, Mod::kCtrl), f.global).consumed);
  R1_EXPECT(f.router.handleKey(down(Key::Space, Mod::kCtrl, true), f.global).consumed);  // holding: repeats stop here
  R1_EXPECT(phases.size() == 1 && phases[0] == ExecutePhase::Press && f.router.heldMomentaryCount() == 1);
  R1_EXPECT(f.router.handleKey(up(Key::Space), f.global).consumed);  // the modifier may be released first
  R1_EXPECT(phases.size() == 2 && phases[1] == ExecutePhase::Release && f.router.heldMomentaryCount() == 0);
  R1_EXPECT(!f.router.handleKey(up(Key::Space), f.global).consumed);
  f.router.handleKey(down(Key::Space, Mod::kCtrl), f.global);
  f.router.releaseMomentary();  // window deactivated
  R1_EXPECT(phases.size() == 4 && phases[3] == ExecutePhase::Release && f.router.heldMomentaryCount() == 0);
  // A momentary command that becomes disabled while held still receives its Release.
  bool enabled = true;
  CommandDef hold = makeCommand("tool.hold", "Hold", kGlobalContext, seq(kH, Mod::kAlt));
  hold.kind = CommandKind::Momentary;
  hold.enabled = [&] { return enabled; };
  int releases = 0;
  hold.execute = [&](const ExecuteArgs& args) {
    if (args.phase == ExecutePhase::Release) ++releases;
    return ExecuteResult::handled();
  };
  f.reg.add(hold);
  f.router.handleKey(down(kH, Mod::kAlt), f.global);
  enabled = false;
  f.router.handleKey(up(kH), f.global);
  R1_EXPECT(releases == 1);
}

void testExecuteAndGuards() {
  Fixture f;
  int runs = 0;
  f.reg.add(makeCommand("a", "A", kGlobalContext, {}, {}, &runs));
  R1_EXPECT(f.router.execute("a", ExecuteSource::Menu).isHandled() && runs == 1);
  R1_EXPECT(f.router.execute("missing").status == ExecuteResult::Status::Refused);
  CommandDef off = makeCommand("off", "Off");
  off.enabled = [] { return false; };
  f.reg.add(off);
  R1_EXPECT(f.router.execute("off").reason == "disabled");  // a menu click on a disabled command does nothing
  CommandDef bare = makeCommand("bare", "Bare");
  bare.execute = nullptr;
  f.reg.add(bare);
  R1_EXPECT(f.router.execute("bare").status == ExecuteResult::Status::NotHandled);

  // A momentary command asked to Invoke (a menu click) gets Press and Release back to back.
  std::vector<ExecutePhase> clickPhases;
  CommandDef hold = makeCommand("hold", "Hold");
  hold.kind = CommandKind::Momentary;
  hold.execute = [&](const ExecuteArgs& args) {
    clickPhases.push_back(args.phase);
    return ExecuteResult::handled();
  };
  f.reg.add(hold);
  R1_EXPECT(f.router.execute("hold", ExecuteSource::Menu).isHandled() && clickPhases == (std::vector<ExecutePhase>{ExecutePhase::Press, ExecutePhase::Release}));

  // The source and phase reach the command.
  ExecuteSource seenSource = ExecuteSource::Api;
  CommandDef src = makeCommand("src", "Src");
  src.execute = [&](const ExecuteArgs& args) {
    seenSource = args.source;
    return ExecuteResult::handled();
  };
  f.reg.add(src);
  f.router.execute("src", ExecuteSource::Toolbar);
  R1_EXPECT(seenSource == ExecuteSource::Toolbar);

  // Re-entrancy: the same command cannot run inside itself; key events inside a command are ignored.
  int depth = 0;
  ExecuteResult inner;
  RouteResult innerKey;
  CommandDef loop = makeCommand("loop", "Loop", kGlobalContext, seq(kL));
  loop.execute = [&](const ExecuteArgs&) {
    ++depth;
    inner = f.router.execute("loop");
    innerKey = f.router.handleKey(down(kL), f.global);
    return ExecuteResult::handled();
  };
  f.reg.add(loop);
  R1_EXPECT(f.router.handleKey(down(kL), f.global).consumed);
  R1_EXPECT(depth == 1 && inner.status == ExecuteResult::Status::Refused && innerKey.reentrant && !innerKey.consumed && !f.router.executing());

  // A command may run a different command; mutual recursion is cut at the depth limit.
  CommandDef ping = makeCommand("ping", "Ping");
  CommandDef pong = makeCommand("pong", "Pong");
  int pings = 0;
  ping.execute = [&](const ExecuteArgs&) {
    ++pings;
    return f.router.execute("pong");
  };
  pong.execute = [&](const ExecuteArgs&) { return f.router.execute("ping"); };
  f.reg.add(ping);
  f.reg.add(pong);
  const ExecuteResult cycle = f.router.execute("ping");
  R1_EXPECT(pings == 1 && !cycle.isHandled() && !f.router.executing());

  // Exceptions become refusals and leave the router usable.
  CommandDef thrower = makeCommand("throws", "Throws", kGlobalContext, seq(kT));
  thrower.execute = [](const ExecuteArgs&) -> ExecuteResult { throw std::runtime_error("boom"); };
  f.reg.add(thrower);
  const RouteResult thrown = f.router.handleKey(down(kT), f.global);
  R1_EXPECT(!thrown.consumed && thrown.result.status == ExecuteResult::Status::Refused && thrown.result.reason.find("boom") != std::string::npos);
  R1_EXPECT(!f.router.executing() && f.router.execute("a").isHandled());
  CommandDef odd = makeCommand("odd", "Odd");
  odd.execute = [](const ExecuteArgs&) -> ExecuteResult { throw 42; };
  f.reg.add(odd);
  R1_EXPECT(f.router.execute("odd").reason == "unknown exception");

  // A command that unregisters itself while running does not crash the router.
  CommandDef suicide = makeCommand("suicide", "Suicide", kGlobalContext, seq(kQ));
  suicide.execute = [&](const ExecuteArgs&) {
    f.reg.remove("suicide");
    return ExecuteResult::handled();
  };
  f.reg.add(suicide);
  R1_EXPECT(f.router.handleKey(down(kQ), f.global).consumed && f.reg.find("suicide") == nullptr);
  R1_EXPECT(!f.router.handleKey(down(kQ), f.global).consumed);
}

void testLiveRebinding() {
  Fixture f;
  int renames = 0;
  f.reg.add(makeCommand("edit.rename", "Rename", kGlobalContext, seq(kF2), {}, &renames));
  R1_EXPECT(f.router.handleKey(down(kF2), f.global).consumed && renames == 1);
  // Spec 07 scenario 6: rebinding applies at once; the old chord no longer renames.
  R1_EXPECT(assignChord(f.over, f.map, f.reg, "edit.rename", 0, seq(kR, Mod::kCtrl | Mod::kShift), false).ok);
  R1_EXPECT(!f.router.handleKey(down(kF2), f.global).consumed);
  R1_EXPECT(f.router.handleKey(down(kR, Mod::kCtrl | Mod::kShift), f.global).consumed && renames == 2);
  f.over.resetAll();
  R1_EXPECT(f.router.handleKey(down(kF2), f.global).consumed && renames == 3);
  // Removing a command frees its chord.
  f.reg.remove("edit.rename");
  R1_EXPECT(!f.router.handleKey(down(kF2), f.global).consumed);
}

}  // namespace

int main() {
  testPrecedence();
  testDisabledBubbles();
  testRepeatGating();
  testTextEntry();
  testSequences();
  testKeyUpAndMomentary();
  testExecuteAndGuards();
  testLiveRebinding();
  return r1test::finish();
}
