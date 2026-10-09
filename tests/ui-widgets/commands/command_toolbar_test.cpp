// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of toolbars bound to commands: buttons by kind (action, toggle, radio tool, group),
//   icon fallback, the tooltip "<label> (<chord>)" that follows a rebound chord at once, enabled and
//   active state following the predicates, hidden commands taking no space, click running the same
//   guarded path as the chord (a refused toggle is flipped back), unknown ids skipped, destruction
//   order safety (binding first, toolbar first, sync first) and a settled toolbar costing no frames.
// Callers: CTest (label fast).
#include "CommandFixture.h"
#include "r1ui/commands/Conflicts.h"
#include "r1ui/widgets/commands/CommandToolbar.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;

void populate(CommandFixture& f) {
  f.declare("radio.select", "Select", cmd::CommandKind::Radio, chordOf(letter('V')), {}, "mouse-pointer");
  f.declare("radio.pen", "Pen", cmd::CommandKind::Radio, chordOf(letter('P')), {}, "pen-tool");
  f.declare("radio.hand", "Hand", cmd::CommandKind::Radio, chordOf(letter('H')), {}, "hand");
  f.declare("edit.undo", "Undo", cmd::CommandKind::Action, chordOf(letter('Z'), Mod::kCtrl), {}, "undo2");
  f.declare("view.grid", "Show grid", cmd::CommandKind::Toggle, chordOf(letter('G'), Mod::kCtrl), {}, "grid-3x3");
  f.declare("plain", "No icon", cmd::CommandKind::Action);
  f.checked["radio.select"] = true;
}

void testButtons() {
  CommandFixture f;
  populate(f);
  Toolbar& bar = f.t.ui.create<Toolbar>(f.t.ui.root());
  using I = CommandToolbarItem;
  auto binding = bindCommandToolbar(f.t.ui, bar, f.sync,
                                    {I::command("radio.select"), I::command("radio.pen"), I::separator(), I::command("edit.undo"), I::command("view.grid"), I::command("ghost"), I::command("plain")});
  f.t.layout();
  R1_EXPECT(bar.buttonCount() == 5);  // the unknown id got no button
  ToolbarButton* select = binding->button("radio.select");
  ToolbarButton* undo = binding->button("edit.undo");
  R1_EXPECT(select != nullptr && select->kind() == ToolbarButtonKind::Tool && select->active() && select->icon() == "mouse-pointer");
  R1_EXPECT(select->tooltipText() == "Select (V)");  // the reference's "Pen (P)" form
  R1_EXPECT(undo != nullptr && undo->kind() == ToolbarButtonKind::Action && undo->tooltipText() == "Undo (Ctrl+Z)");
  R1_EXPECT(binding->button("view.grid")->kind() == ToolbarButtonKind::Toggle);
  R1_EXPECT(binding->button("plain")->icon() == "circle" && binding->button("plain")->tooltipText() == "No icon");  // no chord, no brackets
  R1_EXPECT(binding->button("ghost") == nullptr && binding->toolbar() == bar.id());

  // Click runs the command; the tool button follows the command's checked answer.
  const auto click = [&](ToolbarButton* b) {
    const auto r = f.t.ui.absRect(b->id());
    f.t.ui.pointerMove(r.x + r.w / 2, r.y + r.h / 2);
    f.t.ui.pointerDown(r.x + r.w / 2, r.y + r.h / 2);
    f.t.ui.pointerUp(r.x + r.w / 2, r.y + r.h / 2);
  };
  click(binding->button("radio.pen"));
  R1_EXPECT(f.runs["radio.pen"] == 1 && binding->button("radio.pen")->active() && !select->active() && bar.activeTool() == "radio.pen");
  click(undo);
  R1_EXPECT(f.runs["edit.undo"] == 1);
  click(binding->button("view.grid"));
  R1_EXPECT(f.runs["view.grid"] == 1 && binding->button("view.grid")->active());
  // The same command by its chord updates the toolbar (one source of truth).
  const std::vector<std::string> contexts{cmd::kWindowContext};
  f.router.handleKey({letter('G'), Mod::kCtrl, false, false}, contexts);
  f.registry.touch();
  R1_EXPECT(f.runs["view.grid"] == 2 && !binding->button("view.grid")->active());
}

void testLiveState() {
  CommandFixture f;
  populate(f);
  Toolbar& bar = f.t.ui.create<Toolbar>(f.t.ui.root());
  using I = CommandToolbarItem;
  auto binding = bindCommandToolbar(f.t.ui, bar, f.sync, {I::command("radio.select"), I::command("edit.undo"), I::command("view.grid")});
  f.t.layout();
  ToolbarButton* undo = binding->button("edit.undo");
  R1_EXPECT(undo->enabled());
  f.enabled["edit.undo"] = false;
  f.sync.refresh();
  R1_EXPECT(!undo->enabled());
  // Disabled buttons ignore clicks.
  const auto r = f.t.ui.absRect(undo->id());
  f.t.ui.pointerDown(r.x + r.w / 2, r.y + r.h / 2);
  f.t.ui.pointerUp(r.x + r.w / 2, r.y + r.h / 2);
  R1_EXPECT(f.runs["edit.undo"] == 0);

  // A rebound chord is in the tooltip the moment it is bound; unbinding removes the brackets.
  R1_EXPECT(cmd::assignChord(f.overrides, f.keymap, f.registry, "edit.undo", 0, chordOf(letter('U'), Mod::kAlt), false).ok);
  R1_EXPECT(undo->tooltipText() == "Undo (Alt+U)");
  cmd::assignChord(f.overrides, f.keymap, f.registry, "edit.undo", 0, std::nullopt, false);
  R1_EXPECT(undo->tooltipText() == "Undo");
  f.overrides.resetAll();
  R1_EXPECT(undo->tooltipText() == "Undo (Ctrl+Z)");

  // Checked state of the toggle and of the tool group follows the application.
  f.checked["view.grid"] = true;
  f.checked["radio.select"] = false;
  f.sync.refresh();
  R1_EXPECT(binding->button("view.grid")->active() && !binding->button("radio.select")->active());

  // A hidden command takes no space and the bar closes ranks.
  const double widthWith = f.t.ui.absRect(bar.id()).w;
  f.registry.add([] {
    cmd::CommandDef d;
    d.id = "hide.me";
    d.label = "Hide me";
    d.icon = "hand";
    return d;
  }());
  // (a command that is not visible is hidden by its predicate)
  bool visible = true;
  cmd::CommandDef maybe;
  maybe.id = "maybe";
  maybe.label = "Maybe";
  maybe.icon = "hand";
  maybe.visible = [&] { return visible; };
  f.registry.add(maybe);
  Toolbar& second = f.t.ui.create<Toolbar>(f.t.ui.root());
  auto other = bindCommandToolbar(f.t.ui, second, f.sync, {I::command("maybe"), I::command("hide.me")});
  f.t.layout();
  const double both = f.t.ui.absRect(second.id()).w;
  visible = false;
  f.sync.refresh();
  f.t.layout();
  R1_EXPECT(f.t.ui.absRect(second.id()).w < both);
  visible = true;
  f.sync.refresh();
  f.t.layout();
  R1_EXPECT(f.t.ui.absRect(second.id()).w == both);
  R1_EXPECT(widthWith > 0);

  // A removed command: its button hides instead of crashing.
  f.registry.remove("maybe");
  f.t.layout();
  R1_EXPECT(f.t.ui.absRect(second.id()).w < both);
}

void testRefusedToggleIsCorrected() {
  CommandFixture f;
  populate(f);
  f.enabled["view.grid"] = true;
  Toolbar& bar = f.t.ui.create<Toolbar>(f.t.ui.root());
  using I = CommandToolbarItem;
  // A toggle whose command refuses: the button's own flip is undone by the refresh after the click.
  cmd::CommandDef refuser;
  refuser.id = "refuse.toggle";
  refuser.label = "Refuses";
  refuser.icon = "eye";
  refuser.kind = cmd::CommandKind::Toggle;
  refuser.execute = [](const cmd::ExecuteArgs&) { return cmd::ExecuteResult::refused("busy"); };
  f.registry.add(refuser);
  auto binding = bindCommandToolbar(f.t.ui, bar, f.sync, {I::command("refuse.toggle")});
  f.t.layout();
  ToolbarButton* b = binding->button("refuse.toggle");
  R1_EXPECT(b != nullptr && !b->active());
  const auto r = f.t.ui.absRect(b->id());
  f.t.ui.pointerMove(r.x + r.w / 2, r.y + r.h / 2);
  f.t.ui.pointerDown(r.x + r.w / 2, r.y + r.h / 2);
  f.t.ui.pointerUp(r.x + r.w / 2, r.y + r.h / 2);
  R1_EXPECT(!b->active());
}

void testGroupAndLifetime() {
  CommandFixture f;
  populate(f);
  {
    Toolbar& bar = f.t.ui.create<Toolbar>(f.t.ui.root());
    using I = CommandToolbarItem;
    auto binding = bindCommandToolbar(f.t.ui, bar, f.sync, {I::group({"radio.select", "radio.pen", "radio.hand", "ghost"}), I::group({"ghost"}), I::command("edit.undo")});
    f.t.layout();
    R1_EXPECT(bar.buttonCount() == 2);  // a group of unknown ids adds nothing
    ToolbarButton* main = bar.button(0);
    R1_EXPECT(main->toolId() == "radio.select" && main->active() && main->tooltipText() == "Select (V)");
    // The application picks another tool of the group: the group's main button follows.
    f.checked["radio.select"] = false;
    f.checked["radio.hand"] = true;
    f.sync.refresh();
    R1_EXPECT(main->toolId() == "radio.hand" && main->active() && bar.activeTool() == "radio.hand");
    binding.reset();  // destroying the binding silences the buttons, the toolbar stays
    f.sync.refresh();
    f.enabled["edit.undo"] = false;
    f.sync.refresh();
    R1_EXPECT(bar.button(1)->enabled());
    const auto r = f.t.ui.absRect(bar.button(1)->id());
    f.t.ui.pointerMove(r.x + r.w / 2, r.y + r.h / 2);
    f.t.ui.pointerDown(r.x + r.w / 2, r.y + r.h / 2);
    f.t.ui.pointerUp(r.x + r.w / 2, r.y + r.h / 2);
    R1_EXPECT(f.runs["edit.undo"] == 0);
    f.t.ui.destroy(bar.id());
    f.t.layout();
  }
  {
    // The toolbar destroyed first: the binding checks liveness.
    Toolbar& bar = f.t.ui.create<Toolbar>(f.t.ui.root());
    auto binding = bindCommandToolbar(f.t.ui, bar, f.sync, {CommandToolbarItem::command("edit.undo")});
    f.t.ui.destroy(bar.id());
    f.sync.refresh();
    binding->refresh();
    R1_EXPECT(binding->button("edit.undo") == nullptr);
  }
  {
    // The sync destroyed first: the binding detaches safely.
    auto sync = std::make_unique<CommandUiSync>(f.t.ui, f.services());
    Toolbar& bar = f.t.ui.create<Toolbar>(f.t.ui.root());
    auto binding = bindCommandToolbar(f.t.ui, bar, *sync, {CommandToolbarItem::command("edit.undo")});
    sync.reset();
    binding.reset();
    f.registry.touch();
  }
}

void testSettledToolbarCostsNoFrames() {
  CommandFixture f;
  populate(f);
  Toolbar& bar = f.t.ui.create<Toolbar>(f.t.ui.root());
  using I = CommandToolbarItem;
  auto binding = bindCommandToolbar(f.t.ui, bar, f.sync, {I::command("radio.select"), I::command("edit.undo"), I::command("view.grid")});
  f.t.layout();
  f.t.ui.frame();
  R1_EXPECT(!f.t.ui.needsFrame());
  for (int i = 0; i < 5; ++i) f.sync.refresh();
  R1_EXPECT(!f.t.ui.needsFrame());  // nothing changed, nothing was invalidated
}

}  // namespace

int main() {
  testButtons();
  testLiveState();
  testRefusedToggleIsCorrected();
  testGroupAndLifetime();
  testSettledToolbarCostsNoFrames();
  return r1test::finish();
}
