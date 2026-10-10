// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of EditorMenus.h: the built-in layouts as flat command lists converted with the
//   toolkit's LayoutConvert helpers.
// Invariants: every menu and toolbar id is stable; a command id that is not registered shows up as a
//   missing entry in the edit display and is dropped from the normal one, never an error.
// Callers: EditorApp.
#include "EditorMenus.h"

#include "EditorApp.h"
#include "r1ui/widgets/commands/CommandMenus.h"
#include "r1ui/widgets/commands/CommandToolbar.h"
#include "r1ui/widgets/custommenu/CustomMenusMenu.h"
#include "r1ui/widgets/customize/LayoutConvert.h"

namespace preview::editor {

namespace cmd {
std::string panelToggle(unsigned panelId) { return "window.panel." + std::to_string(panelId); }
std::string screenCommand(int screen) { return "window.screen." + std::to_string(screen); }
}  // namespace cmd

namespace cz = r1ui::commands::customize;
using E = r1ui::widgets::CommandMenuEntry;
using I = r1ui::widgets::CommandToolbarItem;

cz::LayoutSet editorLayoutSet(const std::vector<std::string>& screenNames, const r1ui::commands::custommenu::CustomMenuSet* menus) {
  cz::LayoutSet set;
  const auto menu = [&](const char* id, const char* title, const std::vector<E>& entries) {
    set.menuBar.menus.push_back(r1ui::widgets::menuNodeFromEntries(id, title, entries));
  };
  menu("menu.file", "File", {E::command(cmd::kFileNew), E::command(cmd::kFileSave), E::separator(), E::command(cmd::kFileExit)});
  menu("menu.edit", "Edit",
       {E::command(cmd::kUndo), E::command(cmd::kRedo), E::separator(), E::command(cmd::kSelectAll), E::command(cmd::kDeselect), E::command(cmd::kResetValues), E::separator(),
        E::command(cmd::kShortcuts)});
  menu("menu.view", "View", {E::command(cmd::kTheme), E::separator(), E::command(cmd::kGrid), E::command(cmd::kLights), E::separator(), E::command(cmd::kFrame), E::command(cmd::kWireframe)});
  menu("menu.tools", "Tools", {E::command(cmd::kToolSelect), E::command(cmd::kToolMove), E::command(cmd::kToolRotate), E::command(cmd::kToolScale)});

  std::vector<E> panels;
  for (const unsigned id : panel::kStandard) panels.push_back(E::command(cmd::panelToggle(id)));
  std::vector<E> screens;
  for (size_t i = 0; i < screenNames.size(); ++i) screens.push_back(E::command(cmd::screenCommand(static_cast<int>(i))));
  std::vector<E> windowMenu = {E::submenu("Panels", std::move(panels)), E::separator(), E::command(cmd::kFloatTab), E::command(cmd::kMoveStack), E::separator(),
                               E::command(cmd::kNextTab), E::command(cmd::kPrevTab), E::command(cmd::kCloseTab)};
  if (!screens.empty()) {
    windowMenu.push_back(E::separator());
    windowMenu.push_back(E::submenu("Screen", std::move(screens)));
  }
  menu("menu.window", "Window", windowMenu);

  menu("menu.layout", "Layout",
       {E::command(cmd::kLayoutDefault), E::command(cmd::kLayoutModeling), E::command(cmd::kLayoutReview), E::command(cmd::kLayoutSwitch), E::separator(),
        E::command(cmd::kLayoutSave), E::command(cmd::kLayoutSaveAs), E::command(cmd::kLayoutRename), E::command(cmd::kLayoutDelete), E::separator(), E::command(cmd::kLayoutReset),
        E::separator(), E::command(cmd::kWorkspaceSave), E::command(cmd::kWorkspaceLoad)});
  menu("menu.help", "Help", {E::command(cmd::kShortcuts), E::command(cmd::kAbout)});

  set.toolbars.push_back(r1ui::widgets::toolbarFromItems(
      kToolbarMain, "Main toolbar",
      {I::command(cmd::kToolSelect), I::command(cmd::kToolMove), I::command(cmd::kToolRotate), I::command(cmd::kToolScale), I::separator(), I::command(cmd::kUndo), I::command(cmd::kRedo),
       I::separator(), I::command(cmd::kGrid), I::command(cmd::kLights), I::separator(), I::command(cmd::kTheme)}));

  // The user's own menus come last; its entries are real commands registered by CustomMenuCommands.
  if (menus != nullptr) set.menuBar.menus.push_back(r1ui::widgets::customMenusMenuNode(*menus));
  return set;
}

}  // namespace preview::editor
