// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CustomizeToolStrip, the row of controls that goes with edit mode: the Customize toggle, New
//   menu, the restore list of hidden entries, Reset menu, Revert changes, Reset all (with confirmation)
//   and a message line that shows why the last edit was refused.
// Why: spec 06 rules 28 to 31: a revert-session action, a reset-this-menu action without confirmation,
//   a reset-everything action that asks first (default answer: No), and the restore list that decision
//   D3 asks for (hidden built-in and user entries come back from one list).
// Callers: hosts and the gallery (above the menu bar or in a settings panel). Calls: CustomizeController,
//   the registry commands customize.*, MenuController (the restore popup).
// Buttons run the registered customize.* commands through the router, so keyboard shortcuts and menu
//   entries of the same commands behave identically; "Reset menu" and "Restore" act on the model directly
//   (the current menu is the one the menu editor shows). Edit-only controls are disabled outside edit mode.
#pragma once

#include <memory>
#include <string>

#include "r1ui/widgets/customize/CustomizeController.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class CustomizeToolStrip final : public WidgetObject {
 public:
  explicit CustomizeToolStrip(CustomizeController& controller) : controller_(controller) {}
  const char* typeName() const override { return "CustomizeToolStrip"; }
  void onAttached() override;
  void onDetached() override;

  core::tree::WidgetId toggleButton() const { return toggle_; }
  core::tree::WidgetId newMenuButton() const { return newMenu_; }
  core::tree::WidgetId restoreButton() const { return restore_; }
  core::tree::WidgetId resetMenuButton() const { return resetMenu_; }
  core::tree::WidgetId revertButton() const { return revert_; }
  core::tree::WidgetId resetAllButton() const { return resetAll_; }
  core::tree::WidgetId messageLabel() const { return message_; }
  // Opens the restore list under its button (or at a point); false when nothing is hidden.
  bool openRestoreList(double x, double y);
  MenuController& restoreMenu() { return *restoreMenu_; }

 private:
  void sync();

  CustomizeController& controller_;
  CustomizeController::ListenerId listener_ = 0;
  CustomizeController::ListenerId messageListener_ = 0;
  core::tree::WidgetId toggle_, newMenu_, restore_, resetMenu_, revert_, resetAll_, message_;
  std::unique_ptr<MenuController> restoreMenu_;
};

}  // namespace r1ui::widgets
