// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GalleryCustomize.h: the sample commands, the built-in layouts, the page.
// Invariants: only layout boxes, Labels and the customize widgets are created; every command callback
//   touches only the page's State and ends with registry.touch(); colours come from the widgets' rows;
//   the State is built in onAttached (it needs the UiContext) and released in the destructor, after the
//   child widgets (and their subscriptions) are gone.
// Callers: the gallery preview, the group's gallery tests.
#include "r1ui/widgets/customize/GalleryCustomize.h"

#include <memory>
#include <string>
#include <vector>

#include "CustomizeBox.h"
#include "r1ui/commands/customize/CustomizationIo.h"
#include "r1ui/widgets/commands/CommandMenus.h"
#include "r1ui/widgets/commands/CommandToolbar.h"
#include "r1ui/widgets/customize/LayoutConvert.h"
#include "r1ui/widgets/label/Label.h"

namespace r1ui::widgets {

namespace cmd = commands;
namespace cz = commands::customize;
namespace layout = core::layout;
using core::events::Key;

namespace {

cmd::ChordSequence key(Key k, uint8_t mods = 0) { return cmd::ChordSequence::single({k, mods, false}); }
Key letter(char c) { return static_cast<Key>(c); }

cz::LayoutSet sampleLayouts() {
  using E = CommandMenuEntry;
  cz::LayoutSet set;
  set.menuBar.menus.push_back(menuNodeFromEntries("menu.file", "File", {E::command("file.open"), E::command("file.save"), E::separator(), E::command("file.export")}));
  set.menuBar.menus.push_back(menuNodeFromEntries("menu.edit", "Edit",
                                                  {E::command("edit.undo"), E::command("edit.redo"), E::separator(), E::command("edit.cut"), E::command("edit.copy"),
                                                   E::command("edit.paste"), E::separator(), E::heading("Selection"), E::command("edit.duplicate"), E::command("edit.delete"),
                                                   E::command("edit.selectAll")}));
  set.menuBar.menus.push_back(menuNodeFromEntries("menu.view", "View",
                                                  {E::command("view.grid"), E::command("view.rulers"), E::separator(),
                                                   E::submenu("Zoom", {E::command("view.zoomIn"), E::command("view.zoomOut"), E::command("view.fit")})}));
  set.menuBar.menus.push_back(menuNodeFromEntries("menu.tools", "Tools", {E::command("tool.select"), E::command("tool.pen"), E::command("tool.text"), E::command("tool.hand")}));
  cz::Node help = menuNodeFromEntries("menu.help", "Help", {E::command("help.docs"), E::command("help.about")});
  help.locked = true;  // the owner has locked this menu: it cannot be customized
  set.menuBar.menus.push_back(std::move(help));

  using I = CommandToolbarItem;
  set.toolbars.push_back(toolbarFromItems("tb.tools", "Tools",
                                          {I::command("tool.select"), I::command("tool.pen"), I::command("tool.text"), I::command("tool.hand"), I::separator(),
                                           I::group({"tool.rect", "tool.ellipse"}), I::separator(), I::command("edit.undo"), I::command("edit.redo")}));
  cz::ToolbarLayout side = toolbarFromItems("tb.side", "Side bar", {I::command("view.grid"), I::command("view.rulers"), I::command("view.fit")});
  side.orientation = cz::Orientation::Vertical;
  side.sizeStep = cz::SizeStep::Small;
  set.toolbars.push_back(std::move(side));
  cz::ToolbarLayout locked = toolbarFromItems("tb.locked", "Locked bar", {I::command("file.open"), I::command("file.save")});
  locked.locked = true;
  set.toolbars.push_back(std::move(locked));

  cz::FreeFormPanelLayout panel;
  panel.id = "fp.quick";
  panel.title = "Quick actions";
  panel.width = 380;
  panel.height = 150;
  panel.buttons = {cz::Node::freeButton("fp.quick.save", "file.save", {12, 12, 100, 32}), cz::Node::freeButton("fp.quick.undo", "edit.undo", {124, 12, 100, 32}),
                   cz::Node::freeButton("fp.quick.grid", "view.grid", {236, 12, 120, 32})};
  set.panels.push_back(std::move(panel));
  return set;
}

}  // namespace

struct GalleryCustomizePage::State {
  explicit State(UiContext& ui) : clock(ui), router(registry, keymap, clock), sync(ui, services()), model(sampleLayouts()), storage(model, store), controller(ui, services(), sync, model) {}
  CommandServices services() { return {registry, overrides, keymap, router}; }

  cmd::CommandRegistry registry;
  cmd::KeybindingOverrides overrides{registry};
  cmd::Keymap keymap{registry, overrides};
  UiClock clock;
  cmd::CommandRouter router;
  CommandUiSync sync;
  cz::Customization model;
  cz::MemoryTextStore store;
  cz::CustomizationStorage storage;
  CustomizeController controller;

  int undoDepth = 2;
  int redoDepth = 0;
  bool grid = true;
  bool rulers = false;
  std::string tool = "tool.select";
  std::string status = "Ready";

  core::tree::WidgetId menuBar, mainToolbar, sideToolbar, lockedToolbar, panel, palette, strip, statusLabel;
  CommandUiSync::Attachment attachment;
};

namespace {

using State = GalleryCustomizePage::State;

void declare(State& s, const char* id, const char* label, const char* description, const char* icon, const char* category, cmd::ChordSequence primary,
             std::function<void()> run, cmd::CommandKind kind = cmd::CommandKind::Action, std::function<bool()> enabled = {}, std::function<bool()> checked = {}) {
  cmd::CommandDef def;
  def.id = id;
  def.label = label;
  def.description = description;
  def.icon = icon;
  def.category = category;
  def.kind = kind;
  if (kind == cmd::CommandKind::Radio) def.radioGroup = "tools";
  def.defaultChords = {primary, {}};
  def.enabled = std::move(enabled);
  def.checked = std::move(checked);
  State* state = &s;
  def.execute = [state, run = std::move(run), name = std::string(label)](const cmd::ExecuteArgs&) {
    run();
    state->status = "Last command: " + name;
    state->registry.touch();
    return cmd::ExecuteResult::handled();
  };
  s.registry.add(std::move(def));
}

void declareCommands(State& s) {
  using cmd::Mod::kCtrl;
  using cmd::Mod::kShift;
  State* p = &s;
  const auto none = [] {};
  declare(s, "file.open", "Open...", "Open a document", "folder-open", "File", key(letter('O'), kCtrl), none);
  declare(s, "file.save", "Save", "Save the document", "save", "File", key(letter('S'), kCtrl), none);
  declare(s, "file.export", "Export...", "Export the document", "download", "File", key(letter('E'), kCtrl | kShift), none);
  declare(s, "edit.undo", "Undo", "Undo the last change", "undo2", "Edit", key(letter('Z'), kCtrl), [p] { --p->undoDepth; ++p->redoDepth; }, cmd::CommandKind::Action,
          [p] { return p->undoDepth > 0; });
  declare(s, "edit.redo", "Redo", "Redo the change that was undone", "redo2", "Edit", key(letter('Y'), kCtrl), [p] { ++p->undoDepth; --p->redoDepth; }, cmd::CommandKind::Action,
          [p] { return p->redoDepth > 0; });
  const auto change = [p] { ++p->undoDepth; p->redoDepth = 0; };
  declare(s, "edit.cut", "Cut", "Move the selection to the clipboard", "scissors", "Edit", key(letter('X'), kCtrl), change);
  declare(s, "edit.copy", "Copy", "Copy the selection to the clipboard", "copy", "Edit", key(letter('C'), kCtrl), none);
  declare(s, "edit.paste", "Paste", "Insert the clipboard", "clipboard", "Edit", key(letter('V'), kCtrl), change);
  declare(s, "edit.duplicate", "Duplicate", "Duplicate the selection", "copy-plus", "Edit", key(letter('D'), kCtrl), change);
  declare(s, "edit.delete", "Delete", "Delete the selection", "trash-2", "Edit", key(Key::Delete), change);
  declare(s, "edit.selectAll", "Select all", "Select everything", "scan", "Edit", key(letter('A'), kCtrl), none);
  declare(s, "view.grid", "Show grid", "Show or hide the grid", "grid-3x3", "View", key(letter('G'), kCtrl), [p] { p->grid = !p->grid; }, cmd::CommandKind::Toggle, {},
          [p] { return p->grid; });
  declare(s, "view.rulers", "Show rulers", "Show or hide the rulers", "panel-top", "View", key(letter('R'), kCtrl), [p] { p->rulers = !p->rulers; }, cmd::CommandKind::Toggle, {},
          [p] { return p->rulers; });
  declare(s, "view.zoomIn", "Zoom in", "Zoom in", "zoom-in", "View", key(Key::Up, kCtrl), none);
  declare(s, "view.zoomOut", "Zoom out", "Zoom out", "minus", "View", key(Key::Down, kCtrl), none);
  declare(s, "view.fit", "Fit to window", "Fit the document to the window", "maximize", "View", {}, none);
  const auto tool = [p](const char* id) { return [p, id] { p->tool = id; }; };
  const auto isTool = [p](const char* id) { return [p, id] { return p->tool == id; }; };
  declare(s, "tool.select", "Select", "Select and move objects", "mouse-pointer", "Tools", key(letter('V')), tool("tool.select"), cmd::CommandKind::Radio, {}, isTool("tool.select"));
  declare(s, "tool.pen", "Pen", "Draw paths", "pen-tool", "Tools", key(letter('P')), tool("tool.pen"), cmd::CommandKind::Radio, {}, isTool("tool.pen"));
  declare(s, "tool.text", "Text", "Place text", "type", "Tools", key(letter('T')), tool("tool.text"), cmd::CommandKind::Radio, {}, isTool("tool.text"));
  declare(s, "tool.hand", "Hand", "Pan the view", "hand", "Tools", key(letter('H')), tool("tool.hand"), cmd::CommandKind::Radio, {}, isTool("tool.hand"));
  declare(s, "tool.rect", "Rectangle", "Draw rectangles", "square", "Tools", key(letter('R')), tool("tool.rect"), cmd::CommandKind::Radio, {}, isTool("tool.rect"));
  declare(s, "tool.ellipse", "Ellipse", "Draw ellipses", "circle", "Tools", key(letter('E')), tool("tool.ellipse"), cmd::CommandKind::Radio, {}, isTool("tool.ellipse"));
  declare(s, "help.docs", "Documentation", "Open the documentation", "book-open", "Help", key(Key::F1), none);
  declare(s, "help.about", "About", "About this application", "circle-alert", "Help", {}, none);
}

}  // namespace

GalleryCustomizePage::GalleryCustomizePage() = default;
GalleryCustomizePage::~GalleryCustomizePage() = default;

CustomizeController& GalleryCustomizePage::controller() const { return state_->controller; }
cz::Customization& GalleryCustomizePage::model() const { return state_->model; }
cmd::CommandRegistry& GalleryCustomizePage::registry() const { return state_->registry; }
CommandServices GalleryCustomizePage::services() const { return state_->services(); }
CommandUiSync& GalleryCustomizePage::sync() const { return state_->sync; }
CustomizableMenuBar& GalleryCustomizePage::menuBar() const { return *ui().objectAs<CustomizableMenuBar>(state_->menuBar); }
CustomizableToolbar& GalleryCustomizePage::mainToolbar() const { return *ui().objectAs<CustomizableToolbar>(state_->mainToolbar); }
CustomizableToolbar& GalleryCustomizePage::sideToolbar() const { return *ui().objectAs<CustomizableToolbar>(state_->sideToolbar); }
CustomizableToolbar& GalleryCustomizePage::lockedToolbar() const { return *ui().objectAs<CustomizableToolbar>(state_->lockedToolbar); }
FreeFormPanel& GalleryCustomizePage::panel() const { return *ui().objectAs<FreeFormPanel>(state_->panel); }
CommandPalette& GalleryCustomizePage::palette() const { return *ui().objectAs<CommandPalette>(state_->palette); }
CustomizeToolStrip& GalleryCustomizePage::strip() const { return *ui().objectAs<CustomizeToolStrip>(state_->strip); }
const std::string& GalleryCustomizePage::status() const { return state_->status; }

void GalleryCustomizePage::onAttached() {
  state_ = std::make_unique<State>(ui());
  State& s = *state_;
  declareCommands(s);
  style().direction = layout::FlexDirection::Column;
  style().gapRow = 12.0;
  style().flexShrink = 0.0;
  for (double& p : style().padding) p = 12.0;
  setFocusable(true);

  ui().create<Label>(id(), "Customization", LabelRole::Title);
  ui().create<Label>(id(), "Press Customize, then drag a command from the palette onto a menu, a toolbar or the panel, hide entries with the eye, double-click a name to rename it.", LabelRole::Muted);
  s.strip = ui().create<CustomizeToolStrip>(id(), s.controller).id();

  cust::CustomizeBox& body = cust::row(ui(), id(), 16.0);
  body.style().alignItems = layout::Align::Start;
  body.style().flexShrink = 0.0;
  cust::CustomizeBox& left = cust::column(ui(), body.id(), 12.0);
  left.style().flexShrink = 0.0;
  left.style().width = layout::Length::px(640.0);
  s.menuBar = ui().create<CustomizableMenuBar>(left.id(), s.controller).id();
  cust::CustomizeBox& bars = cust::row(ui(), left.id(), 12.0);
  bars.style().alignItems = layout::Align::Start;
  bars.style().alignContent = layout::AlignContent::Start;
  bars.style().wrap = layout::FlexWrap::Wrap;
  bars.style().gapRow = 12.0;
  bars.style().flexShrink = 0.0;
  s.mainToolbar = ui().create<CustomizableToolbar>(bars.id(), s.controller, "tb.tools").id();
  s.sideToolbar = ui().create<CustomizableToolbar>(bars.id(), s.controller, "tb.side").id();
  s.lockedToolbar = ui().create<CustomizableToolbar>(bars.id(), s.controller, "tb.locked").id();
  s.panel = ui().create<FreeFormPanel>(left.id(), s.controller, "fp.quick").id();
  s.statusLabel = ui().create<Label>(left.id(), s.status, LabelRole::Muted).id();

  cust::CustomizeBox& right = cust::column(ui(), body.id(), 6.0);
  right.style().flexShrink = 0.0;
  ui().create<Label>(right.id(), "Commands", LabelRole::Heading);
  CommandPalette& palette = ui().create<CommandPalette>(right.id(), s.controller);
  palette.style().width = layout::Length::px(300.0);
  palette.style().height = layout::Length::px(380.0);
  palette.style().flexShrink = 0.0;
  s.palette = palette.id();

  s.attachment = s.sync.attach([this] {
    if (Label* l = ui().objectAs<Label>(state_->statusLabel)) {
      if (l->text() != state_->status) l->setText(state_->status);
    }
  });
}

void GalleryCustomizePage::onDetached() {
  if (state_) state_->attachment.reset();
}

void GalleryCustomizePage::onClick(Event& e) {
  if (!e.handled) ui().focusWidget(id());
}

void GalleryCustomizePage::onKeyDown(Event& e) {
  if (!state_) return;
  const std::vector<std::string> contexts{cmd::kWindowContext};
  if (state_->router.handleKey({e.key, e.modifiers, e.repeat, false}, contexts).consumed) e.markHandled();
}

GalleryCustomizePage& createGalleryCustomize(UiContext& ui, core::tree::WidgetId parent) { return ui.create<GalleryCustomizePage>(parent); }

void buildGalleryCustomize(UiContext& ui, core::tree::WidgetId parent) { createGalleryCustomize(ui, parent); }

}  // namespace r1ui::widgets
