// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CustomMenuPanel, the content of a dockable custom menu (owner requirement 2026-10-10: "a
//   user-created panel of action buttons - grid/list with labels and icons - that can be docked, floated
//   and reopened"), CustomMenuButton (one action button) and the factory that makes the panel content for
//   the dock's PanelRegistry.
// Why: a dockable menu is a CustomMenu of kind Panel in the CustomMenuSet; this widget shows it and keeps
//   showing it correctly while the menu is edited (rebuilt at once), while commands change state (enabled,
//   checked, tooltip refreshed live through CommandUiSync) and while the registry gains or loses commands.
// Callers: the host's dock (PanelFactory from makeCustomMenuPanelFactory), the gallery, tests.
//   Calls: CommandServices (execution through the router), CommandUiSync (live refresh), CustomMenuSet.
// Layout: a scrolling column of rows; each row holds `columns` buttons of equal width and `buttonSize`
//   logical px height (a short last row keeps the column widths). A button shows icon and label side by
//   side, or icon above label when it is at least 56 px high, or the icon alone when labels are off.
//   Entry overrides (label, icon) win over the command's own text.
// Button state: enabled follows the command's enabled predicate (a disabled button is dimmed and does
//   nothing); checked (toggle or radio that is on) gets an accent outline; a MISSING command (not
//   registered: a menu loaded on a clean installation, a removed plugin) stays in the panel, dimmed, with
//   the id as its label and a tooltip saying the command is not available - nothing is dropped. The
//   tooltip is the command's tooltip or description plus the chord in brackets.
// Execution: a click or Enter/Space runs router.execute(commandId, Toolbar); a refusal or a throwing
//   command changes nothing here (the router contains it).
// Lifetime: the set, the sync hub and the services must outlive the UiContext (the panel unsubscribes in
//   onDetached). A menu that disappears from the set leaves the panel showing a "deleted" notice.
// Threading: UI thread only.
#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/commands/custommenu/CustomMenuSet.h"
#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/button/Pressable.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/dock/PanelRegistry.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

// What a button shows right now; compared as a whole so a refresh repaints only on a real change.
struct CustomMenuButtonState {
  std::string label;
  std::string icon;
  std::string tooltip;
  bool showLabel = true;
  bool available = true;  // false: disabled by its predicate or missing
  bool checked = false;
  bool missing = false;
  friend bool operator==(const CustomMenuButtonState&, const CustomMenuButtonState&) = default;
};

class CustomMenuButton final : public Pressable {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();

  CustomMenuButton(std::string commandId, std::function<void(const std::string& commandId)> onActivate)
      : commandId_(std::move(commandId)), onActivate_(std::move(onActivate)) {}

  const char* typeName() const override { return "CustomMenuButton"; }
  void onAttached() override;
  uint8_t styleState() const override;
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override { return state_.available ? Cursor::Pointer : Cursor::Default; }
  std::string_view tooltipText() const override { return state_.tooltip; }
  std::string_view accessibleName() const override { return state_.label; }

  const std::string& commandId() const { return commandId_; }
  const CustomMenuButtonState& state() const { return state_; }
  // Applies a new state; true when something changed (a repaint was requested).
  bool setState(CustomMenuButtonState state);

 protected:
  void activate() override;

 private:
  std::string commandId_;
  std::function<void(const std::string&)> onActivate_;
  CustomMenuButtonState state_;
};

class CustomMenuPanel final : public WidgetObject {
 public:
  CustomMenuPanel(CommandServices services, CommandUiSync& sync, commands::custommenu::CustomMenuSet& set, std::string menuId);

  const char* typeName() const override { return "CustomMenuPanel"; }
  void onAttached() override;
  void onDetached() override;

  const std::string& menuId() const { return menuId_; }
  size_t buttonCount() const { return buttons_.size(); }
  // The i-th button in entry order, or nullptr.
  CustomMenuButton* button(size_t index) const;
  // True while the menu exists in the set.
  bool menuExists() const;
  // Re-reads every command's state now (the sync does it once per frame).
  void refresh();
  // Rebuilds the buttons from the set (done automatically on every change of the set).
  void rebuild();

 private:
  CustomMenuButtonState stateFor(const commands::custommenu::MenuEntry& entry, bool showLabels) const;
  void activateCommand(const std::string& commandId);

  CommandServices services_;
  CommandUiSync& sync_;
  commands::custommenu::CustomMenuSet& set_;
  std::string menuId_;
  commands::custommenu::CustomMenuSet::ListenerId listener_ = 0;
  CommandUiSync::Attachment attachment_;
  core::tree::WidgetId scroll_;
  core::tree::WidgetId content_;
  std::vector<core::tree::WidgetId> rows_;     // row containers and the notice, destroyed on rebuild
  std::vector<core::tree::WidgetId> buttons_;
  std::vector<commands::custommenu::MenuEntry> entries_;  // what the buttons were built from
  bool showLabels_ = true;
};

// Panel content factory for PanelRegistry: builds a CustomMenuPanel for `menuId` (the panel shows a
// notice when the menu is gone, so the factory is safe for a panel that outlives its menu).
PanelFactory makeCustomMenuPanelFactory(CommandServices services, CommandUiSync& sync, commands::custommenu::CustomMenuSet& set, std::string menuId);

// The registry entry of a dockable menu: title = the menu's name, factory as above, the menu's default
// window size when it has one, icon "layout-panel-top". `panelId` is chosen by the host (for example a
// fixed base plus the menu's serial); ids must be unique and non-zero. The title is not updated by the
// panel: the host calls PanelRegistry::setTitle when the menu is renamed.
PanelDescriptor describeCustomMenuPanel(const commands::custommenu::CustomMenu& menu, dock::PanelId panelId, PanelFactory factory);

}  // namespace r1ui::widgets
