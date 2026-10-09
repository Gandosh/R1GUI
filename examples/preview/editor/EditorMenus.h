// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the built-in menu bar, toolbar and free-form panel layouts of the Editor screen and the stable
//   command ids they and the command registry share.
// Why: the customization model (user menus, hidden entries, moved commands) is a delta over these
//   layouts, so the ids here are a compatibility surface: renaming one orphans users' customization.
// Callers: EditorApp (builds the Customization over it), EditorCommands (declares the ids), tests.
#pragma once

#include <string>
#include <vector>

#include "r1ui/commands/customize/Layout.h"

namespace preview::editor {

// Command ids (the registry keys, the menu entries and the saved keybinding overrides use them).
namespace cmd {
inline constexpr const char* kFileNew = "file.new";
inline constexpr const char* kFileSave = "file.save";
inline constexpr const char* kFileExit = "file.exit";
inline constexpr const char* kUndo = "edit.undo";
inline constexpr const char* kRedo = "edit.redo";
inline constexpr const char* kSelectAll = "edit.selectAll";
inline constexpr const char* kDeselect = "edit.deselect";
inline constexpr const char* kResetValues = "edit.resetValues";
inline constexpr const char* kShortcuts = "edit.shortcuts";
inline constexpr const char* kCustomize = "edit.customize";
inline constexpr const char* kTheme = "view.darkTheme";
inline constexpr const char* kGrid = "view.grid";
inline constexpr const char* kLights = "view.lights";
inline constexpr const char* kToolSelect = "tool.select";
inline constexpr const char* kToolMove = "tool.move";
inline constexpr const char* kToolRotate = "tool.rotate";
inline constexpr const char* kToolScale = "tool.scale";
inline constexpr const char* kFloatTab = "window.floatTab";
inline constexpr const char* kMoveStack = "window.moveStack";
inline constexpr const char* kNextTab = "window.nextTab";
inline constexpr const char* kPrevTab = "window.prevTab";
inline constexpr const char* kCloseTab = "window.closeTab";
inline constexpr const char* kLayoutDefault = "layout.default";
inline constexpr const char* kLayoutModeling = "layout.modeling";
inline constexpr const char* kLayoutReview = "layout.review";
inline constexpr const char* kLayoutSave = "layout.save";
inline constexpr const char* kLayoutSaveAs = "layout.saveAs";
inline constexpr const char* kLayoutRename = "layout.rename";
inline constexpr const char* kLayoutDelete = "layout.delete";
inline constexpr const char* kLayoutReset = "layout.reset";
inline constexpr const char* kLayoutSwitch = "layout.switch";
inline constexpr const char* kAbout = "help.about";
// "window.panel.<n>" toggles panel n; "window.screen.<n>" shows preview screen n.
std::string panelToggle(unsigned panelId);
std::string screenCommand(int screen);
}  // namespace cmd

// Menus File, Edit, View, Tools, Window, Layout, Help; `screenNames` fill Window > Screen.
r1ui::commands::customize::LayoutSet editorLayoutSet(const std::vector<std::string>& screenNames);

inline constexpr const char* kToolbarMain = "tb.main";
inline constexpr const char* kPanelQuick = "fp.quick";

}  // namespace preview::editor
