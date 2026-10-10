// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of EditorApp.h: construction order, the widget skeleton (menu bar, layout
//   drop-down, toolbar, dock, status line), the per-frame update, saving the user files, the shortcut
//   forwarder of native windows and the destruction order.
// Invariants: members are initialised in the order the toolkit requires (registry, router, sync,
//   customization, controller, key handler); the dock gets a layout before the first frame and the stored
//   layout replaces it right after the first layout pass (the dock needs a rectangle); the destructor
//   writes everything before it destroys a widget.
// Callers: PreviewApp, tests/preview.
#include "EditorApp.h"

#include <algorithm>
#include <cstdlib>

#include "../PreviewParts.h"
#include "EditorMenus.h"
#include "r1ui/widgets/dialog/Dialog.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/section/Section.h"

namespace preview::editor {

namespace rw = r1ui::widgets;
namespace rc = r1ui::commands;
namespace layout = r1ui::core::layout;
using r1ui::core::tree::WidgetId;

namespace {

std::vector<std::string> screenNamesOf(const EditorHost& host) { return host.screenNames ? host.screenNames() : std::vector<std::string>{}; }

// The GlobalKeyHandler of a native window's context: the same router as the main window, with the focus
// path decided in this window. It never creates timers (a pending sequence expires on the next key or
// the main window's tick), so it cannot outlive its context in a harmful way. Escape first ends a pie
// gesture that has not been drawn yet (the pie's own overlay handles the drawn one).
class FloatKeys final : public r1ui::core::events::GlobalKeyHandler {
 public:
  FloatKeys(rw::UiContext& ui, rw::CommandServices services, std::function<void()> cancelPies) : ui_(ui), services_(services), cancelPies_(std::move(cancelPies)) {}
  bool onGlobalKey(const r1ui::core::events::Event& event, r1ui::core::events::Router&) override {
    if (event.key == r1ui::core::events::Key::Escape && cancelPies_) cancelPies_();
    const rw::WidgetObject* focused = ui_.object(ui_.router().focused());
    const std::vector<std::string> chain{focused != nullptr && focused->wantsTextInput() ? rc::kTextContext : rc::kWindowContext};
    return services_.router.handleKey({event.key, event.modifiers, event.repeat, false}, chain).consumed;
  }

 private:
  rw::UiContext& ui_;
  rw::CommandServices services_;
  std::function<void()> cancelPies_;
};

// The main window's handler: Escape ends a pie gesture, then the command keys decide.
class MainKeys final : public r1ui::core::events::GlobalKeyHandler {
 public:
  MainKeys(rw::CommandKeyHandler& inner, std::function<void()> cancelPies) : inner_(inner), cancelPies_(std::move(cancelPies)) {}
  bool onGlobalKey(const r1ui::core::events::Event& event, r1ui::core::events::Router& router) override {
    if (event.key == r1ui::core::events::Key::Escape && cancelPies_) cancelPies_();
    return inner_.onGlobalKey(event, router);
  }

 private:
  rw::CommandKeyHandler& inner_;
  std::function<void()> cancelPies_;
};

}  // namespace

std::filesystem::path defaultDataRoot() {
  const auto env = [](const char* name) -> std::string {
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return {};
    std::string text = value;
    std::free(value);
    return text;
  };
  if (const std::string override_ = env("R1GUI_PREVIEW_DATA"); !override_.empty()) return override_;
  if (const std::string local = env("LOCALAPPDATA"); !local.empty()) return std::filesystem::path(local) / "R1GUI" / "preview";
  std::error_code ignored;
  return std::filesystem::temp_directory_path(ignored) / "R1GUI" / "preview";
}

// ---- construction ---------------------------------------------------------------------------

EditorApp::EditorApp(rw::UiContext& ui, WidgetId parent, rw::IFloatingBackend& backend, EditorHost host)
    : ui_(ui),
      backend_(backend),
      host_(std::move(host)),
      clock_(ui),
      router_(registry_, keymap_, clock_),
      model_(ui),
      sync_(ui, services()),
      customization_(editorLayoutSet(screenNamesOf(host_), &menuSet_)),
      customizationStore_(host_.dataRoot / "customization.json"),
      customizationStorage_(customization_, customizationStore_),
      controller_(ui, services(), sync_, customization_),
      keyStore_(host_.dataRoot / "keybindings.json"),
      keys_(ui, services()),
      menuStore_(host_.dataRoot / "custom-menus.json"),
      menuStorage_(menuSet_, menuStore_),
      creator_(menuSet_),
      layoutStore_(host_.dataRoot / "layouts") {
  std::error_code ignored;
  std::filesystem::create_directories(host_.dataRoot / "layouts", ignored);

  // The menu bar and toolbar are still built by the customizable widgets, but the Editor no longer offers
  // the Customize mode (owner decision 2026-10-10: menus are made in the Create Custom Menu window): its
  // four commands would only clutter the action lists and the hotkey editor.
  for (const char* id : {rw::kCmdCustomizeToggle, rw::kCmdCustomizeRevert, rw::kCmdCustomizeResetAll, rw::kCmdCustomizeNewMenu}) registry_.remove(id);

  registerPanels();
  registerCommands();
  startCustomMenus();  // before the key bindings and customization load: they refer to its commands

  const rc::ImportReport keys = rc::loadOverrides(keyStore_, registry_, overrides_);
  if (!keys.ok && !keys.error.empty()) model_.log(LogLevel::Warning, "keybindings.json was not used: " + keys.error);
  for (const rc::ImportIssue& issue : keys.issues) model_.log(LogLevel::Warning, "keybindings.json entry " + std::to_string(issue.index) + ": " + issue.message);
  savedOverridesVersion_ = overrides_.version();

  const rc::customize::LoadReport custom = customizationStorage_.load();
  if (!custom.ok) model_.log(LogLevel::Warning, "customization.json was not used: " + custom.error);
  if (!custom.userKeptAside.empty()) model_.log(LogLevel::Warning, "customization.json was damaged and kept as " + custom.userKeptAside);

  router_.setPendingObserver([this](const rc::PendingState& state) { pendingText_ = state.active ? state.text : std::string(); });
  undoListener_ = model_.context().undo().addListener([this](const r1ui::props::UndoEvent&) { registry_.touch(); });
  mainKeys_ = std::make_unique<MainKeys>(keys_, [this] { cancelPies(); });
  ui_.setGlobalKeyHandler(mainKeys_.get());

  buildUi(parent);
  ui_.frame();  // the dock needs a rectangle before a stored layout is applied
  startLayouts();
  if (firstMenuRun_) {  // the owner can try the sample dockable menu at once, and it stays in the Default layout
    for (const auto& menu : menuSet_.menus()) {
      if (menu.kind == r1ui::commands::custommenu::MenuKind::Panel) openMenuPanel(menu.id);
    }
    if (layouts_ && !activeKey_.empty()) {
      if (const r1ui::dock::Status saved = layouts_->save(activeKey_); !saved) model_.log(LogLevel::Warning, "the Default layout was not updated: " + saved.error);
    }
  }
  model_.info("Editor ready");
}

EditorApp::~EditorApp() {
  flush();
  *alive_ = false;
  menuSet_.unsubscribe(menuListener_);
  model_.context().undo().removeListener(undoListener_);
  ui_.setGlobalKeyHandler(nullptr);
  if (dialog_.valid() && rw::isDialogOpen(ui_, dialog_)) rw::closeDialog(ui_, dialog_);
  // The dock goes first: it closes the native windows and detaches every panel (their listeners point
  // into the model and the controller, which die with this object).
  if (ui_.alive(root_)) ui_.destroy(root_);
}

void EditorApp::buildUi(WidgetId parent) {
  rw::SectionBox& root = ui_.create<rw::SectionBox>(parent);
  root.style().direction = layout::FlexDirection::Column;
  root.style().flexGrow = 1.0;
  root.style().flexShrink = 1.0;
  root.style().minWidth = layout::Length::px(0.0);
  root.style().minHeight = layout::Length::px(0.0);
  root_ = root.id();

  const auto bar = [&](uint8_t edge, double height) -> preview::Surface& {
    preview::Surface& surface = ui_.create<preview::Surface>(root_, "panel", edge);
    surface.style().direction = layout::FlexDirection::Row;
    surface.style().alignItems = layout::Align::Center;
    surface.style().flexShrink = 0.0;
    if (height > 0.0) surface.style().height = layout::Length::px(height);
    return surface;
  };

  preview::Surface& top = bar(preview::Surface::kBottom, 0.0);
  menuBar_ = ui_.create<rw::CustomizableMenuBar>(top.id(), controller_).id();
  rw::SectionBox& spacer = ui_.create<rw::SectionBox>(top.id());
  spacer.style().flexGrow = 1.0;
  rw::Label& layoutText = ui_.create<rw::Label>(top.id(), "Layout", rw::LabelRole::Muted);
  layoutText.style().margin[layout::kRight] = layout::Length::px(8.0);
  rw::Select& select = ui_.create<rw::Select>(top.id());
  select.setCompact(true);
  select.style().width = layout::Length::px(170.0);
  select.style().margin[layout::kRight] = layout::Length::px(8.0);
  select.setOnChanged([this](size_t, std::string_view value) {
    if (applyingLayoutSelect_ || !layouts_ || value.empty()) return;
    loadLayoutKey(std::string(value));
  });
  layoutSelect_ = select.id();

  preview::Surface& tools = bar(preview::Surface::kBottom, 0.0);
  toolbar_ = ui_.create<rw::CustomizableToolbar>(tools.id(), controller_, kToolbarMain).id();

  rw::DockHostOptions options;
  rw::DockHost& dockHost = ui_.create<rw::DockHost>(root_, panels_, backend_, options);
  dockId_ = dockHost.id();
  dockHost.setLayout(builtinLayout(0));

  preview::Surface& status = bar(preview::Surface::kTop, 26.0);
  status.style().padding[layout::kLeft] = 12.0;
  status.style().padding[layout::kRight] = 12.0;
  rw::Label& left = ui_.create<rw::Label>(status.id(), status_, rw::LabelRole::Muted);
  left.style().flexGrow = 1.0;
  left.style().minWidth = layout::Length::px(0.0);
  statusLabel_ = left.id();
  rw::Label& right = ui_.create<rw::Label>(status.id(), "", rw::LabelRole::Muted);
  right.style().flexShrink = 0.0;
  layoutLabel_ = right.id();
}

// ---- per frame ------------------------------------------------------------------------------

void EditorApp::update() {
  sync_.refresh();
  if (layoutSelectDirty_) rebuildLayoutSelect();
  refreshStatus();
}

void EditorApp::tick() {
  if (layouts_) layouts_->tick();
  saveUserFiles();
}

std::optional<uint64_t> EditorApp::msUntilTick() const { return layouts_ ? layouts_->msUntilSave() : std::nullopt; }

bool EditorApp::onKeyUp(r1ui::core::events::Key key, uint8_t modifiers) { return keys_.onKeyUp(key, modifiers); }

// The label follows at once: a command run from a native window may be the only thing that changes in the
// main window, and nothing else would ask it for a frame.
void EditorApp::setStatus(std::string text) {
  status_ = std::move(text);
  refreshStatus();
}

void EditorApp::refreshStatus() {
  const std::string left = pendingText_.empty() ? status_ : pendingText_ + "  (waiting for the next key, Escape cancels)";
  if (rw::Label* label = ui_.objectAs<rw::Label>(statusLabel_); label != nullptr && label->text() != left) label->setText(left);
  const std::string name = activeName_;
  const r1ui::props::UndoStack& undo = model_.context().undo();
  const std::string right = "Layout: " + (name.empty() ? std::string("custom") : name) + "     Undo: " + (undo.canUndo() ? undo.undoLabel() : std::string("nothing"));
  if (rw::Label* label = ui_.objectAs<rw::Label>(layoutLabel_); label != nullptr && label->text() != right) label->setText(right);
}

void EditorApp::saveUserFiles() {
  if (overrides_.version() == savedOverridesVersion_) return;
  savedOverridesVersion_ = overrides_.version();  // a failed write is reported once, not retried every frame
  std::string error;
  if (rc::saveOverrides(registry_, overrides_, keyStore_, error)) model_.info("Keyboard shortcuts saved");
  else model_.log(LogLevel::Error, "keybindings.json not saved: " + error);
}

void EditorApp::saveAll() {
  if (layouts_) {
    if (const r1ui::dock::Status written = layouts_->flush(true); !written) model_.log(LogLevel::Error, "layout not saved: " + written.error);
  }
  savedOverridesVersion_ = 0;
  saveUserFiles();
  if (!customizationStorage_.save()) model_.log(LogLevel::Error, "customization.json not saved: " + customizationStorage_.lastError());
  if (!menuStorage_.save()) model_.log(LogLevel::Error, "custom-menus.json not saved: " + menuStorage_.lastError());
  setStatus("Saved the layout, shortcuts, customization and custom menus");
}

// Application exit and tests: the layout and every user file now.
void EditorApp::flush() { saveAll(); }

bool EditorApp::run(std::string_view commandId) {
  const rc::ExecuteResult result = router_.execute(commandId, rc::ExecuteSource::Api);
  return result.isHandled();
}

void EditorApp::installKeyForwarder(rw::UiContext& ui) {
  for (auto& [owner, handler] : floatKeys_) {
    if (owner == &ui) {
      ui.setGlobalKeyHandler(handler.get());
      return;
    }
  }
  floatKeys_.emplace_back(&ui, std::make_unique<FloatKeys>(ui, services(), [this] { cancelPies(); }));
  ui.setGlobalKeyHandler(floatKeys_.back().second.get());
}

}  // namespace preview::editor
