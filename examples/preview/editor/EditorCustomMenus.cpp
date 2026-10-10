// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Editor's custom menus (slice 5.19): loading and saving the user's menus, the sample menus of the
//   first run, one dock panel per dockable menu, the Custom Menus main menu and the commands behind it, the
//   Create Custom Menu window (a floating panel), the pie that opens on a right mouse hold in the viewport,
//   the .r1mn save and load dialogs, and the small file chooser every dialog of the Editor uses.
// Why: owner requirement 2026-10-10: create a pie or dockable menu in a window, find it under a top-level
//   Custom Menus menu, reopen a closed dockable menu, save and load menus as files. The toolkit provides the
//   model, widgets and commands; this file is the host's part: panel ids, storage paths, where dialogs open.
// Invariants: a dockable menu with serial n is dock panel kMenuBase + n for the life of the app (the panel
//   registry cannot remove panels, so a deleted menu's panel is closed and stays registered, unused); the
//   panel registry is brought in line with the set synchronously (a layout load needs it), the menu bar
//   from a timer (a command running from an open menu must not rebuild that menu); dialogs open from a
//   timer of the context they belong to; every callback re-checks alive_.
// Callers: EditorApp's constructor and the commands in EditorCommands.cpp.
#include <algorithm>
#include <cctype>
#include <set>

#include "EditorApp.h"
#include "EditorMenus.h"
#include "EditorViewport.h"
#include "r1ui/widgets/custommenu/CustomMenuPanel.h"
#include "r1ui/widgets/custommenu/CustomMenusMenu.h"
#include "r1ui/widgets/dialog/Dialog.h"

namespace preview::editor {

namespace rw = r1ui::widgets;
namespace rc = r1ui::commands;
namespace cm = r1ui::commands::custommenu;
namespace dk = r1ui::dock;
namespace fs = std::filesystem;
using r1ui::core::tree::WidgetId;

namespace {

constexpr const char* kViewportPieFile = "viewport-pie.txt";
constexpr uint32_t kMaxMenuSerialForPanel = 100000;

std::string safeFileStem(const std::string& name) {
  std::string out;
  for (const char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    out += (std::isalnum(u) != 0 || c == ' ' || c == '_' || c == '-' || c == '.' || c == '(' || c == ')') ? c : '_';
    if (out.size() >= 60) break;
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
  while (!out.empty() && (out.front() == ' ' || out.front() == '.')) out.erase(out.begin());
  return out.empty() ? std::string("menu") : out;
}

// The viewport's trigger: a PieTrigger that tells the app while it lives, so Escape can end a gesture in
// whichever window the viewport is in.
class ViewportPie final : public rw::PieTrigger {
 public:
  ViewportPie(rw::CommandServices services, Provider provider, std::vector<rw::PieTrigger*>& live) : PieTrigger(services, std::move(provider)), live_(live) {}
  void onAttached() override {
    PieTrigger::onAttached();
    live_.push_back(this);
  }
  void onDetached() override {
    live_.erase(std::remove(live_.begin(), live_.end(), static_cast<rw::PieTrigger*>(this)), live_.end());
    PieTrigger::onDetached();
  }

 private:
  std::vector<rw::PieTrigger*>& live_;
};

}  // namespace

// ---- startup ----------------------------------------------------------------------------------------

void EditorApp::startCustomMenus() {
  std::error_code ec;
  fs::create_directories(host_.dataRoot, ec);
  firstMenuRun_ = !fs::exists(host_.dataRoot / "custom-menus.json", ec);
  menuCommands_ = std::make_unique<rw::CustomMenuCommands>(registry_, menuSet_, menuHooks());
  const cm::MenuLoadReport report = menuStorage_.load();
  if (!report.ok) model_.log(LogLevel::Warning, "custom-menus.json was not used: " + report.error);
  if (!report.keptAside.empty()) model_.log(LogLevel::Warning, "custom-menus.json was damaged and kept as " + report.keptAside);
  for (const cm::MenuIssue& issue : report.issues) model_.log(LogLevel::Warning, "custom-menus.json " + issue.where + ": " + issue.message);
  if (firstMenuRun_) createSampleMenus();

  if (const std::optional<std::string> text = dk::readBoundedFile(host_.dataRoot / kViewportPieFile)) {
    viewportPieId_ = *text;
    while (!viewportPieId_.empty() && (viewportPieId_.back() == '\n' || viewportPieId_.back() == '\r' || viewportPieId_.back() == ' ')) viewportPieId_.pop_back();
  }
  if (!viewportPie()) viewportPieId_.clear();
  registerCreatorPanel();
  registerMenuPanels();  // the panels of the existing dockable menus exist before the layouts start
  menuListener_ = menuSet_.subscribe([this] { onMenusChanged(); });
  refreshMenuBar();
}

// First start only: a pie the owner can try at once (right mouse hold in the viewport) and a dockable panel.
void EditorApp::createSampleMenus() {
  const cm::MenuEditResult pie = menuSet_.createMenu(cm::MenuKind::Pie, "Tools Pie");
  if (pie.ok) {
    const char* slots[] = {cmd::kToolMove, cmd::kToolRotate, cmd::kToolScale, cmd::kFrame, cmd::kToolSelect, cmd::kWireframe, cmd::kUndo, cmd::kRedo};
    for (size_t i = 0; i < 8; ++i) menuSet_.setSlot(pie.id, i, slots[i]);
    rememberViewportPie(pie.id);
  }
  const cm::MenuEditResult panelMenu = menuSet_.createMenu(cm::MenuKind::Panel, "Quick Tools");
  if (panelMenu.ok) {
    for (const char* c : {cmd::kToolSelect, cmd::kToolMove, cmd::kToolRotate, cmd::kToolScale, cmd::kUndo, cmd::kRedo, cmd::kFrame, cmd::kWireframe}) menuSet_.addEntry(panelMenu.id, c);
    menuSet_.setPanelColumns(panelMenu.id, 2);
  }
  model_.info("First start: the Tools Pie (hold the right mouse button in the viewport) and the Quick Tools panel were created");
}

// ---- panels of the dockable menus -------------------------------------------------------------------

dk::PanelId EditorApp::panelForMenu(const std::string& menuId) const {
  const cm::CustomMenu* menu = menuSet_.find(menuId);
  if (menu == nullptr || menu->kind != cm::MenuKind::Panel || menu->serial == 0 || menu->serial > kMaxMenuSerialForPanel) return 0;
  return panel::kMenuBase + menu->serial;
}

dk::PanelId EditorApp::registerMenuPanel(const cm::CustomMenu& menu) {
  const dk::PanelId id = panelForMenu(menu.id);
  if (id == 0) return 0;
  if (panels_.find(id) != nullptr) {
    panels_.setTitle(id, menu.name);
    return id;
  }
  rw::PanelFactory inner = rw::makeCustomMenuPanelFactory(services(), sync_, menuSet_, menu.id);
  rw::PanelFactory factory = [this, inner = std::move(inner)](rw::UiContext& ui, WidgetId parent) {
    if (&ui != &ui_) installKeyForwarder(ui);  // a native window's context has no shortcut handler of its own
    return inner(ui, parent);
  };
  rw::PanelDescriptor descriptor = rw::describeCustomMenuPanel(menu, id, std::move(factory));
  descriptor.suggested = {panel::kViewport, false, dk::Side::Right};
  descriptor.minSize = {96.0, 60.0};
  if (!panels_.add(std::move(descriptor))) {
    model_.log(LogLevel::Error, "the panel of the menu \"" + menu.name + "\" could not be registered");
    return 0;
  }
  return id;
}

// Registers a descriptor for every dockable menu and keeps the titles; no widget is touched, so it is safe
// inside the set's change notification. The signature (serials and titles) tells syncMenuPanels whether the
// dock still has to be told.
void EditorApp::registerMenuPanels() {
  std::string signature;
  for (const cm::CustomMenu& menu : menuSet_.menus()) {
    if (menu.kind != cm::MenuKind::Panel) continue;
    if (registerMenuPanel(menu) != 0) signature += std::to_string(menu.serial) + "=" + menu.name + '\n';
  }
  menuPanelSignature_ = signature;
}

// Brings the dock in line with the set: the model learns new panels and titles, the panel of a deleted
// menu is closed. Destroys widgets, so never call it from inside the set's change notification (the
// notification runs on a copy of the listener list and would still call the listener of a destroyed panel).
void EditorApp::syncMenuPanels() {
  registerMenuPanels();
  rw::DockHost* d = dockPtr();
  if (d == nullptr) return;
  std::set<uint32_t> live;
  for (const cm::CustomMenu& menu : menuSet_.menus()) {
    if (menu.kind == cm::MenuKind::Panel) live.insert(menu.serial);
  }
  for (const rw::PanelDescriptor& descriptor : panels_.all()) {
    if (descriptor.id <= panel::kMenuBase || descriptor.id > panel::kMenuBase + kMaxMenuSerialForPanel) continue;
    if (live.count(descriptor.id - panel::kMenuBase) == 0 && d->layout().isDocked(descriptor.id)) d->closePanel(descriptor.id);
  }
  if (menuPanelSignature_ == dockPanelSignature_) return;
  const r1ui::dock::Status refreshed = d->refreshPanels();
  if (refreshed) dockPanelSignature_ = menuPanelSignature_;
  else model_.log(LogLevel::Warning, "dock panels: " + refreshed.error);
}

bool EditorApp::openMenuPanel(const std::string& menuId) {
  rw::DockHost* d = dockPtr();
  if (d == nullptr) return false;
  syncMenuPanels();
  const dk::PanelId id = panelForMenu(menuId);
  if (id == 0) {
    setStatus("That menu is not a dockable menu");
    return false;
  }
  if (!d->openPanel(id)) {
    setStatus("The panel could not be opened: " + d->lastError());
    return false;
  }
  d->activatePanel(id);
  return true;
}

// ---- the viewport pie -------------------------------------------------------------------------------

void EditorApp::rememberViewportPie(const std::string& menuId) {
  const cm::CustomMenu* menu = menuSet_.find(menuId);
  if (menu == nullptr || menu->kind != cm::MenuKind::Pie) return;
  viewportPieId_ = menuId;
  const r1ui::dock::Status written = dk::writeFileAtomic(host_.dataRoot / kViewportPieFile, menuId);
  if (!written) model_.log(LogLevel::Warning, "the viewport pie was not saved: " + written.error);
}

std::optional<cm::CustomMenu> EditorApp::viewportPie() const {
  if (const cm::CustomMenu* menu = menuSet_.find(viewportPieId_); menu != nullptr && menu->kind == cm::MenuKind::Pie) return *menu;
  for (const cm::CustomMenu& menu : menuSet_.menus()) {
    if (menu.kind == cm::MenuKind::Pie) return menu;
  }
  return std::nullopt;
}

WidgetId EditorApp::makeViewport(rw::UiContext& ui, WidgetId parent) {
  ViewportPie& trigger = ui.create<ViewportPie>(parent, services(), [this](double, double) { return viewportPie(); }, pieTriggers_);
  trigger.setOnExecuted([this](const std::string& commandId, const rc::ExecuteResult& result) {
    if (!result.isHandled()) model_.log(LogLevel::Warning, "The pie menu could not run " + commandId);
  });
  ui.create<ViewportCanvas>(trigger.id(), model_);
  return trigger.id();
}

void EditorApp::cancelPies() {
  for (rw::PieTrigger* trigger : pieTriggers_) {
    if (trigger->gestureActive()) trigger->cancelGesture();
  }
}

// ---- changes of the set -----------------------------------------------------------------------------

void EditorApp::onMenusChanged() {
  registerMenuPanels();
  if (!viewportPie()) viewportPieId_.clear();
  scheduleMenuBarRefresh();
  registry_.touch();
}

// The dock and the menu bar are brought in line from a timer: a command that changes the set may run from
// an open menu, and closing a panel or rebuilding the bar there would pull widgets out from under the
// notification that is still running.
void EditorApp::scheduleMenuBarRefresh() {
  if (menuBarRefreshPending_) return;
  menuBarRefreshPending_ = true;
  ui_.setTimer(0, [this, alive = alive_] {
    if (!*alive) return;
    menuBarRefreshPending_ = false;
    syncMenuPanels();
    refreshMenuBar();
  });
}

void EditorApp::refreshMenuBar() {
  std::vector<std::string> screens;
  if (host_.screenNames) screens = host_.screenNames();
  customization_.setBuiltin(editorLayoutSet(screens, &menuSet_));
}

// ---- the commands behind the Custom Menus menu ------------------------------------------------------

rw::CustomMenuHooks EditorApp::menuHooks() {
  rw::CustomMenuHooks hooks;
  hooks.openPanel = [this](const std::string& menuId) { openMenuPanel(menuId); };
  hooks.edit = [this](const std::string& menuId) { beginEditMenu(menuId); };
  hooks.save = [this](const std::string& menuId) { askSaveMenu(menuId); };
  hooks.remove = [this](const std::string& menuId) { askDeleteMenu(menuId); };
  hooks.create = [this] { beginCreateMenu(); };
  hooks.load = [this] { askLoadMenu(); };
  return hooks;
}

void EditorApp::beginCreateMenu() {
  creator_.beginCreate();
  showCreator();
}

void EditorApp::beginEditMenu(const std::string& menuId) {
  if (!creator_.beginEdit(menuId)) {
    setStatus("That custom menu no longer exists");
    return;
  }
  showCreator();
}

// ---- the creator window -----------------------------------------------------------------------------

void EditorApp::registerCreatorPanel() {
  rw::PanelDescriptor d;
  d.id = panel::kCreator;
  d.title = "Create Custom Menu";
  d.icon = "plus";
  d.floatSize = {1080.0, 700.0};
  d.minSize = {820.0, 560.0};
  d.suggested = {panel::kViewport, true, dk::Side::Right};
  d.factory = [this](rw::UiContext& ui, WidgetId parent) {
    if (&ui != &ui_) installKeyForwarder(ui);
    rw::CreateCustomMenuWindow& window = ui.create<rw::CreateCustomMenuWindow>(parent, services(), creator_, creatorHooks());
    return window.id();
  };
  panels_.add(std::move(d));
}

rw::CreatorHooks EditorApp::creatorHooks() {
  rw::CreatorHooks hooks;
  hooks.committed = [this](const std::string& menuId, bool edited) { menuCommitted(menuId, edited); };
  hooks.cancelled = [this] { closeCreator(); };
  hooks.saveFile = [this](rw::UiContext& ui, WidgetId owner, const cm::CustomMenu& menu) {
    rw::FilePathOptions options;
    options.mode = rw::FilePathMode::Save;
    options.title = "Save custom menu";
    options.description = "Write this menu to a .r1mn file. Load it later (also on another machine) from Custom Menus > Load Custom Menu.";
    options.folder = menusFolder();
    options.extension = cm::kMenuFileExtension;
    options.initialName = safeFileStem(menu.name);
    chooseFile(ui, owner, std::move(options), [this, menu](const fs::path& path) {
      std::string error;
      if (cm::saveMenuFile(menu, path, error)) {
        model_.info("Custom menu \"" + menu.name + "\" written to " + path.string());
        setStatus("Saved the menu file " + path.string());
      } else {
        model_.log(LogLevel::Error, "save menu: " + error);
        setStatus("The menu file was not saved: " + error);
      }
    });
  };
  hooks.loadFile = [this](rw::UiContext& ui, WidgetId owner) {
    rw::FilePathOptions options;
    options.mode = rw::FilePathMode::Open;
    options.title = "Load custom menu";
    options.description = "Start the new menu from the content of a .r1mn file.";
    options.folder = menusFolder();
    options.extension = cm::kMenuFileExtension;
    chooseFile(ui, owner, std::move(options), [this](const fs::path& path) {
      const cm::MenuParseResult parsed = cm::loadMenuFile(path);
      if (!parsed.ok) {
        model_.log(LogLevel::Error, "load menu: " + parsed.error);
        setStatus("The menu file could not be used: " + parsed.error);
        return;
      }
      if (!creator_.beginFromFile(parsed.menu)) setStatus("The menu in that file cannot be edited");
      else setStatus("Loaded " + path.string() + " into the creator");
    });
  };
  return hooks;
}

void EditorApp::showCreator() {
  deferred([this] {
    rw::DockHost* d = dockPtr();
    if (d == nullptr) return;
    if (!d->openPanel(panel::kCreator)) {
      setStatus("The creator window could not be opened: " + d->lastError());
      return;
    }
    d->activatePanel(panel::kCreator);
    const std::optional<dk::PanelSlot> slot = d->layout().locate(panel::kCreator);
    if (slot && slot->area == dk::kMainAreaId) floatPanelSized(panel::kCreator, 1080.0, 700.0);  // a window of its own
  });
}

bool EditorApp::floatPanelSized(dk::PanelId panelId, double width, double height) {
  rw::DockHost* d = dockPtr();
  if (d == nullptr || !d->floatPanel(panelId)) return false;
  const std::optional<dk::PanelSlot> slot = d->layout().locate(panelId);
  if (!slot) return false;
  const dk::Rect main = d->mainContentRect();
  const double w = std::max(300.0, std::min(width, main.w - 40.0));
  const double h = std::max(200.0, std::min(height, main.h - 40.0));
  return d->setWindowRect(slot->area, {main.x + (main.w - w) * 0.5, main.y + (main.h - h) * 0.5, w, h});
}

void EditorApp::closeCreator() {
  deferred([this] {
    if (rw::DockHost* d = dockPtr(); d != nullptr && d->layout().isDocked(panel::kCreator)) d->closePanel(panel::kCreator);
  });
}

void EditorApp::menuCommitted(const std::string& menuId, bool edited) {
  const cm::CustomMenu* menu = menuSet_.find(menuId);
  const std::string name = menu != nullptr ? menu->name : menuId;
  model_.info(std::string(edited ? "Saved the custom menu \"" : "Created the custom menu \"") + name + "\"");
  setStatus(std::string(edited ? "Saved the custom menu " : "Created the custom menu ") + name);
  if (menu != nullptr && menu->kind == cm::MenuKind::Pie) rememberViewportPie(menuId);
  const bool isPanel = menu != nullptr && menu->kind == cm::MenuKind::Panel;
  deferred([this, menuId, isPanel] {
    if (rw::DockHost* d = dockPtr(); d != nullptr && d->layout().isDocked(panel::kCreator)) d->closePanel(panel::kCreator);
    if (isPanel) openMenuPanel(menuId);
  });
}

// ---- files of single menus --------------------------------------------------------------------------

void EditorApp::askSaveMenu(const std::string& menuId) {
  const cm::CustomMenu* menu = menuSet_.find(menuId);
  if (menu == nullptr) return;
  rw::FilePathOptions options;
  options.mode = rw::FilePathMode::Save;
  options.title = "Save custom menu";
  options.description = "Write \"" + menu->name + "\" to a .r1mn file.";
  options.folder = menusFolder();
  options.extension = cm::kMenuFileExtension;
  options.initialName = safeFileStem(menu->name);
  chooseFile(ui_, root_, std::move(options), [this, menuId](const fs::path& path) { saveMenuTo(menuId, path); });
}

void EditorApp::askLoadMenu() {
  rw::FilePathOptions options;
  options.mode = rw::FilePathMode::Open;
  options.title = "Load custom menu";
  options.description = "Add the menu in a .r1mn file to your custom menus (a name that exists gets a number).";
  options.folder = menusFolder();
  options.extension = cm::kMenuFileExtension;
  chooseFile(ui_, root_, std::move(options), [this](const fs::path& path) { loadMenuFrom(path); });
}

void EditorApp::askDeleteMenu(const std::string& menuId) {
  const cm::CustomMenu* menu = menuSet_.find(menuId);
  if (menu == nullptr) return;
  confirm("Delete custom menu", "Delete the " + std::string(menu->kind == cm::MenuKind::Pie ? "pie" : "dockable") + " menu \"" + menu->name + "\"? Save it to a file first if you want it back.", "Delete", true,
          [this, menuId] {
            const cm::MenuEditResult result = menuSet_.deleteMenu(menuId);
            setStatus(result.ok ? "Deleted the custom menu" : "Could not delete: " + result.reason);
            if (result.ok) model_.info("Custom menu " + menuId + " deleted");
          });
}

bool EditorApp::saveMenuTo(const std::string& menuId, const fs::path& file) {
  const cm::CustomMenu* menu = menuSet_.find(menuId);
  if (menu == nullptr) return false;
  std::string error;
  if (!cm::saveMenuFile(*menu, file, error)) {
    model_.log(LogLevel::Error, "save menu: " + error);
    setStatus("The menu file was not saved: " + error);
    return false;
  }
  model_.info("Custom menu \"" + menu->name + "\" written to " + cm::withMenuExtension(file).string());
  setStatus("Saved the menu file " + cm::withMenuExtension(file).string());
  return true;
}

bool EditorApp::loadMenuFrom(const fs::path& file) {
  const cm::ImportMenuResult result = cm::importMenuFile(menuSet_, file, cm::CollisionPolicy::Rename);
  if (!result.ok) {
    model_.log(LogLevel::Error, "load menu: " + result.error);
    setStatus("The menu file could not be used: " + result.error);
    return false;
  }
  for (const cm::MenuIssue& issue : result.issues) model_.log(LogLevel::Warning, "menu file " + issue.where + ": " + issue.message);
  const std::string id = result.adopted.id;
  model_.info("Loaded the custom menu \"" + result.adopted.name + "\" from " + file.string());
  setStatus("Loaded the custom menu " + result.adopted.name + (result.adopted.renamed ? " (the name was taken, a number was added)" : ""));
  const cm::CustomMenu* menu = menuSet_.find(id);
  if (menu != nullptr && menu->kind == cm::MenuKind::Pie) rememberViewportPie(id);
  if (menu != nullptr && menu->kind == cm::MenuKind::Panel) deferred([this, id] { openMenuPanel(id); });
  return true;
}

// ---- file chooser and deferred work -----------------------------------------------------------------

void EditorApp::deferredIn(rw::UiContext& ui, std::function<void()> action) {
  ui.setTimer(0, [alive = alive_, action = std::move(action)] {
    if (*alive) action();
  });
}

// Opens the toolkit's file path dialog in `ui` from the next timer tick (see deferred()).
void EditorApp::chooseFile(rw::UiContext& ui, WidgetId owner, rw::FilePathOptions options, std::function<void(const fs::path&)> onChosen) {
  deferredIn(ui, [context = &ui, owner, options = std::move(options), onChosen = std::move(onChosen), alive = alive_]() mutable {
    options.owner = context->alive(owner) ? owner : WidgetId{};
    rw::openFilePathDialog(*context, std::move(options), [alive, onChosen = std::move(onChosen)](const fs::path& path) {
      if (*alive) onChosen(path);
    });
  });
}

}  // namespace preview::editor
