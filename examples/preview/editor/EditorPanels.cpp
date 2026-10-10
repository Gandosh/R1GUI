// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the standard dockable panels of the Editor screen and their registration: viewport (with the pie
//   menu trigger), outliner (tree over the sample scene, selection shared with the viewport and the
//   inspector), inspector (the generated property panel), assets (thumbnail grid), curves (curve editor),
//   console (the command and layout log), actions (the searchable action list with descriptions) and the
//   hotkey editor (a keyboard view, hotkey sets, import/export through the file path dialog).
// Why: DockHost recreates a panel's content from its factory whenever the panel moves into another
//   window's context, so every panel is a small widget over state that lives in EditorApp (the model,
//   the registry, the controller) and never over state of its own.
// Invariants: factories run in whichever UiContext shows the panel; a factory of a floating context also
//   installs the shortcut forwarder there; panels remove their model listeners in onDetached.
// Callers: EditorApp's constructor (registerPanels).
#include <algorithm>
#include <cctype>

#include "EditorApp.h"
#include "EditorMenus.h"
#include "EditorViewport.h"
#include "r1ui/widgets/actions/ActionList.h"
#include "r1ui/widgets/curveeditor/CurveEditor.h"
#include "r1ui/widgets/hotkeys/HotkeyEditor.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/scroll/ScrollArea.h"
#include "r1ui/widgets/section/Section.h"
#include "r1ui/widgets/thumbnailgrid/ThumbnailGrid.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace preview::editor {

namespace rw = r1ui::widgets;
namespace rc = r1ui::commands;
namespace layout = r1ui::core::layout;
using r1ui::core::tree::WidgetId;

namespace {

void fill(layout::Style& s) {
  s.flexGrow = 1.0;
  s.flexShrink = 1.0;
  s.minWidth = layout::Length::px(0.0);
  s.minHeight = layout::Length::px(0.0);
}

// ---- outliner -------------------------------------------------------------------------------

constexpr rw::NodeId kMeshesFolder = 1000;
constexpr rw::NodeId kLightsFolder = 1001;

class OutlinerView final : public rw::WidgetObject {
 public:
  explicit OutlinerView(EditorModel& model) : model_(model) {}
  const char* typeName() const override { return "OutlinerView"; }

  void onAttached() override {
    style().direction = layout::FlexDirection::Column;
    fill(style());
    tree_ = std::make_shared<rw::SimpleTreeModel>();
    tree_->add(rw::kTreeRoot, kMeshesFolder, "Meshes", "layers");
    tree_->add(rw::kTreeRoot, kLightsFolder, "Lights", "layers");
    for (const SceneItem& item : model_.items()) {
      tree_->add(item.kind == ObjectKind::Mesh ? kMeshesFolder : kLightsFolder, item.id, model_.nameOf(item), item.kind == ObjectKind::Mesh ? "shapes" : "sun");
    }
    rw::TreeView& view = ui().create<rw::TreeView>(id());
    fill(view.style());
    view.setSelectionMode(rw::TreeSelectionMode::Multi);
    view.setModel(tree_);
    view.setExpanded(kMeshesFolder, true);
    view.setExpanded(kLightsFolder, true);
    view.setSelection(model_.selection());
    view.setOnSelectionChanged([this](rw::TreeView& t) {
      if (syncing_) return;
      std::vector<uint64_t> ids;
      for (const rw::NodeId node : t.selection()) {
        if (node != kMeshesFolder && node != kLightsFolder) ids.push_back(node);
      }
      syncing_ = true;
      model_.select(std::move(ids));
      syncing_ = false;
    });
    view_ = view.id();
    selectionListener_ = model_.onSelectionChanged([this] {
      if (syncing_) return;
      syncing_ = true;
      if (rw::TreeView* t = ui().objectAs<rw::TreeView>(view_)) t->setSelection(model_.selection());
      syncing_ = false;
    });
    notifierToken_ = model_.context().notifier().subscribe([this](const r1ui::props::ChangeEvent&) { refreshLabels(); });
  }

  void onDetached() override {
    model_.removeSelectionListener(selectionListener_);
    model_.context().notifier().unsubscribe(notifierToken_);
  }

 private:
  void refreshLabels() {
    bool changed = false;
    for (const SceneItem& item : model_.items()) {
      if (tree_->label(item.id) != model_.nameOf(item)) {
        tree_->setLabel(item.id, model_.nameOf(item));
        changed = true;
      }
    }
    if (changed) {
      if (rw::TreeView* t = ui().objectAs<rw::TreeView>(view_)) t->refresh();
    }
  }

  EditorModel& model_;
  std::shared_ptr<rw::SimpleTreeModel> tree_;
  WidgetId view_;
  EditorModel::ListenerId selectionListener_ = 0;
  r1ui::props::ChangeNotifier::Token notifierToken_ = 0;
  bool syncing_ = false;
};

// ---- console --------------------------------------------------------------------------------

class ConsoleView final : public rw::WidgetObject {
 public:
  explicit ConsoleView(EditorModel& model) : model_(model) {}
  const char* typeName() const override { return "ConsoleView"; }

  void onAttached() override {
    style().direction = layout::FlexDirection::Column;
    fill(style());
    rw::ScrollArea& scroll = ui().create<rw::ScrollArea>(id());
    fill(scroll.style());
    scroll_ = scroll.id();
    if (layout::Style* contentStyle = &ui().object(scroll.content())->style()) {
      contentStyle->direction = layout::FlexDirection::Column;
      for (double& p : contentStyle->padding) p = 6.0;
    }
    append();
    listener_ = model_.onLog([this] { append(); });
  }

  void onDetached() override { model_.removeLogListener(listener_); }

 private:
  void append() {
    rw::ScrollArea* scroll = ui().objectAs<rw::ScrollArea>(scroll_);
    if (scroll == nullptr) return;
    for (const LogLine& line : model_.lines()) {
      if (line.serial <= shown_) continue;
      rw::Label& label = ui().create<rw::Label>(scroll->content(), line.text, line.level == LogLevel::Error ? rw::LabelRole::Danger : rw::LabelRole::Body);
      if (line.level == LogLevel::Warning) label.setColorToken("warning-action");
      label.style().flexShrink = 0.0;
      labels_.push_back(label.id());
      shown_ = line.serial;
    }
    while (labels_.size() > EditorModel::kMaxLogLines) {
      ui().destroy(labels_.front());
      labels_.erase(labels_.begin());
    }
    if (!scrollPending_) {
      scrollPending_ = true;
      const WidgetId self = id();
      ui().setTimer(0, [ui = &ui(), self] {
        if (!ui->alive(self)) return;
        if (auto* console = ui->objectAs<ConsoleView>(self)) console->scrollToEnd();
      });
    }
  }

  void scrollToEnd() {
    scrollPending_ = false;
    if (rw::ScrollArea* scroll = ui().objectAs<rw::ScrollArea>(scroll_)) scroll->scrollBy(0.0, 1.0e9);
  }

  EditorModel& model_;
  WidgetId scroll_;
  std::vector<WidgetId> labels_;
  uint64_t shown_ = 0;
  bool scrollPending_ = false;
  EditorModel::ListenerId listener_ = 0;
};

// ---- assets ---------------------------------------------------------------------------------

const rw::VectorAssetModel& sampleAssets() {
  static const rw::VectorAssetModel model = [] {
    rw::VectorAssetModel m;
    const char* kinds[] = {"Texture", "Mesh", "Material", "Prefab", "Audio"};
    const char* icons[] = {"image", "shapes", "palette", "layers", "play"};
    for (uint64_t i = 0; i < 160; ++i) {
      rw::GridItem item;
      item.key = i + 1;
      item.folder = i < 4;
      item.name = item.folder ? "Folder " + std::to_string(i + 1) : std::string(kinds[i % 5]) + " " + std::to_string(i + 1);
      item.typeLabel = item.folder ? "Folder" : kinds[i % 5];
      item.icon = item.folder ? "folder-open" : icons[i % 5];
      item.modified = i % 17 == 4;
      m.items.push_back(std::move(item));
    }
    return m;
  }();
  return model;
}

// ---- curves ---------------------------------------------------------------------------------

void buildCurves(rw::UiContext& ui, WidgetId parent) {
  rw::CurveEditor& editor = ui.create<rw::CurveEditor>(parent);
  fill(editor.style());
  r1ui::widgets::curve::Curve c;
  c.id = 1;
  c.name = "Value";
  c.colour = {0.9, 0.3, 0.2};
  for (uint32_t i = 0; i < 5; ++i) {
    r1ui::widgets::curve::Key k;
    k.id = i + 1;
    k.time = static_cast<double>(i);
    k.value = (i % 2 == 0) ? 0.0 : 1.0;
    k.interp = r1ui::widgets::curve::Interp::Cubic;
    c.keys.push_back(k);
  }
  editor.graph().setCurves({c});
}

}  // namespace

// ---- registration ---------------------------------------------------------------------------

void EditorApp::registerPanels() {
  using namespace panel;
  using Make = std::function<WidgetId(rw::UiContext&, WidgetId)>;
  const auto add = [this](r1ui::dock::PanelId id, const char* title, const char* icon, r1ui::dock::Point floatSize, Make make) {
    rw::PanelDescriptor d;
    d.id = id;
    d.title = title;
    d.icon = icon;
    d.floatSize = floatSize;
    d.minSize = {160.0, 100.0};
    d.factory = [this, make = std::move(make)](rw::UiContext& ui, WidgetId parent) {
      if (&ui != &ui_) installKeyForwarder(ui);  // a native window's context has no shortcut handler of its own
      return make(ui, parent);
    };
    if (id == kShortcuts) {
      d.suggested = {kViewport, true, r1ui::dock::Side::Right};
      d.minSize = {560.0, 380.0};
    }
    panels_.add(std::move(d));
  };

  add(kViewport, "Viewport", "shapes", {720.0, 480.0}, [this](rw::UiContext& ui, WidgetId parent) { return makeViewport(ui, parent); });
  add(kOutliner, "Outliner", "layers", {300.0, 380.0}, [this](rw::UiContext& ui, WidgetId parent) { return ui.create<OutlinerView>(parent, model_).id(); });
  add(kInspector, "Inspector", "sliders-horizontal", {320.0, 520.0}, [this](rw::UiContext& ui, WidgetId parent) {
    rw::PropertyPanel& p = ui.create<rw::PropertyPanel>(parent, model_.context(), model_.panelState());
    return p.id();
  });
  add(kAssets, "Assets", "image", {520.0, 380.0}, [](rw::UiContext& ui, WidgetId parent) {
    rw::ThumbnailGrid& grid = ui.create<rw::ThumbnailGrid>(parent);
    fill(grid.style());
    grid.setModel(&sampleAssets());
    return grid.id();
  });
  add(kCurves, "Curves", "spline", {560.0, 360.0}, [](rw::UiContext& ui, WidgetId parent) {
    rw::SectionBox& box = ui.create<rw::SectionBox>(parent);
    box.style().direction = layout::FlexDirection::Column;
    buildCurves(ui, box.id());
    return box.id();
  });
  add(kConsole, "Console", "code", {560.0, 300.0}, [this](rw::UiContext& ui, WidgetId parent) { return ui.create<ConsoleView>(parent, model_).id(); });
  add(kCommands, "Actions", "search", {520.0, 460.0}, [this](rw::UiContext& ui, WidgetId parent) {
    rw::ActionList& list = ui.create<rw::ActionList>(parent);
    fill(list.style());
    list.bindRegistry(&registry_, &keymap_);
    list.view().setOnActivate([this](const rw::ActionInfo& action) { router_.execute(action.id, rc::ExecuteSource::Menu); });
    return list.id();
  });
  add(kShortcuts, "Hotkey Editor", "settings2", {1100.0, 640.0}, [this](rw::UiContext& ui, WidgetId parent) {
    rw::HotkeyEditor& editor = ui.create<rw::HotkeyEditor>(parent, services());
    fill(editor.style());
    restoreHotkeySets(editor);
    editor.setOnSetsChanged([this](const std::string& name, const std::string& json) { persistHotkeySet(name, json); });
    editor.setOnExport([this, context = &ui, self = editor.id()] { askKeybindingFile(*context, self, true); });
    editor.setOnImport([this, context = &ui, self = editor.id()] { askKeybindingFile(*context, self, false); });
    return editor.id();
  });
}

// ---- keybinding files and hotkey sets -------------------------------------------------------------

void EditorApp::askKeybindingFile(rw::UiContext& ui, WidgetId owner, bool save) {
  rw::FilePathOptions options;
  options.mode = save ? rw::FilePathMode::Save : rw::FilePathMode::Open;
  options.title = save ? "Export hotkeys" : "Import hotkeys";
  options.description = save ? "Write your key bindings (only what differs from the defaults) to a file." : "Replace your key bindings with the ones in a file; a file with a problem changes nothing.";
  options.folder = host_.dataRoot / "keybindings";
  options.extension = ".json";
  options.initialName = "my-hotkeys";
  chooseFile(ui, owner, std::move(options), [this, save](const std::filesystem::path& path) {
    if (save) exportKeybindingsTo(path);
    else importKeybindingsFrom(path);
  });
}

void EditorApp::exportKeybindingsTo(const std::filesystem::path& file) {
  const r1ui::dock::Status written = r1ui::dock::writeFileAtomic(file, rc::exportOverrides(registry_, overrides_));
  if (written) {
    model_.info("Shortcuts exported to " + file.string());
    setStatus("Shortcuts exported to " + file.string());
  } else {
    model_.log(LogLevel::Error, "export shortcuts: " + written.error);
    setStatus("Export failed: " + written.error);
  }
}

void EditorApp::importKeybindingsFrom(const std::filesystem::path& file) {
  const std::optional<std::string> text = r1ui::dock::readBoundedFile(file);
  if (!text) {
    model_.log(LogLevel::Warning, "import shortcuts: cannot read " + file.string());
    setStatus("Nothing to import: " + file.string() + " cannot be read");
    return;
  }
  const rc::ImportReport report = rc::importOverrides(*text, registry_, overrides_, rc::ImportMode::Replace);
  if (!report.ok) {
    model_.log(LogLevel::Error, "import shortcuts: " + report.error);
    setStatus("Import failed: " + report.error);
    return;
  }
  for (const rc::ImportIssue& issue : report.issues) model_.log(LogLevel::Warning, "import shortcuts: entry " + std::to_string(issue.index) + ": " + issue.message);
  model_.info("Shortcuts imported from " + file.string() + " (" + std::to_string(report.applied) + " applied)");
  setStatus("Shortcuts imported (" + std::to_string(report.applied) + " applied)");
}

// A hotkey set is a named copy of the key bindings; the editor keeps them in memory and tells us when one is
// saved. They live in <data>/hotkey-sets/<name>.json and come back each time the editor is built.
void EditorApp::persistHotkeySet(const std::string& name, const std::string& json) {
  std::string stem;
  for (const char c : name) stem += (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == ' ' || c == '_' || c == '-' || c == '.' || c == '(' || c == ')') ? c : '_';
  if (stem.empty() || stem == "." || stem == "..") stem = "set";
  std::error_code ignored;
  std::filesystem::create_directories(host_.dataRoot / "hotkey-sets", ignored);
  const r1ui::dock::Status written = r1ui::dock::writeFileAtomic(host_.dataRoot / "hotkey-sets" / (stem + ".json"), json);
  if (written) model_.info("Hotkey set \"" + name + "\" saved");
  else model_.log(LogLevel::Error, "hotkey set not saved: " + written.error);
}

void EditorApp::restoreHotkeySets(rw::HotkeyEditor& editor) {
  std::error_code ec;
  const std::filesystem::path folder = host_.dataRoot / "hotkey-sets";
  if (!std::filesystem::is_directory(folder, ec)) return;
  size_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
    if (++count > rw::kMaxHotkeySets) break;
    if (!entry.is_regular_file(ec) || entry.path().extension() != ".json") continue;
    const std::optional<std::string> text = r1ui::dock::readBoundedFile(entry.path());
    if (!text) continue;
    const std::u8string stem = entry.path().stem().u8string();
    editor.addSet(std::string(reinterpret_cast<const char*>(stem.data()), stem.size()), *text);
  }
}

}  // namespace preview::editor
