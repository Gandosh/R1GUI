// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Editor's layout management: the three built-in arrangements (Default, Modeling, Review), the
//   first-run seeding of named layouts, switching, saving, saving as, renaming, deleting, resetting, the
//   layout drop-down and the small dialogs those operations need.
// Why: spec 04 asks for named layouts, auto-save and a safe restore; dock::LayoutManager does the files,
//   this file turns it into commands and widgets and remembers which named layout is active (the manager
//   restores the arrangement from "_active" but not its name).
// Invariants: a rejected load or save changes nothing and is reported in the console and status line; the
//   active layout key is persisted atomically beside the layouts; a dialog callback runs only while the
//   app lives (alive_ flag) and re-checks every id it uses.
// Callers: EditorApp (startup, update) and the layout commands.
#include <algorithm>

#include "EditorApp.h"
#include "EditorMenus.h"
#include "r1ui/dock/DockLayout.h"
#include "r1ui/widgets/dialog/Dialog.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace preview::editor {

namespace rw = r1ui::widgets;
namespace dk = r1ui::dock;
using dk::Axis;
using dk::Node;

namespace {

constexpr const char* kBuiltinNames[3] = {"Default", "Modeling", "Review"};

}  // namespace

// ---- built-in arrangements ------------------------------------------------------------------

dk::DockLayout EditorApp::builtinLayout(int which) const {
  using namespace panel;
  Node root;
  switch (which) {
    case 1:  // Modeling: a big viewport, curves and console below, the inspector and tools on the right
      root = Node::split(Axis::Row, {Node::stack({kOutliner, kAssets}, 0, 1.0),
                                     Node::split(Axis::Column, {Node::stack({kViewport}, 0, 4.0), Node::stack({kCurves, kConsole}, 0, 1.1)}, 4.6),
                                     Node::stack({kInspector, kCommands, kQuickActions}, 0, 1.4)});
      break;
    case 2:  // Review: the viewport on top, console, inspector and outliner in a strip below
      root = Node::split(Axis::Column, {Node::stack({kViewport}, 0, 3.2), Node::split(Axis::Row, {Node::stack({kConsole}, 0, 1.0), Node::stack({kInspector}, 0, 1.0), Node::stack({kOutliner, kAssets}, 0, 1.0)}, 1.2)});
      break;
    default:  // Default: outliner and assets left, viewport over console/actions/curves, inspector over the palette right
      root = Node::split(Axis::Row, {Node::split(Axis::Column, {Node::stack({kOutliner}, 0, 1.0), Node::stack({kAssets}, 0, 1.0)}, 1.0),
                                     Node::split(Axis::Column, {Node::stack({kViewport}, 0, 3.0), Node::stack({kConsole, kQuickActions, kCurves}, 0, 1.4)}, 3.6),
                                     Node::split(Axis::Column, {Node::stack({kInspector}, 0, 1.7), Node::stack({kCommands}, 0, 1.2)}, 1.3)});
      break;
  }
  dk::DockLayoutResult created = dk::DockLayout::create(panels_.infos(), {}, std::move(root));
  if (!created.ok()) throw std::runtime_error("built-in layout " + std::to_string(which) + ": " + created.error);
  return std::move(*created.layout);
}

// ---- first run, startup ---------------------------------------------------------------------

// First run only (no stored active layout, no named layout): the three built-ins become named layouts.
// Default is stored last so the stored active layout is the Default arrangement.
bool EditorApp::seedLayouts() {
  if (layoutStore_.exists("editor", "_active") || !layouts_->list().empty()) return false;
  for (const int which : {1, 2, 0}) {
    if (const r1ui::dock::Status applied = dock().applyLayout(builtinLayout(which)); !applied) {
      model_.log(LogLevel::Error, "seed layout: " + applied.error);
      continue;
    }
    std::string key;
    if (const r1ui::dock::Status saved = layouts_->saveAs(kBuiltinNames[which], "Built-in arrangement", &key); !saved) model_.log(LogLevel::Error, "seed layout: " + saved.error);
    else if (which == 0) setActiveKey(key);
  }
  return true;
}

void EditorApp::startLayouts() {
  layouts_ = std::make_unique<dk::LayoutManager>(layoutStore_, layoutClock_, dock());
  if (const r1ui::dock::Status mode = layouts_->setMode("editor"); !mode) model_.log(LogLevel::Error, "layout mode: " + mode.error);
  layouts_->setDefaultProvider([this] { return builtinLayout(0); });
  if (host_.monitors) layouts_->setMonitorProvider(host_.monitors);
  dock().setDefaultLayout([this] { return builtinLayout(0); });
  dock().setOnChanged([this](rw::DockChange change) {
    if (change == rw::DockChange::DragStarted) {
      layouts_->setSuspended(true);
      router_.setSuppressed(true);
    } else if (change == rw::DockChange::DragEnded) {
      layouts_->setSuspended(false);
      router_.setSuppressed(false);
    } else {
      layouts_->notifyChanged();
    }
    registry_.touch();
  });
  const std::filesystem::path activeFile = host_.dataRoot / "layouts" / "active-layout.txt";
  std::string remembered;
  if (const std::optional<std::string> text = dk::readBoundedFile(activeFile)) remembered = *text;
  const bool firstRun = seedLayouts();
  const dk::LayoutReport report = layouts_->startup();
  if (!report.keptAsideAs.empty()) model_.log(LogLevel::Warning, "the stored layout was damaged and kept as \"" + report.keptAsideAs + "\"; the default arrangement is used");
  for (const std::string& warning : report.warnings) model_.log(LogLevel::Warning, "layout: " + warning);
  if (report.usedDefault && !remembered.empty()) {
    remembered.clear();  // the arrangement is not the named one any more: forget the name on disk too
    setActiveKey({});
  }
  if (!remembered.empty()) activeKey_ = remembered;
  refreshActiveName();
  model_.info(firstRun ? "First start: the Default, Modeling and Review layouts were created" : report.usedDefault ? "Layout: default arrangement" : "Layout restored from the last session");
  started_ = true;
  layoutSelectDirty_ = true;
}

void EditorApp::setActiveKey(const std::string& key) {
  activeKey_ = key;
  refreshActiveName();
  layoutSelectDirty_ = true;
  std::error_code ignored;
  std::filesystem::create_directories(host_.dataRoot / "layouts", ignored);
  if (const r1ui::dock::Status written = dk::writeFileAtomic(host_.dataRoot / "layouts" / "active-layout.txt", key); !written) model_.log(LogLevel::Warning, "active layout name not saved: " + written.error);
  registry_.touch();
}

// The display name is read from the stored layout once, when the active key changes: it is shown every
// frame and asked by every layout command's checked state, and listing means reading the files.
void EditorApp::refreshActiveName() {
  activeName_.clear();
  if (!layouts_ || activeKey_.empty()) return;
  for (const dk::LayoutSummary& item : layouts_->list()) {
    if (item.key == activeKey_) activeName_ = item.displayName;
  }
}

// ---- operations -----------------------------------------------------------------------------

void EditorApp::reportLayout(const dk::LayoutReport& report, std::string_view what) {
  if (!report.ok) {
    model_.log(LogLevel::Error, std::string(what) + " failed: " + report.error);
    setStatus(std::string(what) + " failed: " + report.error);
    return;
  }
  for (const std::string& warning : report.warnings) model_.log(LogLevel::Warning, "layout: " + warning);
  model_.info(std::string(what) + " done");
}

void EditorApp::loadLayoutKey(const std::string& key) {
  const dk::LayoutReport report = layouts_->load(key);
  reportLayout(report, "Load layout \"" + key + "\"");
  if (report.ok) setActiveKey(key);
  layoutSelectDirty_ = true;
}

// A built-in layout can be deleted by the user; asking for it by name brings it back from the built-in
// arrangement.
void EditorApp::loadNamed(const std::string& name, int builtin) {
  for (const dk::LayoutSummary& item : layouts_->list()) {
    if (item.displayName == name) {
      loadLayoutKey(item.key);
      return;
    }
  }
  if (const r1ui::dock::Status applied = dock().applyLayout(builtinLayout(builtin)); !applied) {
    setStatus("Cannot apply the " + name + " layout: " + applied.error);
    return;
  }
  std::string key;
  if (const r1ui::dock::Status saved = layouts_->saveAs(name, "Built-in arrangement", &key); !saved) setStatus("Cannot store the " + name + " layout: " + saved.error);
  else setActiveKey(key);
}

void EditorApp::saveLayout() {
  if (activeKey_.empty() || activeName_.empty()) {
    saveLayoutAs();
    return;
  }
  if (const r1ui::dock::Status saved = layouts_->save(activeKey_); !saved) {
    model_.log(LogLevel::Error, "save layout: " + saved.error);
    setStatus("Save layout failed: " + saved.error);
    return;
  }
  setStatus("Layout \"" + activeName_ + "\" saved");
}

void EditorApp::saveLayoutAs() {
  promptText("Save layout as", "Store the arrangement on screen under a name. A name that exists is not overwritten; use Save layout for that.", "My layout", "Save",
             [this](const std::string& name) {
               std::string key;
               if (const r1ui::dock::Status saved = layouts_->saveAs(name, "", &key); !saved) {
                 model_.log(LogLevel::Error, "save layout as: " + saved.error);
                 setStatus("Save layout failed: " + saved.error);
                 return;
               }
               setActiveKey(key);
               setStatus("Layout \"" + name + "\" saved");
             });
}

void EditorApp::renameLayout() {
  const std::string current = activeName_;
  if (current.empty()) return;
  promptText("Rename layout", "A new name for \"" + current + "\".", current, "Rename", [this](const std::string& name) {
    std::string key;
    if (const r1ui::dock::Status renamed = layouts_->rename(activeKey_, name, &key); !renamed) {
      model_.log(LogLevel::Error, "rename layout: " + renamed.error);
      setStatus("Rename failed: " + renamed.error);
      return;
    }
    setActiveKey(key);
    setStatus("Layout renamed to \"" + name + "\"");
  });
}

void EditorApp::deleteLayout() {
  const std::string current = activeName_;
  if (current.empty()) return;
  confirm("Delete layout", "Delete the stored layout \"" + current + "\"? The arrangement on screen stays.", "Delete", true, [this, current] {
    if (const r1ui::dock::Status removed = layouts_->remove(activeKey_); !removed) {
      model_.log(LogLevel::Error, "delete layout: " + removed.error);
      setStatus("Delete failed: " + removed.error);
      return;
    }
    setActiveKey({});
    setStatus("Layout \"" + current + "\" deleted");
  });
}

void EditorApp::resetLayout() {
  confirm("Reset layout", "Return every panel to the built-in default arrangement? Stored layouts are not touched.", "Reset", false, [this] {
    const dk::LayoutManager::ResetOutcome outcome = layouts_->resetToDefault([](std::string_view) { return true; });
    if (!outcome.done) {
      setStatus("Reset failed: " + outcome.error);
      return;
    }
    setActiveKey({});
    setStatus("Layout reset to the default arrangement");
  });
}

void EditorApp::showLayoutMenu() {
  if (rw::Select* select = ui_.objectAs<rw::Select>(layoutSelect_)) {
    rebuildLayoutSelect();
    select->open();
  }
}

// ---- the drop-down --------------------------------------------------------------------------

void EditorApp::rebuildLayoutSelect() {
  layoutSelectDirty_ = false;
  rw::Select* select = ui_.objectAs<rw::Select>(layoutSelect_);
  if (select == nullptr || !layouts_ || select->isOpen()) {
    if (select != nullptr && select->isOpen()) layoutSelectDirty_ = true;
    return;
  }
  std::vector<dk::LayoutSummary> items = layouts_->list();
  std::sort(items.begin(), items.end(), [](const dk::LayoutSummary& a, const dk::LayoutSummary& b) { return a.displayName < b.displayName; });
  applyingLayoutSelect_ = true;
  select->clearEntries();
  std::optional<size_t> selected;
  for (const dk::LayoutSummary& item : items) {
    if (item.key == activeKey_) selected = select->entries().size();
    select->addItem(item.displayName, item.key);
  }
  select->setPlaceholder(items.empty() ? "No saved layout" : "Custom arrangement");
  select->setSelectedIndex(selected);
  applyingLayoutSelect_ = false;
}

// ---- dialogs --------------------------------------------------------------------------------

// A dialog is opened from the next timer tick, not from the command that asked for it: a command run from a
// menu is followed by the menu closing, which gives the focus back to the widget that had it before the
// menu opened and would take it from the dialog (the drive found typed text going to the viewport).
void EditorApp::deferred(std::function<void()> action) {
  ui_.setTimer(0, [alive = alive_, action = std::move(action)] {
    if (*alive) action();
  });
}

void EditorApp::confirm(const std::string& title, const std::string& description, const std::string& action, bool danger, std::function<void()> accept) {
  deferred([this, title, description, action, danger, accept = std::move(accept)] { openConfirm(title, description, action, danger, accept); });
}

void EditorApp::promptText(const std::string& title, const std::string& description, const std::string& initial, const std::string& action,
                           std::function<void(const std::string&)> accept) {
  deferred([this, title, description, initial, action, accept = std::move(accept)] { openPrompt(title, description, initial, action, accept); });
}

void EditorApp::openConfirm(const std::string& title, const std::string& description, const std::string& action, bool danger, const std::function<void()>& accept) {
  rw::DialogSpec spec;
  spec.title = title;
  spec.description = description;
  spec.actions = {{"cancel", "Cancel", rw::DialogActionKind::Neutral, true, true, true}, {"ok", action, danger ? rw::DialogActionKind::Danger : rw::DialogActionKind::Primary, false, false, true}};
  spec.owner = root_;
  spec.maxWidth = 400.0;
  spec.onResult = [alive = alive_, accept](const rw::DialogResult& result) {
    if (*alive && result.action == "ok") accept();
  };
  dialog_ = rw::openDialog(ui_, std::move(spec));
}

void EditorApp::openPrompt(const std::string& title, const std::string& description, const std::string& initial, const std::string& action,
                           const std::function<void(const std::string&)>& accept) {
  auto text = std::make_shared<std::string>(initial);
  rw::DialogSpec spec;
  spec.title = title;
  spec.description = description;
  spec.actions = {{"cancel", "Cancel", rw::DialogActionKind::Neutral, false, true, true}, {"ok", action, rw::DialogActionKind::Primary, true, false, true}};
  spec.owner = root_;
  spec.width = 380.0;
  spec.onResult = [alive = alive_, text, accept](const rw::DialogResult& result) {
    if (*alive && result.action == "ok") accept(*text);
  };
  dialog_ = rw::openDialog(ui_, std::move(spec));
  if (!dialog_.valid()) return;
  rw::TextInput& input = ui_.create<rw::TextInput>(dialog_.body);
  input.setText(initial);
  input.setMaxLength(60);
  input.setOnTextChanged([text](std::string_view value) { *text = std::string(value); });
  // Enter commits the field; a commit caused by leaving the field (clicking Cancel) must not accept, and
  // by then the focus is no longer in the field.
  input.setOnCommitted([this, alive = alive_, field = input.id()](std::string_view) {
    if (*alive && ui_.router().focused() == field) rw::closeDialog(ui_, dialog_, "ok");
  });
  ui_.focusWidget(input.id(), r1ui::core::events::FocusReason::Keyboard);
  input.selectAll();
}

}  // namespace preview::editor
