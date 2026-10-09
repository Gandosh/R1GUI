// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of CommandKeyHandler through UiContext::keyDown: the key reaches the commands only when
//   the focused widget chain left it unused (spec 01 rule 16), the context follows the focus path
//   (a panel's own command beats the global one, a disabled one bubbles), a focused text field keeps
//   plain letters while Ctrl chords still run, sequences with the pending indicator and the timer that
//   expires them on the UI clock, key release forwarding for momentary commands, suppression, and a
//   handler destroyed with a pending timer.
// Callers: CTest (label fast).
#include "CommandFixture.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/commands/CommandKeys.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;

void testPrecedenceAndText() {
  CommandFixture f;
  f.registry.addContext("layers", cmd::kWindowContext);
  f.declare("global.delete", "Delete selection", cmd::CommandKind::Action, chordOf(Key::Delete));
  f.declare("layers.delete", "Remove layer", cmd::CommandKind::Action, chordOf(Key::Delete), {}, {}, "layers");
  f.declare("view.frame", "Frame", cmd::CommandKind::Action, chordOf(letter('F')));
  f.declare("file.save", "Save", cmd::CommandKind::Action, chordOf(letter('S'), Mod::kCtrl));
  CommandKeyHandler handler(f.t.ui, f.services());
  f.t.ui.setGlobalKeyHandler(&handler);

  Button& other = f.t.ui.create<Button>(f.t.ui.root(), "Other");
  TextInput& field = f.t.ui.create<TextInput>(f.t.ui.root());
  f.t.layout();

  // No focus: global.
  R1_EXPECT(f.t.ui.keyDown(Key::Delete) && f.runs["global.delete"] == 1);
  // A focused widget that does not use the key: still the window / global context.
  f.t.ui.focusWidget(other.id());
  R1_EXPECT(f.t.ui.keyDown(Key::Delete) && f.runs["global.delete"] == 2 && f.runs["layers.delete"] == 0);
  // A resolver puts a panel's context first (spec 01 scenario 6) ...
  handler.setContextResolver([&](UiContext&, WidgetId focused) { return std::vector<std::string>{focused == other.id() ? "layers" : cmd::kWindowContext}; });
  R1_EXPECT(f.t.ui.keyDown(Key::Delete) && f.runs["layers.delete"] == 1 && f.runs["global.delete"] == 2);
  // ... and a disabled panel command bubbles to the global one (scenario 7).
  f.enabled["layers.delete"] = false;
  R1_EXPECT(f.t.ui.keyDown(Key::Delete) && f.runs["global.delete"] == 3);
  f.enabled["layers.delete"] = true;
  handler.setContextResolver({});

  // Text field: plain letters stay in the field, Ctrl chords run (scenarios 4 and 5).
  f.t.ui.focusWidget(field.id());
  f.t.ui.keyDown(letter('F'));
  R1_EXPECT(f.runs["view.frame"] == 0);
  R1_EXPECT(f.t.ui.keyDown(letter('S'), Mod::kCtrl) && f.runs["file.save"] == 1);
  f.t.ui.focusWidget(other.id());
  R1_EXPECT(f.t.ui.keyDown(letter('F')) && f.runs["view.frame"] == 1);
  f.t.ui.setGlobalKeyHandler(nullptr);
  f.t.ui.keyDown(letter('F'));
  R1_EXPECT(f.runs["view.frame"] == 1);
}

void testSequenceAndTimer() {
  CommandFixture f;
  f.declare("seq.fit", "Fit", cmd::CommandKind::Action, pairOf(letter('K'), Mod::kCtrl, letter('F'), Mod::kCtrl));
  CommandKeyHandler handler(f.t.ui, f.services());
  f.t.ui.setGlobalKeyHandler(&handler);
  std::vector<cmd::PendingState> seen;
  f.router.setPendingObserver([&](const cmd::PendingState& s) { seen.push_back(s); });

  f.t.ui.setTime(1000);
  R1_EXPECT(f.t.ui.keyDown(letter('K'), Mod::kCtrl));
  R1_EXPECT(f.router.pending() && seen.size() == 1 && seen[0].text == "Ctrl+K");
  R1_EXPECT(f.t.ui.msUntilTick().has_value() && *f.t.ui.msUntilTick() <= 1500);  // the only scheduled work
  f.t.ui.setTime(1400);
  R1_EXPECT(f.t.ui.keyDown(letter('F'), Mod::kCtrl) && f.runs["seq.fit"] == 1 && !f.router.pending());

  // Without another key the timer (UI clock) ends the sequence after 1.5 s.
  f.t.ui.setTime(10000);
  R1_EXPECT(f.t.ui.keyDown(letter('K'), Mod::kCtrl) && f.router.pending());
  f.t.ui.setTime(11499);
  f.t.ui.tick();
  R1_EXPECT(f.router.pending());
  f.t.ui.setTime(11500);
  f.t.ui.tick();
  R1_EXPECT(!f.router.pending() && !seen.back().active);
  R1_EXPECT(!f.t.ui.msUntilTick().has_value());  // nothing stays scheduled once the sequence is over

  // A handler destroyed while a sequence is pending cancels its timer.
  f.t.ui.setTime(20000);
  f.t.ui.keyDown(letter('K'), Mod::kCtrl);
  f.t.ui.setGlobalKeyHandler(nullptr);
  f.router.setPendingObserver({});
}

void testKeyUpAndSuppression() {
  CommandFixture f;
  std::vector<cmd::ExecutePhase> phases;
  cmd::CommandDef pan;
  pan.id = "tool.pan";
  pan.label = "Pan";
  pan.kind = cmd::CommandKind::Momentary;
  pan.defaultChords = {chordOf(Key::Space, Mod::kCtrl), {}};
  pan.execute = [&](const cmd::ExecuteArgs& a) {
    phases.push_back(a.phase);
    return cmd::ExecuteResult::handled();
  };
  R1_EXPECT(f.registry.add(pan).ok);
  CommandKeyHandler handler(f.t.ui, f.services());
  f.t.ui.setGlobalKeyHandler(&handler);
  R1_EXPECT(f.t.ui.keyDown(Key::Space, Mod::kCtrl) && phases.size() == 1);
  R1_EXPECT(handler.onKeyUp(Key::Space, 0) && phases.size() == 2 && phases[1] == cmd::ExecutePhase::Release);
  R1_EXPECT(!handler.onKeyUp(Key::Space, 0));
  f.router.setSuppressed(true);  // a drag is running
  R1_EXPECT(!f.t.ui.keyDown(Key::Space, Mod::kCtrl) && phases.size() == 2);
  f.router.setSuppressed(false);
  f.t.ui.setGlobalKeyHandler(nullptr);
}

}  // namespace

int main() {
  testPrecedenceAndText();
  testSequenceAndTimer();
  testKeyUpAndSuppression();
  return r1test::finish();
}
