// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CustomizeToolStrip.h.
// Invariants: the strip only mirrors the controller (button state, message line) and calls registry
//   commands or model operations; it keeps no customization state; edit-only controls are enabled
//   exactly while the controller is in edit mode.
// Callers: hosts, the gallery, tests.
#include "r1ui/widgets/customize/CustomizeToolStrip.h"

#include "CustomizeBox.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/menu/MenuModel.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace cz = commands::customize;

void CustomizeToolStrip::onAttached() {
  style().direction = layout::FlexDirection::Row;
  style().alignItems = layout::Align::Center;
  style().gapColumn = 6.0;
  style().flexShrink = 0.0;
  style().alignSelf = layout::Align::Start;
  restoreMenu_ = std::make_unique<MenuController>(ui());
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  const auto runCommand = [context, self](const char* commandId) {
    CustomizeToolStrip* strip = context->objectAs<CustomizeToolStrip>(self);
    if (strip != nullptr) strip->controller_.services().router.execute(commandId, commands::ExecuteSource::Toolbar);
  };
  const auto makeButton = [&](const char* text, const char* icon, const char* tooltip, std::function<void()> onClick) {
    Button& b = ui().create<Button>(id(), text, ButtonTone::Panel, ButtonSize::Sm);
    if (icon != nullptr && *icon != '\0') b.setIcon(icon);
    b.setTooltip(tooltip);
    b.setOnClick(std::move(onClick));
    return b.id();
  };
  toggle_ = makeButton("Customize", "sliders-horizontal", "Edit the menus, toolbars and panels", [runCommand] { runCommand(kCmdCustomizeToggle); });
  newMenu_ = makeButton("New menu", "plus", "Create a menu of your own", [runCommand] { runCommand(kCmdCustomizeNewMenu); });
  restore_ = makeButton("Restore...", "eye", "Show hidden entries again", [context, self] {
    CustomizeToolStrip* strip = context->objectAs<CustomizeToolStrip>(self);
    if (strip == nullptr) return;
    const layout::Rect r = context->absRect(strip->restore_);
    strip->openRestoreList(r.x, r.y + r.h + 4.0);
  });
  resetMenu_ = makeButton("Reset menu", "", "Return the menu you are editing to its original state", [context, self] {
    CustomizeToolStrip* strip = context->objectAs<CustomizeToolStrip>(self);
    if (strip == nullptr || strip->controller_.currentMenu().empty()) return;
    strip->controller_.noteResult(strip->controller_.model().resetMenu(strip->controller_.currentMenu()));
  });
  revert_ = makeButton("Revert changes", "undo2", "Return to how things were when you started customizing", [runCommand] { runCommand(kCmdCustomizeRevert); });
  resetAll_ = makeButton("Reset all...", "trash-2", "Remove every customization", [runCommand] { runCommand(kCmdCustomizeResetAll); });
  Label& message = ui().create<Label>(id(), "", LabelRole::Danger);
  message_ = message.id();
  message.style().flexShrink = 1.0;
  listener_ = controller_.subscribe([context, self] {
    if (CustomizeToolStrip* strip = context->objectAs<CustomizeToolStrip>(self)) strip->sync();
  });
  messageListener_ = controller_.subscribeMessage([context, self] {
    if (CustomizeToolStrip* strip = context->objectAs<CustomizeToolStrip>(self)) strip->sync();
  });
  sync();
}

void CustomizeToolStrip::onDetached() {
  controller_.unsubscribe(listener_);
  controller_.unsubscribeMessage(messageListener_);
  if (restoreMenu_) restoreMenu_->close();
}

void CustomizeToolStrip::sync() {
  const bool editing = controller_.editMode();
  if (Button* b = ui().objectAs<Button>(toggle_)) b->setTone(editing ? ButtonTone::Accent : ButtonTone::Panel);
  for (const core::tree::WidgetId id : {newMenu_, restore_, resetMenu_, revert_, resetAll_}) {
    if (WidgetObject* w = ui().object(id)) w->setEnabled(editing);
  }
  if (Button* b = ui().objectAs<Button>(revert_)) b->setEnabled(editing && controller_.model().sessionChanged());
  if (Label* l = ui().objectAs<Label>(message_)) l->setText(controller_.lastRefusal());
}

bool CustomizeToolStrip::openRestoreList(double x, double y) {
  const std::vector<cz::RestoreItem> items = controller_.model().restoreList();
  MenuSpec spec;
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  if (items.empty()) {
    MenuItemSpec none = menuAction("customize:none", "Nothing is hidden");
    none.enabled = false;
    spec.items.push_back(std::move(none));
  }
  for (const cz::RestoreItem& item : items) {
    MenuItemSpec row = menuAction("customize:restore:" + item.id, "Show " + item.path);
    row.onActivate = [context, self, nodeId = item.id](const MenuItemSpec&) {
      if (CustomizeToolStrip* strip = context->objectAs<CustomizeToolStrip>(self)) strip->controller_.noteResult(strip->controller_.model().showEntry(nodeId));
    };
    spec.items.push_back(std::move(row));
  }
  spec.onCommand = [](const MenuItemSpec&) {};
  return restoreMenu_->openContextMenu(std::move(spec), x, y);
}

}  // namespace r1ui::widgets
