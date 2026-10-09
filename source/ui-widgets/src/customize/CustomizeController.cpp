// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CustomizeController.h.
// Invariants: leaving edit mode always commits (or reverts nothing) the session it began; the registry
//   subscription forwards only real changes of the command SET; the dialog result callback is guarded by
//   `alive_` so a controller destroyed with the dialog open is never called; registered commands are
//   removed in the destructor.
// Callers: hosts, the gallery, tests.
#include "r1ui/widgets/customize/CustomizeController.h"

#include <algorithm>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace cmd = commands;
namespace cz = commands::customize;

CustomizeController::CustomizeController(UiContext& ui, CommandServices services, CommandUiSync& sync, cz::Customization& model)
    : ui_(ui), services_(services), sync_(sync), model_(model), drag_(ui) {
  cmd::CommandRegistry* registry = &services_.registry;
  model_.setCommandLabeler([registry](const std::string& id) {
    const cmd::CommandDef* def = registry->find(id);
    return def != nullptr ? def->label : id;
  });
  model_.setCommandExists([registry](const std::string& id) { return registry->find(id) != nullptr; });
  seenCount_ = commandCount();
  modelListener_ = model_.subscribe([this] { notify(); });
  registryListener_ = services_.registry.subscribe([this] {
    const size_t now = commandCount();
    if (now == seenCount_) return;
    seenCount_ = now;
    model_.commandsChanged();
  });
  registerCommands();
}

CustomizeController::~CustomizeController() {
  *alive_ = false;
  if (dialog_.valid() && isDialogOpen(ui_, dialog_)) closeDialog(ui_, dialog_, "cancel");
  services_.registry.unsubscribe(registryListener_);
  model_.unsubscribe(modelListener_);
  model_.setCommandExists({});
  model_.setCommandLabeler({});
  for (const std::string& id : ownCommands_) services_.registry.remove(id);
  if (editMode_) model_.commitSession();
}

// The registry adds and removes commands one at a time and notifies after each, so a change of the count
// is exactly a change of the set (checking it must stay O(1): hosts register thousands of commands).
size_t CustomizeController::commandCount() const { return services_.registry.size(); }

// ---- commands -----------------------------------------------------------------------------------

void CustomizeController::registerCommands() {
  const auto add = [this](const char* id, const char* label, const char* description, const char* icon, cmd::CommandKind kind, std::function<bool()> enabled,
                          std::function<bool()> checked, std::function<void()> run) {
    cmd::CommandDef def;
    def.id = id;
    def.label = label;
    def.description = description;
    def.icon = icon;
    def.category = "Customize";
    def.kind = kind;
    def.enabled = std::move(enabled);
    def.checked = std::move(checked);
    def.execute = [run = std::move(run)](const cmd::ExecuteArgs&) {
      run();
      return cmd::ExecuteResult::handled();
    };
    if (services_.registry.add(std::move(def)).ok) ownCommands_.emplace_back(id);
  };
  add(kCmdCustomizeToggle, "Customize", "Edit the menus, toolbars and panels", "sliders-horizontal", cmd::CommandKind::Toggle, {}, [this] { return editMode_; },
      [this] { toggleEditMode(); });
  add(kCmdCustomizeRevert, "Revert customization changes", "Return to how things were when you started customizing", "undo2", cmd::CommandKind::Action,
      [this] { return editMode_ && model_.sessionChanged(); }, {}, [this] { revertSession(); });
  add(kCmdCustomizeResetAll, "Reset all customization...", "Remove every menu, toolbar and panel customization", "trash-2", cmd::CommandKind::Action,
      [this] { return editMode_; }, {}, [this] { requestResetAll(); });
  add(kCmdCustomizeNewMenu, "New menu", "Create a menu of your own", "plus", cmd::CommandKind::Action, [this] { return editMode_; }, {},
      [this] { createUserMenu(); });
}

// ---- edit mode ----------------------------------------------------------------------------------

void CustomizeController::setEditMode(bool on) {
  if (on == editMode_) return;
  editMode_ = on;
  drag_.cancel();
  if (on) {
    model_.beginEditSession();
  } else {
    model_.commitSession();
  }
  refusal_.clear();
  services_.registry.touch();  // the Customize toggle and the enabled state of the other commands
  notify();
}

bool CustomizeController::revertSession() {
  if (!editMode_) return false;
  drag_.cancel();
  const bool ok = model_.revertSession();
  services_.registry.touch();
  return ok;
}

void CustomizeController::requestResetAll() {
  if (resetAllDialogOpen()) return;
  DialogSpec spec;
  spec.title = "Reset all customization";
  spec.description = "Every menu, toolbar and panel returns to its original state. Menus and toolbars you created are deleted.";
  spec.actions = {{"cancel", "Cancel", DialogActionKind::Neutral, true, true, true}, {"reset", "Reset all", DialogActionKind::Danger, false, false, true}};
  spec.maxWidth = 380.0;
  spec.onResult = [this, alive = std::weak_ptr<bool>(alive_)](const DialogResult& result) {
    const std::shared_ptr<bool> live = alive.lock();
    if (!live || !*live) return;
    dialog_ = {};
    if (result.action == "reset") noteResult(model_.resetAll());
  };
  dialog_ = openDialog(ui_, std::move(spec));
}

bool CustomizeController::resetAllDialogOpen() const { return dialog_.valid() && isDialogOpen(ui_, dialog_); }

std::string CustomizeController::createUserMenu() {
  for (int n = 1; n < 100; ++n) {
    const std::string title = n == 1 ? "New menu" : "New menu " + std::to_string(n);
    cz::EditResult r = model_.addUserMenu(title);
    if (r.ok) {
      noteResult(r);
      pendingRename_ = r.id;
      notify();
      return r.id;
    }
    if (r.error != cz::EditError::Duplicate) {
      noteResult(r);
      return {};
    }
  }
  return {};
}

std::string CustomizeController::takePendingRename() {
  std::string id;
  id.swap(pendingRename_);
  return id;
}

void CustomizeController::noteResult(const cz::EditResult& result) {
  const std::string text = result.ok ? std::string() : result.reason;
  if (text == refusal_) return;
  refusal_ = text;
  const auto snapshot = messageListeners_;
  for (const auto& entry : snapshot) {
    if (entry.second) entry.second();
  }
}

CustomizeController::ListenerId CustomizeController::subscribeMessage(Listener listener) {
  const ListenerId id = nextListener_++;
  messageListeners_.emplace_back(id, std::move(listener));
  return id;
}

void CustomizeController::unsubscribeMessage(ListenerId id) {
  messageListeners_.erase(std::remove_if(messageListeners_.begin(), messageListeners_.end(), [id](const auto& e) { return e.first == id; }), messageListeners_.end());
}

// ---- notifications ------------------------------------------------------------------------------

CustomizeController::ListenerId CustomizeController::subscribe(Listener listener) {
  const ListenerId id = nextListener_++;
  listeners_.emplace_back(id, std::move(listener));
  return id;
}

void CustomizeController::unsubscribe(ListenerId id) {
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [id](const auto& e) { return e.first == id; }), listeners_.end());
}

void CustomizeController::notify() {
  ++revision_;
  if (notifying_) {
    renotify_ = true;
    return;
  }
  notifying_ = true;
  for (int round = 0; round < 4; ++round) {
    renotify_ = false;
    const auto snapshot = listeners_;
    for (const auto& entry : snapshot) {
      const bool stillThere = std::any_of(listeners_.begin(), listeners_.end(), [&](const auto& e) { return e.first == entry.first; });
      if (stillThere && entry.second) entry.second();
    }
    if (!renotify_) break;
  }
  notifying_ = false;
}

}  // namespace r1ui::widgets
