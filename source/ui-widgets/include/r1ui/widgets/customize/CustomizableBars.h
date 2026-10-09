// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CustomizableMenuBar and CustomizableToolbar, the host widgets that show the EFFECTIVE layouts of
//   the customization model live and switch to the edit displays (MenuEditor, ToolbarEditor) while the
//   controller is in edit mode.
// Why: slices 5.7 and 5.8 feed the existing command binders: outside edit mode the child is exactly
//   what bindCommandMenuBar / bindCommandToolbar build for the layout (so a customization-free
//   layout is pixel-equal to the command-built widgets), and every customization change rebuilds
//   it at once (live apply); inside edit mode the child is the edit display.
// Callers: hosts (instead of creating a MenuBar / Toolbar and binding it), the gallery, tests. Calls:
//   CustomizeController (model, sync), LayoutConvert, bindCommandToolbar, MenuBar, Toolbar.
// Menu bar: the MenuBar gets one menu per effective menu; closed menus are rebuilt right before they
//   open (live shortcut text, enabled and checked state), user labels of command entries are patched in.
//   The user's size step and gap are applied to the toolbar's buttons and gaps only when they differ from
//   the defaults, so an untouched toolbar is the unmodified Toolbar widget.
// Toolbar: the effective items are bound in runs (a spacer ends a run and becomes a flexible gap in the
//   toolbar); the size step sets the button edge (small 26, medium 32 = the toolbar's own, large 40) and
//   the gap the distance between items.
// Rebuild rule: a change of edit mode, or (outside edit mode) a change of the model version, replaces the
//   child; the edit displays follow the model themselves. The widget never keeps a pointer into the
//   model.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "r1ui/widgets/commands/CommandMenus.h"
#include "r1ui/widgets/commands/CommandToolbar.h"
#include "r1ui/widgets/customize/CustomizeController.h"
#include "r1ui/widgets/customize/MenuEditor.h"
#include "r1ui/widgets/customize/ToolbarEditor.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/toolbar/Toolbar.h"

namespace r1ui::widgets {

class CustomizableMenuBar final : public WidgetObject {
 public:
  explicit CustomizableMenuBar(CustomizeController& controller, CommandMenuOptions options = {}) : controller_(controller), options_(std::move(options)) {}
  const char* typeName() const override { return "CustomizableMenuBar"; }
  void onAttached() override;
  void onDetached() override;

  // The bound menu bar (null while editing) and the editor (null otherwise).
  MenuBar* menuBar() const { return ui().objectAs<MenuBar>(child_); }
  MenuEditor* editor() const { return ui().objectAs<MenuEditor>(child_); }
  core::tree::WidgetId childWidget() const { return child_; }
  void rebuild();

 private:
  CustomizeController& controller_;
  CommandMenuOptions options_;
  CustomizeController::ListenerId listener_ = 0;
  core::tree::WidgetId child_;
  uint64_t builtVersion_ = 0;
  bool builtEditing_ = false;
};

class CustomizableToolbar final : public WidgetObject {
 public:
  CustomizableToolbar(CustomizeController& controller, std::string toolbarId) : controller_(controller), toolbarId_(std::move(toolbarId)) {}
  const char* typeName() const override { return "CustomizableToolbar"; }
  void onAttached() override;
  void onDetached() override;

  // The bound toolbar (null while editing) and the editor (null otherwise).
  Toolbar* toolbar() const { return ui().objectAs<Toolbar>(child_); }
  ToolbarEditor* editor() const { return ui().objectAs<ToolbarEditor>(child_); }
  core::tree::WidgetId childWidget() const { return child_; }
  const std::string& toolbarId() const { return toolbarId_; }
  // The button of a command in the bound toolbar (null when not shown).
  ToolbarButton* button(std::string_view commandId) const;
  void rebuild();

 private:
  void applyLook(Toolbar& toolbar, const commands::customize::ToolbarLayout& layout);

  CustomizeController& controller_;
  std::string toolbarId_;
  CustomizeController::ListenerId listener_ = 0;
  core::tree::WidgetId child_;
  std::vector<std::unique_ptr<CommandToolbarBinding>> bindings_;
  uint64_t builtVersion_ = 0;
  bool builtEditing_ = false;
};

}  // namespace r1ui::widgets
