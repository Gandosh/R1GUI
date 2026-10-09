// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CommandToolbar.h.
// Invariants: every callback and the refresher go through State and check `alive` and the toolbar's
//   liveness first; a button's command id is its toolId (a group's main button follows its current
//   entry); refresh() changes a widget only when the value differs, so a settled toolbar costs no frames.
// Callers: hosts, the gallery, tests.
#include "r1ui/widgets/commands/CommandToolbar.h"

#include <algorithm>

#include "r1ui/widgets/menu/MenuModel.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace cmd = commands;
namespace layout = core::layout;

CommandToolbarItem CommandToolbarItem::command(std::string id) {
  CommandToolbarItem item;
  item.commandIds.push_back(std::move(id));
  return item;
}

CommandToolbarItem CommandToolbarItem::separator() {
  CommandToolbarItem item;
  item.kind = Kind::Separator;
  return item;
}

CommandToolbarItem CommandToolbarItem::group(std::vector<std::string> ids) {
  CommandToolbarItem item;
  item.kind = Kind::Group;
  item.commandIds = std::move(ids);
  return item;
}

struct CommandToolbarBinding::State {
  struct Bound {
    std::vector<std::string> ids;  // one id, or the entries of a group
    size_t buttonIndex = 0;
    bool group = false;
  };

  State(UiContext& u, core::tree::WidgetId bar, const CommandServices& s) : ui(&u), toolbar(bar), services(s) {}

  Toolbar* bar() const { return alive ? ui->objectAs<Toolbar>(toolbar) : nullptr; }

  void activated(const std::string& id) {
    if (!alive) return;
    services.router.execute(id, cmd::ExecuteSource::Toolbar);
    refresh();  // the command decides the final state: a refused toggle is flipped back here
  }

  void refresh() {
    Toolbar* t = bar();
    if (t == nullptr) return;
    for (const Bound& b : bound) {
      ToolbarButton* button = t->button(b.buttonIndex);
      if (button == nullptr) continue;
      if (b.group) {
        refreshGroup(*t, *button, b);
      } else {
        refreshSingle(*button, b);
      }
    }
  }

  void refreshSingle(ToolbarButton& button, const Bound& b) {
    const cmd::CommandDef* command = services.registry.find(b.ids.front());
    show(button, command != nullptr && command->isVisible());
    if (command == nullptr) return;
    button.setEnabled(command->isEnabled());
    const bool checked = command->isChecked();
    if (button.kind() == ToolbarButtonKind::Tool) {
      if (checked) {
        if (Toolbar* t = bar()) t->setActiveTool(command->id);
      } else if (button.active()) {
        button.setActive(false);
      }
    } else if (button.kind() == ToolbarButtonKind::Toggle) {
      button.setActive(checked);
    }
    const std::string tip = commandTooltip(services, *command, command->tooltip);
    if (button.tooltipText() != tip) button.setTooltip(tip);
  }

  void refreshGroup(Toolbar& t, ToolbarButton& main, const Bound& b) {
    bool anyVisible = false;
    const std::string* checkedId = nullptr;
    for (const std::string& id : b.ids) {
      const cmd::CommandDef* command = services.registry.find(id);
      if (command == nullptr) continue;
      anyVisible = anyVisible || command->isVisible();
      if (checkedId == nullptr && command->isChecked()) checkedId = &id;
    }
    show(main, anyVisible);
    if (checkedId != nullptr) {
      t.setActiveTool(*checkedId);
    } else if (main.active()) {
      main.setActive(false);
    }
  }

  // A hidden button takes no space (spec 07 rule 35, the toolbar closes ranks).
  void show(ToolbarButton& button, bool visible) {
    const layout::Display wanted = visible ? layout::Display::Flex : layout::Display::None;
    if (button.style().display == wanted) return;
    button.style().display = wanted;
    button.requestLayout();
  }

  UiContext* ui;
  core::tree::WidgetId toolbar;
  CommandServices services;
  std::vector<Bound> bound;
  bool alive = true;
};

CommandToolbarBinding::~CommandToolbarBinding() {
  if (state_) state_->alive = false;
}

void CommandToolbarBinding::refresh() {
  if (state_) state_->refresh();
}

core::tree::WidgetId CommandToolbarBinding::toolbar() const { return state_ ? state_->toolbar : core::tree::WidgetId{}; }

ToolbarButton* CommandToolbarBinding::button(std::string_view commandId) {
  Toolbar* t = state_ ? state_->bar() : nullptr;
  if (t == nullptr) return nullptr;
  for (const State::Bound& b : state_->bound) {
    if (std::find(b.ids.begin(), b.ids.end(), commandId) != b.ids.end()) return t->button(b.buttonIndex);
  }
  return nullptr;
}

namespace {

std::string iconOf(const cmd::CommandDef& command) { return command.icon.empty() ? "circle" : command.icon; }

}  // namespace

std::unique_ptr<CommandToolbarBinding> bindCommandToolbar(UiContext& ui, Toolbar& toolbar, CommandUiSync& sync, std::vector<CommandToolbarItem> items) {
  std::unique_ptr<CommandToolbarBinding> binding(new CommandToolbarBinding());
  binding->state_ = std::make_shared<CommandToolbarBinding::State>(ui, toolbar.id(), sync.services());
  const std::shared_ptr<CommandToolbarBinding::State> state = binding->state_;
  const CommandServices& services = sync.services();

  toolbar.setOnTool([state](const std::string& id) { state->activated(id); });
  for (const CommandToolbarItem& item : items) {
    if (item.kind == CommandToolbarItem::Kind::Separator) {
      toolbar.addSeparator();
      continue;
    }
    if (item.kind == CommandToolbarItem::Kind::Group) {
      std::vector<ToolEntry> entries;
      CommandToolbarBinding::State::Bound bound;
      bound.group = true;
      for (const std::string& id : item.commandIds) {
        const cmd::CommandDef* command = services.registry.find(id);
        if (command == nullptr) continue;
        entries.push_back({id, iconOf(*command), command->label, services.keymap.displayText(id)});
        bound.ids.push_back(id);
      }
      if (entries.empty()) continue;
      toolbar.addToolGroup(std::move(entries));
      bound.buttonIndex = toolbar.buttonCount() - 1;
      state->bound.push_back(std::move(bound));
      continue;
    }
    const std::string& id = item.commandIds.front();
    const cmd::CommandDef* command = services.registry.find(id);
    if (command == nullptr) continue;
    const std::string tip = commandTooltip(services, *command, command->tooltip);
    switch (command->kind) {
      case cmd::CommandKind::Toggle: toolbar.addToggle(id, iconOf(*command), tip, [state](ToolbarButton& b) { state->activated(b.toolId()); }); break;
      case cmd::CommandKind::Radio: toolbar.addTool(id, iconOf(*command), tip); break;
      default: toolbar.addAction(id, iconOf(*command), tip, [state](ToolbarButton& b) { state->activated(b.toolId()); }); break;
    }
    state->bound.push_back({{id}, toolbar.buttonCount() - 1, false});
  }
  binding->attachment_ = sync.attach([state] { state->refresh(); });
  state->refresh();
  return binding;
}

}  // namespace r1ui::widgets
