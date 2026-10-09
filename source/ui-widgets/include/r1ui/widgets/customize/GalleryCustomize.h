// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery page of the customization group: a sample registry of 24 commands (the command
//   gallery's set plus shapes and help), the built-in layouts of a menu bar (File, Edit, View, Tools and
//   a locked Help menu), three toolbars (main tools, a vertical side bar, a locked one) and a
//   free-form panel, the Customization over them with an in-memory user store, the controller, and the
//   widgets bound to it: the tool strip with the Customize toggle, the customizable menu bar, the
//   toolbars, the free-form panel and the command palette.
// Why: the Phase 5 gallery and the preview show slices 5.7 to 5.9 with real widgets: toggle Customize,
//   drag commands from the palette onto a menu, a toolbar or the panel, hide and restore entries, rename
//   in place, create a menu, resize toolbar buttons, move and resize panel buttons, revert the session,
//   and see the menus and toolbars change live.
// Callers: the gallery preview (mounts it under any container), the group's gallery tests.
// Contract: builds children of `parent` only (one column, typeName "GalleryCustomize"); the page owns its
//   registry, overrides, keymap, router, sync, customization and controller and everything goes away with
//   the page. Nothing is global. Chords work while a widget of the page has focus (the page forwards the
//   keys its widgets leave unused to the router).
#pragma once

#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/customize/CommandPalette.h"
#include "r1ui/widgets/customize/CustomizableBars.h"
#include "r1ui/widgets/customize/CustomizeController.h"
#include "r1ui/widgets/customize/CustomizeToolStrip.h"
#include "r1ui/widgets/customize/FreeFormPanel.h"

namespace r1ui::widgets {

class UiContext;

class GalleryCustomizePage : public WidgetObject {
 public:
  GalleryCustomizePage();
  ~GalleryCustomizePage() override;
  const char* typeName() const override { return "GalleryCustomize"; }
  void onAttached() override;
  void onDetached() override;
  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;

  CustomizeController& controller() const;
  commands::customize::Customization& model() const;
  commands::CommandRegistry& registry() const;
  CommandUiSync& sync() const;
  CustomizableMenuBar& menuBar() const;
  CustomizableToolbar& mainToolbar() const;
  CustomizableToolbar& sideToolbar() const;
  CustomizableToolbar& lockedToolbar() const;
  FreeFormPanel& panel() const;
  CommandPalette& palette() const;
  CustomizeToolStrip& strip() const;
  const std::string& status() const;

  struct State;

 private:
  std::unique_ptr<State> state_;
};

GalleryCustomizePage& createGalleryCustomize(UiContext& ui, core::tree::WidgetId parent);
void buildGalleryCustomize(UiContext& ui, core::tree::WidgetId parent);

}  // namespace r1ui::widgets
