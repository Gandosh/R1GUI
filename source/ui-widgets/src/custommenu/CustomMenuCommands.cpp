// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CustomMenuCommands (CustomMenuCommands.h).
// Invariants: registered_ lists exactly the commands this binder added and still holds; the binder never
//   removes or replaces a command it did not register (a clashing id from elsewhere counts as a
//   failure); a hook that throws becomes a refused result.
// Callers: the host, tests.
#include "r1ui/widgets/custommenu/CustomMenuCommands.h"

namespace r1ui::widgets {

namespace {

namespace cm = commands::custommenu;

constexpr const char* kCategory = "Custom Menus";

}  // namespace

CustomMenuCommands::CustomMenuCommands(commands::CommandRegistry& registry, cm::CustomMenuSet& set, CustomMenuHooks hooks)
    : registry_(registry), set_(set), hooks_(std::move(hooks)) {
  registerGlobals();
  sync();
  listener_ = set_.subscribe([this] { sync(); });
}

CustomMenuCommands::~CustomMenuCommands() {
  set_.unsubscribe(listener_);
  for (const auto& entry : registered_) registry_.remove(entry.first);
}

bool CustomMenuCommands::addCommand(commands::CommandDef def) {
  const std::string id = def.id;
  if (registry_.find(id) != nullptr) {
    ++failures_;
    return false;
  }
  if (!registry_.add(std::move(def)).ok) {
    ++failures_;
    return false;
  }
  registered_[id] = true;
  return true;
}

commands::ExecuteResult CustomMenuCommands::run(const std::function<void()>& hook, const char* what) {
  if (!hook) return commands::ExecuteResult::refused(std::string(what) + " is not available here");
  try {
    hook();
  } catch (const std::exception& ex) {
    return commands::ExecuteResult::refused(ex.what());
  } catch (...) {
    return commands::ExecuteResult::refused(std::string(what) + " failed");
  }
  return commands::ExecuteResult::handled();
}

void CustomMenuCommands::registerGlobals() {
  commands::CommandDef create;
  create.id = cm::kCommandCreate;
  create.label = "Create Custom Menu...";
  create.description = "Create a pie menu or a dockable menu of your own actions";
  create.icon = "plus";
  create.category = kCategory;
  create.execute = [this](const commands::ExecuteArgs&) { return run(hooks_.create, "Creating a custom menu"); };
  addCommand(std::move(create));

  commands::CommandDef load;
  load.id = cm::kCommandLoad;
  load.label = "Load Custom Menu...";
  load.description = "Load a custom menu from a .r1mn file";
  load.icon = "folder-open";
  load.category = kCategory;
  load.execute = [this](const commands::ExecuteArgs&) { return run(hooks_.load, "Loading a custom menu"); };
  addCommand(std::move(load));
}

void CustomMenuCommands::registerMenu(const cm::CustomMenu& menu) {
  const std::string menuId = menu.id;
  const auto hookWith = [this, menuId](std::function<void(const std::string&)> CustomMenuHooks::*member, const char* what) {
    return [this, menuId, member, what](const commands::ExecuteArgs&) {
      const std::function<void(const std::string&)>& fn = hooks_.*member;
      return run(fn ? std::function<void()>([fn, menuId] { fn(menuId); }) : std::function<void()>(), what);
    };
  };
  if (menu.kind == cm::MenuKind::Panel) {
    commands::CommandDef open;
    open.id = cm::commandIdFor(cm::MenuAction::Open, menuId);
    open.label = menu.name;
    open.description = "Open or focus the dockable menu '" + menu.name + "'";
    open.icon = "layout-panel-top";
    open.category = kCategory;
    open.execute = hookWith(&CustomMenuHooks::openPanel, "Opening the menu");
    addCommand(std::move(open));
  }
  commands::CommandDef edit;
  edit.id = cm::commandIdFor(cm::MenuAction::Edit, menuId);
  edit.label = "Edit...";
  edit.description = "Edit the custom menu '" + menu.name + "'";
  edit.category = kCategory;
  edit.hiddenFromEditor = true;
  edit.execute = hookWith(&CustomMenuHooks::edit, "Editing a custom menu");
  addCommand(std::move(edit));

  commands::CommandDef save;
  save.id = cm::commandIdFor(cm::MenuAction::Save, menuId);
  save.label = "Save As...";
  save.description = "Save the custom menu '" + menu.name + "' to a .r1mn file";
  save.category = kCategory;
  save.hiddenFromEditor = true;
  save.execute = hookWith(&CustomMenuHooks::save, "Saving a custom menu");
  addCommand(std::move(save));

  commands::CommandDef remove;
  remove.id = cm::commandIdFor(cm::MenuAction::Delete, menuId);
  remove.label = "Delete";
  remove.description = "Delete the custom menu '" + menu.name + "'";
  remove.category = kCategory;
  remove.hiddenFromEditor = true;
  remove.execute = [this, menuId](const commands::ExecuteArgs&) {
    if (hooks_.remove) return run([this, menuId] { hooks_.remove(menuId); }, "Deleting a custom menu");
    const cm::MenuEditResult result = set_.deleteMenu(menuId);
    return result.ok ? commands::ExecuteResult::handled() : commands::ExecuteResult::refused(result.reason);
  };
  addCommand(std::move(remove));
}

void CustomMenuCommands::removeAll(const std::string& menuId) {
  for (const cm::MenuAction action : {cm::MenuAction::Open, cm::MenuAction::Edit, cm::MenuAction::Save, cm::MenuAction::Delete}) {
    const std::string id = cm::commandIdFor(action, menuId);
    const auto it = registered_.find(id);
    if (it == registered_.end()) continue;
    registry_.remove(id);
    registered_.erase(it);
  }
}

void CustomMenuCommands::sync() {
  // Menus that are gone, or whose label / kind changed, lose their commands first.
  for (auto it = shown_.begin(); it != shown_.end();) {
    const cm::CustomMenu* menu = set_.find(it->first);
    if (menu == nullptr || menu->name != it->second.name || menu->kind != it->second.kind) {
      removeAll(it->first);
      it = shown_.erase(it);
    } else {
      ++it;
    }
  }
  for (const cm::CustomMenu& menu : set_.menus()) {
    if (shown_.count(menu.id) != 0) continue;
    registerMenu(menu);
    shown_[menu.id] = {menu.name, menu.kind};
  }
}

}  // namespace r1ui::widgets
