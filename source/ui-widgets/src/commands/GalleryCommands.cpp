// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GalleryCommands.h: the sample command set and the page that shows it.
// Invariants: only layout boxes, Labels, Buttons, the menu bar, the toolbar, the context menu
//   controller and the keybinding editor are created; every command callback only touches the page's
//   Sample state and ends with registry.touch(), which refreshes the bound widgets; colours come from
//   the widgets' own rows.
// Callers: the gallery preview, the group's gallery tests.
#include "r1ui/widgets/commands/GalleryCommands.h"

#include <memory>
#include <string>
#include <vector>

#include "LayoutBox.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/commands/CommandMenus.h"
#include "r1ui/widgets/commands/CommandToolbar.h"
#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/commands/KeybindingEditor.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/toolbar/Toolbar.h"

namespace r1ui::widgets {

namespace {

namespace cmd = commands;
namespace layout = core::layout;
using core::events::Key;
using core::tree::WidgetId;

// Everything the page owns. Member order is the construction order (and the reverse destruction order):
// the registry outlives everything that listens to it.
struct Sample {
  explicit Sample(UiContext& ui) : clock(ui), router(registry, keymap, clock), sync(ui, services()) {}
  CommandServices services() { return {registry, overrides, keymap, router}; }

  cmd::CommandRegistry registry;
  cmd::KeybindingOverrides overrides{registry};
  cmd::Keymap keymap{registry, overrides};
  UiClock clock;
  cmd::CommandRouter router;
  CommandUiSync sync;
  std::unique_ptr<CommandToolbarBinding> toolbar;
  std::unique_ptr<MenuController> contextMenu;

  // Document state the commands act on.
  int undoDepth = 2;
  int redoDepth = 0;
  bool grid = true;
  bool rulers = false;
  std::string tool = "tool.select";
  std::string last = "Ready";
  std::string pending;
};

cmd::ChordSequence key(Key k, uint8_t mods = 0) { return cmd::ChordSequence::single({k, mods, false}); }
Key letter(char c) { return static_cast<Key>(c); }

// Declares one command; `run` is the behaviour, the rest is the declaration of spec 07.
struct Declaration {
  const char* id;
  const char* label;
  const char* description;
  const char* icon;
  const char* category;
  cmd::ChordSequence primary;
  cmd::ChordSequence alternate;
};

void declare(Sample& s, const Declaration& d, std::function<void()> run, cmd::CommandKind kind = cmd::CommandKind::Action, std::function<bool()> enabled = {},
             std::function<bool()> checked = {}, const char* context = cmd::kGlobalContext) {
  cmd::CommandDef def;
  def.id = d.id;
  def.label = d.label;
  def.description = d.description;
  def.icon = d.icon;
  def.category = d.category;
  def.context = context;
  def.kind = kind;
  if (kind == cmd::CommandKind::Radio) def.radioGroup = "tools";
  def.defaultChords = {d.primary, d.alternate};
  def.enabled = std::move(enabled);
  def.checked = std::move(checked);
  Sample* sample = &s;
  def.execute = [sample, run = std::move(run), label = std::string(d.label)](const cmd::ExecuteArgs&) {
    run();
    sample->last = label;
    sample->registry.touch();  // the document changed: refresh menus, toolbar and status
    return cmd::ExecuteResult::handled();
  };
  s.registry.add(std::move(def));
}

void declareCommands(Sample& s) {
  using cmd::Mod::kCtrl;
  using cmd::Mod::kShift;
  Sample* p = &s;
  s.registry.addContext("layers", cmd::kWindowContext, false, "Layers panel");
  const auto edit = [p](int undoChange) { return [p, undoChange] { p->undoDepth += undoChange; p->redoDepth = 0; }; };

  declare(s, {"edit.undo", "Undo", "Undo the last change", "undo2", "Edit", key(letter('Z'), kCtrl), {}}, [p] { --p->undoDepth; ++p->redoDepth; }, cmd::CommandKind::Action,
          [p] { return p->undoDepth > 0; });
  declare(s, {"edit.redo", "Redo", "Redo the change that was undone", "redo2", "Edit", key(letter('Y'), kCtrl), key(letter('Z'), kCtrl | kShift)},
          [p] { ++p->undoDepth; --p->redoDepth; }, cmd::CommandKind::Action, [p] { return p->redoDepth > 0; });
  declare(s, {"edit.cut", "Cut", "Move the selection to the clipboard", "scissors", "Edit", key(letter('X'), kCtrl), {}}, edit(1));
  declare(s, {"edit.copy", "Copy", "Copy the selection to the clipboard", "copy", "Edit", key(letter('C'), kCtrl), {}}, [] {});
  declare(s, {"edit.paste", "Paste", "Insert the clipboard", "clipboard", "Edit", key(letter('V'), kCtrl), {}}, edit(1));
  declare(s, {"edit.duplicate", "Duplicate", "Duplicate the selection", "copy-plus", "Edit", key(letter('D'), kCtrl), {}}, edit(1));
  declare(s, {"edit.delete", "Delete", "Delete the selection", "trash-2", "Edit", key(Key::Delete), key(Key::Backspace)}, edit(1));
  declare(s, {"edit.selectAll", "Select all", "Select everything", "scan", "Edit", key(letter('A'), kCtrl), {}}, [] {});

  declare(s, {"file.open", "Open...", "Open a document", "folder-open", "File", key(letter('O'), kCtrl), {}}, [] {});
  declare(s, {"file.save", "Save", "Save the document", "save", "File", key(letter('S'), kCtrl), {}}, [] {});

  declare(s, {"view.grid", "Show grid", "Show or hide the grid", "grid-3x3", "View", key(letter('G'), kCtrl), {}}, [p] { p->grid = !p->grid; }, cmd::CommandKind::Toggle, {},
          [p] { return p->grid; });
  declare(s, {"view.rulers", "Show rulers", "Show or hide the rulers", "panel-top", "View", key(letter('R'), kCtrl), {}}, [p] { p->rulers = !p->rulers; },
          cmd::CommandKind::Toggle, {}, [p] { return p->rulers; });
  declare(s, {"view.zoomIn", "Zoom in", "Zoom in", "zoom-in", "View", key(Key::Up, kCtrl), {}}, [] {});
  declare(s, {"view.zoomOut", "Zoom out", "Zoom out", "minus", "View", key(Key::Down, kCtrl), {}}, [] {});
  declare(s, {"view.fit", "Fit to window", "Fit the document to the window (Ctrl+K, then Ctrl+F)", "maximize", "View", cmd::ChordSequence::pair({letter('K'), kCtrl, false}, {letter('F'), kCtrl, false}), {}}, [] {});

  const auto tool = [p](const char* id) { return [p, id] { p->tool = id; }; };
  const auto isTool = [p](const char* id) { return [p, id] { return p->tool == id; }; };
  declare(s, {"tool.select", "Select", "Select and move objects", "mouse-pointer", "Tools", key(letter('V')), {}}, tool("tool.select"), cmd::CommandKind::Radio, {}, isTool("tool.select"));
  declare(s, {"tool.pen", "Pen", "Draw paths", "pen-tool", "Tools", key(letter('P')), {}}, tool("tool.pen"), cmd::CommandKind::Radio, {}, isTool("tool.pen"));
  declare(s, {"tool.text", "Text", "Place text", "type", "Tools", key(letter('T')), {}}, tool("tool.text"), cmd::CommandKind::Radio, {}, isTool("tool.text"));
  declare(s, {"tool.hand", "Hand", "Pan the view", "hand", "Tools", key(letter('H')), {}}, tool("tool.hand"), cmd::CommandKind::Radio, {}, isTool("tool.hand"));

  declare(s, {"layers.delete", "Delete layer", "Delete the selected layer (only with the layers panel focused)", "trash-2", "Layers", key(Key::Delete), {}}, edit(1),
          cmd::CommandKind::Action, {}, {}, "layers");
}

// ---- the page ----------------------------------------------------------------------------------

class GalleryCommandsPage final : public WidgetObject {
 public:
  const char* typeName() const override { return "GalleryCommands"; }

  void onAttached() override {
    sample_ = std::make_unique<Sample>(ui());
    Sample& s = *sample_;
    declareCommands(s);
    style().direction = layout::FlexDirection::Column;
    style().gapRow = 12.0;
    style().flexShrink = 0.0;
    style().padding[layout::kLeft] = style().padding[layout::kRight] = style().padding[layout::kTop] = style().padding[layout::kBottom] = 12.0;

    ui().create<Label>(id(), "Commands", LabelRole::Title);
    ui().create<Label>(id(), "One registration drives the menu, the toolbar and the shortcut. Click here, then try V P T H, Ctrl+Z, Ctrl+K then Ctrl+F.", LabelRole::Muted);

    buildMenus(s);
    buildToolbar(s);
    buildLayersAndStatus(s);

    KeybindingEditor& editor = ui().create<KeybindingEditor>(id(), s.services());
    editor.style().height = layout::Length::px(440);
    editor.style().flexShrink = 0.0;
    setFocusable(true);

    s.router.setPendingObserver([this](const cmd::PendingState& state) {
      sample_->pending = state.active ? state.text + " ... waiting for the second key" : std::string();
      sample_->registry.touch();
    });
    refreshAttachment_ = s.sync.attach([this] { refreshStatus(); });
    refreshStatus();
  }

  void onDetached() override {
    if (timer_ != 0) ui().cancelTimer(timer_);
    refreshAttachment_.reset();
    if (sample_) {
      sample_->router.setPendingObserver({});
      if (sample_->contextMenu) sample_->contextMenu->close();
    }
  }

  // A click on empty page area gives the page the keyboard (a click a child used keeps its focus).
  void onClick(Event& e) override {
    if (!e.handled) ui().focusWidget(id());
  }

  // The keys the page's widgets leave unused reach the router with the context of the focus path.
  void onKeyDown(Event& e) override {
    if (!sample_) return;
    std::vector<std::string> contexts;
    const WidgetId focused = ui().router().focused();
    const WidgetObject* object = ui().object(focused);
    if (object != nullptr && object->wantsTextInput()) {
      contexts.push_back(cmd::kTextContext);
    } else if (inside(focused, layersBox_)) {
      contexts.push_back("layers");
    } else {
      contexts.push_back(cmd::kWindowContext);
    }
    const cmd::RouteResult result = sample_->router.handleKey({e.key, e.modifiers, e.repeat, false}, contexts);
    if (result.pendingStarted) {
      if (timer_ != 0) ui().cancelTimer(timer_);
      cmd::CommandRouter* router = &sample_->router;
      timer_ = ui().setTimer(router->pendingRemainingMs(), [router] { router->tick(); });
    }
    if (result.consumed) e.markHandled();
  }

 private:
  bool inside(WidgetId widget, WidgetId container) const {
    for (WidgetId w = widget; w.valid(); w = ui().tree().parent(w)) {
      if (w == container) return true;
    }
    return false;
  }

  void buildMenus(Sample& s) {
    MenuBar& bar = ui().create<MenuBar>(id());
    bar.style().alignSelf = layout::Align::Start;
    using Entry = CommandMenuEntry;
    bindCommandMenuBar(
        bar, s.services(),
        {{"File", {Entry::command("file.open"), Entry::command("file.save")}},
         {"Edit",
          {Entry::command("edit.undo"), Entry::command("edit.redo"), Entry::separator(), Entry::command("edit.cut"), Entry::command("edit.copy"), Entry::command("edit.paste"),
           Entry::command("edit.duplicate"), Entry::separator(), Entry::command("edit.delete"), Entry::command("edit.selectAll")}},
         {"View",
          {Entry::command("view.grid"), Entry::command("view.rulers"), Entry::separator(), Entry::command("view.zoomIn"), Entry::command("view.zoomOut"), Entry::command("view.fit")}},
         {"Tools", {Entry::command("tool.select"), Entry::command("tool.pen"), Entry::command("tool.text"), Entry::command("tool.hand")}}});
  }

  void buildToolbar(Sample& s) {
    Toolbar& bar = ui().create<Toolbar>(id());
    bar.style().alignSelf = layout::Align::Start;
    using Item = CommandToolbarItem;
    s.toolbar = bindCommandToolbar(ui(), bar, s.sync,
                                   {Item::command("tool.select"), Item::command("tool.pen"), Item::command("tool.text"), Item::command("tool.hand"), Item::separator(),
                                    Item::command("edit.undo"), Item::command("edit.redo"), Item::separator(), Item::command("view.grid"), Item::command("view.rulers")});
  }

  void buildLayersAndStatus(Sample& s) {
    CommandsLayoutBox& row = layoutRow(ui(), id(), 12);
    row.style().flexShrink = 0.0;
    Button& context = ui().create<Button>(row.id(), "Open context menu", ButtonTone::Neutral, ButtonSize::Sm);
    s.contextMenu = std::make_unique<MenuController>(ui());
    const WidgetId contextButton = context.id();
    context.setOnClick([this, contextButton] {
      const core::layout::Rect r = ui().absRect(contextButton);
      using Entry = CommandMenuEntry;
      const std::vector<Entry> entries{Entry::command("edit.cut"), Entry::command("edit.copy"), Entry::command("edit.paste"), Entry::separator(), Entry::command("edit.duplicate"),
                                       Entry::command("edit.delete"), Entry::separator(), Entry::command("view.grid")};
      openCommandContextMenu(*sample_->contextMenu, sample_->services(), entries, r.x, r.y + r.h + 4.0);
    });

    CommandsLayoutBox& layers = layoutRow(ui(), row.id(), 8);
    layersBox_ = layers.id();
    ui().create<Button>(layers.id(), "Layers panel (focus it, then press Delete)", ButtonTone::Neutral, ButtonSize::Sm);

    status_ = ui().create<Label>(id(), "", LabelRole::Body).id();
  }

  void refreshStatus() {
    if (!sample_) return;
    Label* label = ui().objectAs<Label>(status_);
    if (label == nullptr) return;
    const Sample& s = *sample_;
    std::string text = "Last command: " + s.last + "   Undo " + std::to_string(s.undoDepth) + " / Redo " + std::to_string(s.redoDepth) + "   Tool: " + s.tool.substr(5);
    if (!s.pending.empty()) text = s.pending;
    label->setText(std::move(text));
  }

  std::unique_ptr<Sample> sample_;
  CommandUiSync::Attachment refreshAttachment_;
  WidgetId layersBox_;
  WidgetId status_;
  uint32_t timer_ = 0;
};

}  // namespace

void buildGalleryCommands(UiContext& ui, WidgetId parent) { ui.create<GalleryCommandsPage>(parent); }

}  // namespace r1ui::widgets
