// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of CustomizeController and the tool strip: the registered customize.* commands (kinds, labels,
//   enabled and checked predicates), the Customize toggle entering and leaving edit mode (session begin and
//   commit, save on leaving), revert, the reset-all confirmation (default answer Cancel, Escape, Reset all),
//   New menu with a unique name and the locked menu bar, notifications and message listeners, the command
//   set tracking (a changed set rebuilds, a plain touch does not), the tool strip buttons, and destroying
//   the controller with a dialog open.
// Callers: CTest (label fast).
#include <algorithm>

#include "CustomizeFixture.h"
#include "r1ui/commands/Conflicts.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/customize/CustomizableBars.h"
#include "r1ui/widgets/customize/CustomizeToolStrip.h"
#include "r1ui/widgets/dialog/DialogParts.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/menu/MenuPanel.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;

DialogButton* dialogButton(CustomizeFixture& f, const std::string& label) {
  if (!f.t.ui.overlays().any()) return nullptr;
  DialogButton* found = nullptr;
  f.t.ui.tree().forEachDescendant(f.t.ui.overlays().hostOf(f.t.ui.overlays().topmost()), [&](WidgetId id) {
    DialogButton* b = f.t.ui.objectAs<DialogButton>(id);
    if (b != nullptr && b->action().label == label) found = b;
  });
  return found;
}

void testCommands() {
  CustomizeFixture f;
  const cmd::CommandDef* toggle = f.registry.find(kCmdCustomizeToggle);
  const cmd::CommandDef* revert = f.registry.find(kCmdCustomizeRevert);
  const cmd::CommandDef* resetAll = f.registry.find(kCmdCustomizeResetAll);
  const cmd::CommandDef* newMenu = f.registry.find(kCmdCustomizeNewMenu);
  R1_EXPECT(toggle != nullptr && revert != nullptr && resetAll != nullptr && newMenu != nullptr);
  R1_EXPECT(toggle->kind == cmd::CommandKind::Toggle && toggle->label == "Customize" && toggle->category == "Customize");
  R1_EXPECT(!toggle->isChecked() && !revert->isEnabled() && !resetAll->isEnabled() && !newMenu->isEnabled());  // edit mode only
  // The toggle enters edit mode, which begins a session and enables the others.
  R1_EXPECT(f.router.execute(kCmdCustomizeToggle, cmd::ExecuteSource::Api).isHandled());
  R1_EXPECT(f.controller.editMode() && f.model.inEditSession() && toggle->isChecked() && resetAll->isEnabled() && newMenu->isEnabled());
  R1_EXPECT(!revert->isEnabled());  // nothing changed yet
  f.model.hideEntry("menu.edit.edit.copy");
  R1_EXPECT(revert->isEnabled());
  R1_EXPECT(!f.store.contents());  // nothing is written while editing
  f.router.execute(kCmdCustomizeRevert, cmd::ExecuteSource::Api);
  R1_EXPECT(f.model.userDelta().empty() && !revert->isEnabled() && f.model.inEditSession());
  f.model.hideEntry("menu.edit.edit.copy");
  // Leaving commits the session and the storage writes the file.
  f.router.execute(kCmdCustomizeToggle, cmd::ExecuteSource::Api);
  R1_EXPECT(!f.controller.editMode() && !f.model.inEditSession() && !toggle->isChecked() && f.store.contents().has_value());
  R1_EXPECT(f.router.execute(kCmdCustomizeRevert, cmd::ExecuteSource::Api).status != cmd::ExecuteResult::Status::Handled);  // disabled: refused
  // The chord system sees them like any command: the keybinding of the toggle works.
  R1_EXPECT(cmd::assignChord(f.overrides, f.keymap, f.registry, kCmdCustomizeToggle, 0, chordOf(letter('K'), Mod::kCtrl | Mod::kShift), false).ok);
  const std::vector<std::string> contexts{cmd::kWindowContext};
  R1_EXPECT(f.router.handleKey({letter('K'), static_cast<uint8_t>(Mod::kCtrl | Mod::kShift), false, false}, contexts).consumed && f.controller.editMode());
  f.controller.setEditMode(false);
}

void testNotifications() {
  CustomizeFixture f;
  int changes = 0, messages = 0;
  const auto id = f.controller.subscribe([&] { ++changes; });
  const auto msg = f.controller.subscribeMessage([&] { ++messages; });
  f.controller.setEditMode(true);
  R1_EXPECT(changes == 1);
  f.model.hideEntry("menu.edit.edit.copy");
  R1_EXPECT(changes == 2);
  // A refused edit sets the message once; the next success clears it; an unchanged message is silent.
  const auto refused = f.model.hideEntry("menu.help.help.about");
  R1_EXPECT(!refused.ok);
  f.controller.noteResult(refused);
  f.controller.noteResult(refused);
  R1_EXPECT(messages == 1 && f.controller.lastRefusal().find("locked") != std::string::npos);
  f.controller.noteResult(f.model.showEntry("menu.edit.edit.copy"));
  R1_EXPECT(messages == 2 && f.controller.lastRefusal().empty());
  f.controller.unsubscribe(id);
  f.controller.unsubscribeMessage(msg);
  f.model.hideEntry("menu.edit.edit.copy");
  R1_EXPECT(changes == 3 && messages == 2);  // changes == 3: the model change before unsubscribe counted, none after
  const uint64_t revision = f.controller.revision();
  f.controller.setEditMode(false);
  R1_EXPECT(f.controller.revision() > revision);
}

void testCommandSetTracking() {
  CustomizeFixture f;
  const uint64_t v = f.model.version();
  f.registry.touch();                       // a touch does not change the command set
  f.router.execute("file.save", cmd::ExecuteSource::Api);
  R1_EXPECT(f.model.version() == v);
  cmd::CommandDef extra;
  extra.id = "misc.extra";
  extra.label = "Extra";
  R1_EXPECT(f.registry.add(extra).ok);
  R1_EXPECT(f.model.version() > v);         // a new command can make a missing entry real
  const uint64_t v2 = f.model.version();
  f.registry.remove("edit.copy");
  R1_EXPECT(f.model.version() > v2);
  R1_EXPECT(f.model.effective().report.has(cz::ReportEntry::Code::MissingCommand, "menu.edit.edit.copy"));
  // The labels the model shows come from the registry.
  const cz::Node* undo = f.model.find("menu.edit.edit.undo");
  R1_EXPECT(undo != nullptr && f.model.shownLabel(*undo) == "Undo");
}

void testResetAllConfirmation() {
  CustomizeFixture f;
  f.controller.setEditMode(true);
  f.model.hideEntry("menu.edit.edit.copy");
  f.model.addUserMenu("Mine");
  // Opening the dialog changes nothing; the default answer is Cancel (Enter keeps everything).
  f.router.execute(kCmdCustomizeResetAll, cmd::ExecuteSource::Api);
  f.t.layout();
  R1_EXPECT(f.controller.resetAllDialogOpen() && f.t.ui.overlays().anyModal());
  R1_EXPECT(dialogButton(f, "Cancel") != nullptr && dialogButton(f, "Reset all") != nullptr);
  R1_EXPECT(!f.model.userDelta().empty());
  f.t.ui.keyDown(Key::Enter);
  f.t.layout();
  R1_EXPECT(!f.controller.resetAllDialogOpen() && !f.model.userDelta().empty());
  // Escape keeps everything too.
  f.controller.requestResetAll();
  f.controller.requestResetAll();  // a second request while open is ignored
  R1_EXPECT(f.t.ui.overlays().count() == 1);
  f.t.ui.keyDown(Key::Escape);
  f.t.layout();
  R1_EXPECT(!f.controller.resetAllDialogOpen() && !f.model.userDelta().empty());
  // "Reset all" removes every customization, user menus included.
  f.controller.requestResetAll();
  f.t.layout();
  f.clickWidget(dialogButton(f, "Reset all")->id());
  f.t.layout();
  R1_EXPECT(!f.controller.resetAllDialogOpen() && f.model.userDelta().empty() && f.model.effective().layout.menuBar.menus.size() == 4);
  // A controller destroyed while its dialog is open leaves nothing behind and is never called back.
  cz::Customization other(fixtureLayouts());
  other.hideEntry("menu.edit.edit.copy");
  auto* local = new r1ui::widgets::CustomizeController(f.t.ui, f.services(), f.sync, other);
  local->requestResetAll();
  R1_EXPECT(f.t.ui.overlays().anyModal());
  delete local;
  f.t.layout();
  R1_EXPECT(!f.t.ui.overlays().any() && !other.userDelta().empty());
}

void testNewMenu() {
  CustomizeFixture f;
  f.controller.setEditMode(true);
  const std::string first = f.controller.createUserMenu();
  R1_EXPECT(!first.empty() && f.model.find(first)->label == "New menu" && f.controller.takePendingRename() == first && f.controller.takePendingRename().empty());
  const std::string second = f.controller.createUserMenu();
  R1_EXPECT(f.model.find(second)->label == "New menu 2");
  f.router.execute(kCmdCustomizeNewMenu, cmd::ExecuteSource::Api);
  R1_EXPECT(f.model.effective().layout.menuBar.menus.size() == 7);
  // A locked menu bar refuses with a reason.
  cz::LayoutSet set = fixtureLayouts();
  set.menuBar.locked = true;
  f.model.resetAll();
  f.model.setBuiltin(set);
  R1_EXPECT(f.controller.createUserMenu().empty() && f.controller.lastRefusal().find("locked") != std::string::npos);
  f.controller.setEditMode(false);
}

void testToolStrip() {
  CustomizeFixture f;
  CustomizableMenuBar& bar = f.t.ui.create<CustomizableMenuBar>(f.t.ui.root(), f.controller);
  CustomizeToolStrip& strip = f.t.ui.create<CustomizeToolStrip>(f.t.ui.root(), f.controller);
  f.t.layout();
  const auto enabled = [&](WidgetId id) { return f.t.ui.object(id)->enabled(); };
  R1_EXPECT(enabled(strip.toggleButton()) && !enabled(strip.newMenuButton()) && !enabled(strip.restoreButton()) && !enabled(strip.resetMenuButton()) &&
            !enabled(strip.revertButton()) && !enabled(strip.resetAllButton()));
  f.clickWidget(strip.toggleButton());
  R1_EXPECT(f.controller.editMode() && bar.editor() != nullptr);
  R1_EXPECT(enabled(strip.newMenuButton()) && enabled(strip.restoreButton()) && enabled(strip.resetMenuButton()) && enabled(strip.resetAllButton()));
  R1_EXPECT(!enabled(strip.revertButton()));
  R1_EXPECT(f.t.ui.objectAs<Button>(strip.toggleButton())->tone() == ButtonTone::Accent);
  // New menu.
  f.clickWidget(strip.newMenuButton());
  R1_EXPECT(bar.editor()->titleCount() == 5 && bar.editor()->renaming());
  f.t.ui.keyDown(Key::Escape);
  // Reset menu acts on the menu the editor shows (a built-in one loses its hidden entry).
  bar.editor()->setCurrentMenu("menu.edit");
  f.model.hideEntry("menu.edit.edit.copy");
  R1_EXPECT(enabled(strip.revertButton()));
  f.clickWidget(strip.resetMenuButton());
  R1_EXPECT(f.model.restoreList().empty() && bar.editor()->titleCount() == 5);  // the user menu stays, the edit menu is reset
  // Revert returns to the start of the session (the menu created above is gone).
  f.clickWidget(strip.revertButton());
  R1_EXPECT(bar.editor()->titleCount() == 4 && f.model.userDelta().empty());
  // The message line shows why an edit was refused.
  f.model.hideEntry("menu.edit.edit.copy");
  f.controller.noteResult(f.model.hideEntry("menu.help.help.about"));
  const Label* message = f.t.ui.objectAs<Label>(strip.messageLabel());
  R1_EXPECT(message->text().find("locked") != std::string::npos);
  f.controller.noteResult(f.model.showEntry("menu.edit.edit.copy"));
  R1_EXPECT(message->text().empty());
  // Reset all asks first.
  f.clickWidget(strip.resetAllButton());
  R1_EXPECT(f.controller.resetAllDialogOpen());
  f.t.ui.keyDown(Key::Escape);
  // Leaving edit mode disables the edit-only controls again.
  f.clickWidget(strip.toggleButton());
  R1_EXPECT(!f.controller.editMode() && !enabled(strip.newMenuButton()) && f.t.ui.objectAs<Button>(strip.toggleButton())->tone() == ButtonTone::Panel);
}

}  // namespace

int main() {
  testCommands();
  testNotifications();
  testCommandSetTracking();
  testResetAllConfirmation();
  testNewMenu();
  testToolStrip();
  return r1test::finish();
}
