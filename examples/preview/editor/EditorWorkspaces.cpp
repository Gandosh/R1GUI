// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Editor's custom workspaces (slice 5.18/5.19): Layout > Save Custom Workspace... packs the
//   arrangement on screen, the custom menus, the menu and toolbar customization and the key bindings into a
//   .r1ws file; Load Custom Workspace... validates every part and then applies them.
// Why: owner requirement 2026-10-10: carry a whole setup to another machine or restore it after a clean
//   installation. The container (ui-commands/workspace) never interprets a part; the owners' own strict
//   loaders do, and this file orders the work so that a bad file changes nothing.
// Invariants: every part is checked before anything is applied (the menus and the customization on a copy,
//   the key bindings on a scratch table); the layout can only be checked against the panels of the loaded
//   menus, so the menus are applied first and put back when the layout is then refused; the folder is
//   %LOCALAPPDATA%/R1GUI/preview/workspaces and the dialog lists its files (the workspace list).
// Callers: the commands workspace.save / workspace.load.
#include <algorithm>

#include "EditorApp.h"
#include "EditorMenus.h"
#include "r1ui/commands/custommenu/CustomMenu.h"
#include "r1ui/dock/DockLayout.h"

namespace preview::editor {

namespace rw = r1ui::widgets;
namespace rc = r1ui::commands;
namespace cm = r1ui::commands::custommenu;
namespace ws = r1ui::commands::workspace;
namespace dk = r1ui::dock;
namespace fs = std::filesystem;

namespace {

std::string utf8Stem(const fs::path& path) {
  const std::u8string text = path.stem().u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

}  // namespace

void EditorApp::askSaveWorkspace() {
  rw::FilePathOptions options;
  options.mode = rw::FilePathMode::Save;
  options.title = "Save custom workspace";
  options.description = "Packs the panel layout, your custom menus, the menu and toolbar customization and the key bindings into one .r1ws file.";
  options.folder = workspacesFolder();
  options.extension = ws::kWorkspaceExtension;
  options.initialName = "My workspace";
  chooseFile(ui_, root_, std::move(options), [this](const fs::path& path) { saveWorkspaceTo(path); });
}

void EditorApp::askLoadWorkspace() {
  rw::FilePathOptions options;
  options.mode = rw::FilePathMode::Open;
  options.title = "Load custom workspace";
  options.description = "Replaces the panel layout, the custom menus, the customization and the key bindings with the ones in the file. A file with a problem changes nothing.";
  options.folder = workspacesFolder();
  options.extension = ws::kWorkspaceExtension;
  chooseFile(ui_, root_, std::move(options), [this](const fs::path& path) { loadWorkspaceFrom(path); });
}

bool EditorApp::saveWorkspaceTo(const fs::path& file) {
  ws::Workspace workspace;
  workspace.name = cm::cleanName(utf8Stem(ws::withWorkspaceExtension(file)));
  if (workspace.name.empty()) workspace.name = "Workspace";
  workspace.layout = dock().layout().toJson();
  workspace.menus = cm::exportSet(menuSet_);
  workspace.customization = rc::customize::exportCustomization(customization_);
  workspace.keybindings = rc::exportOverrides(registry_, overrides_);
  std::string error;
  const fs::path target = ws::withWorkspaceExtension(file);
  std::error_code ec;
  if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
  if (!ws::saveWorkspaceFile(workspace, target, error)) {
    model_.log(LogLevel::Error, "save workspace: " + error);
    setStatus("The workspace was not saved: " + error);
    return false;
  }
  model_.info("Custom workspace \"" + workspace.name + "\" saved to " + target.string());
  setStatus("Saved the custom workspace " + target.string());
  return true;
}

bool EditorApp::loadWorkspaceFrom(const fs::path& file) {
  const auto refuse = [this](const std::string& why) {
    model_.log(LogLevel::Error, "load workspace: " + why);
    setStatus("The workspace was not loaded: " + why);
    return false;
  };
  const ws::WorkspaceParseResult parsed = ws::loadWorkspaceFile(file);
  if (!parsed.ok) return refuse(parsed.error);
  const ws::Workspace& workspace = parsed.workspace;

  // 1. Check every part that can be checked without changing anything.
  cm::SetParseResult menus;
  if (workspace.menus) {
    menus = cm::parseSet(*workspace.menus);
    if (!menus.ok) return refuse("the custom menus: " + menus.error);
  }
  if (workspace.customization) {
    rc::customize::Customization scratch(customization_.builtin());
    const rc::customize::LoadReport report = rc::customize::importCustomization(scratch, *workspace.customization);
    if (!report.ok) return refuse("the customization: " + report.error);
  }
  if (workspace.keybindings) {
    rc::KeybindingOverrides scratch(registry_);
    const rc::ImportReport report = rc::importOverrides(*workspace.keybindings, registry_, scratch, rc::ImportMode::Replace);
    if (!report.ok) return refuse("the key bindings: " + report.error);
  }

  // 2. The menus first: their dock panels must exist before the layout is read. Put them back when the
  //    layout turns out to be unusable.
  const cm::SetParseResult previous = cm::parseSet(cm::exportSet(menuSet_));
  if (workspace.menus) {
    const cm::MenuEditResult replaced = menuSet_.replaceAll(menus.menus, menus.nextSerial);
    if (!replaced.ok) return refuse("the custom menus: " + replaced.reason);
    syncMenuPanels();
  }
  std::optional<dk::DockLayout> layout;
  if (workspace.layout) {
    dk::LoadResult loaded = dk::DockLayout::fromJson(*workspace.layout, dock().panels(), dock().config());
    if (!loaded.ok()) {
      if (workspace.menus && previous.ok) menuSet_.replaceAll(previous.menus, std::max(previous.nextSerial, menuSet_.nextSerial()));
      return refuse("the layout: " + loaded.error);
    }
    layout = std::move(loaded.layout);
  }

  // 3. Apply.
  std::string applied;
  if (layout) {
    if (const dk::Status status = dock().applyLayout(std::move(*layout)); !status) return refuse("the layout: " + status.error);
    if (layouts_) layouts_->notifyChanged();
    setActiveKey({});
    applied += " layout";
  }
  if (workspace.menus) applied += " menus";
  if (workspace.customization) {
    rc::customize::importCustomization(customization_, *workspace.customization);
    applied += " customization";
  }
  if (workspace.keybindings) {
    rc::importOverrides(*workspace.keybindings, registry_, overrides_, rc::ImportMode::Replace);
    applied += " keybindings";
  }
  if (const std::optional<cm::CustomMenu> pie = viewportPie()) rememberViewportPie(pie->id);
  model_.info("Custom workspace \"" + workspace.name + "\" loaded from " + file.string() + " (" + applied.substr(applied.empty() ? 0 : 1) + ")");
  setStatus("Loaded the custom workspace " + workspace.name);
  registry_.touch();
  return true;
}

}  // namespace preview::editor
