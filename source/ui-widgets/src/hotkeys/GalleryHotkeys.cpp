// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GalleryHotkeys.h: the sample command set and the two pages.
// Invariants: every sample command has a non-empty description; the page owns the command objects and
//   outlives the widgets built on them only through the widget tree (the widgets are children of the
//   page and go first); the drop zone unregisters from the hub in onDetached.
// Callers: the gallery preview, gallery tests.
#include "r1ui/widgets/hotkeys/GalleryHotkeys.h"

#include <memory>
#include <string>
#include <vector>

#include "HotkeyBox.h"
#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/CommandRouter.h"
#include "r1ui/commands/Keymap.h"
#include "r1ui/widgets/actions/ActionList.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/hotkeys/HotkeyEditor.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/runtime/PaintContext.h"

namespace r1ui::widgets {

namespace {

namespace cmd = commands;
namespace layout = core::layout;
using core::events::Key;
using core::tree::WidgetId;
namespace Mod = core::events::Mod;

struct Sample {
  explicit Sample(UiContext& ui) : clock(ui), router(registry, keymap, clock) {}
  CommandServices services() { return {registry, overrides, keymap, router}; }

  cmd::CommandRegistry registry;
  cmd::KeybindingOverrides overrides{registry};
  cmd::Keymap keymap{registry, overrides};
  UiClock clock;
  cmd::CommandRouter router;
};

struct Row {
  const char* id;
  const char* label;
  const char* description;
  const char* category;
  const char* icon;
  Key key;
  uint8_t mods;
  Key altKey;
  uint8_t altMods;
};

constexpr Key letter(char c) { return static_cast<Key>(c); }
constexpr Key fn(int n) { return static_cast<Key>(static_cast<int>(Key::F1) + n - 1); }
constexpr Key kNone = Key::Unknown;
constexpr uint8_t C = Mod::kCtrl, S = Mod::kShift, A = Mod::kAlt;

// Ten categories; the descriptions are one sentence in the user's words.
const Row kRows[] = {
    {"file.new", "New document", "Create an empty document in a new tab.", "File", "file", letter('N'), C, kNone, 0},
    {"file.open", "Open...", "Choose a document on disk and open it.", "File", "folder-open", letter('O'), C, kNone, 0},
    {"file.save", "Save", "Write the document to its file.", "File", "save", letter('S'), C, kNone, 0},
    {"file.saveAs", "Save as...", "Write the document to a new file and keep working on that one.", "File", "save", letter('S'), C | S, kNone, 0},
    {"file.export", "Export...", "Write the document in a format other programs read.", "File", "download", letter('E'), C | S, kNone, 0},
    {"file.close", "Close document", "Close the current tab; asks first when it has unsaved changes.", "File", "x", letter('W'), C, kNone, 0},
    {"edit.undo", "Undo", "Take back the last change.", "Edit", "undo2", letter('Z'), C, kNone, 0},
    {"edit.redo", "Redo", "Do again the change that was taken back.", "Edit", "rotate-cw", letter('Z'), C | S, letter('Y'), C},
    {"edit.cut", "Cut", "Move the selection to the clipboard.", "Edit", "scissors", letter('X'), C, kNone, 0},
    {"edit.copy", "Copy", "Copy the selection to the clipboard.", "Edit", "copy", letter('C'), C, kNone, 0},
    {"edit.paste", "Paste", "Insert the clipboard at the cursor.", "Edit", "clipboard", letter('V'), C, kNone, 0},
    {"edit.duplicate", "Duplicate", "Make a copy of the selection next to the original.", "Edit", "copy", letter('D'), C, kNone, 0},
    {"edit.delete", "Delete", "Remove the selection.", "Edit", "trash-2", Key::Delete, 0, Key::Backspace, 0},
    {"edit.rename", "Rename", "Edit the name of the selected item in place.", "Edit", "pencil", fn(2), 0, kNone, 0},
    {"select.all", "Select all", "Select everything in the document.", "Select", "square", letter('A'), C, kNone, 0},
    {"select.none", "Select none", "Clear the selection.", "Select", "square", letter('A'), C | S, kNone, 0},
    {"select.invert", "Invert selection", "Select what is not selected and deselect what is.", "Select", "square", letter('I'), C | S, kNone, 0},
    {"select.grow", "Grow selection", "Add the neighbours of the selection to it.", "Select", "plus", Key::Up, A, kNone, 0},
    {"select.shrink", "Shrink selection", "Remove the outer ring of the selection.", "Select", "minus", Key::Down, A, kNone, 0},
    {"view.zoomIn", "Zoom in", "Magnify the view around the pointer.", "View", "zoom-in", Key::Up, C, kNone, 0},
    {"view.zoomOut", "Zoom out", "Shrink the view around the pointer.", "View", "minus", Key::Down, C, kNone, 0},
    {"view.fit", "Fit to window", "Zoom so the whole document is visible.", "View", "maximize", Key::Digit0, C, kNone, 0},
    {"view.actual", "Actual size", "Show the document at 100 percent.", "View", "maximize", static_cast<Key>('1'), C, kNone, 0},
    {"view.grid", "Show grid", "Toggle the grid lines behind the content.", "View", "grid-3x3", letter('G'), C, kNone, 0},
    {"view.rulers", "Show rulers", "Toggle the rulers along the top and the left edge.", "View", "grid-3x3", letter('R'), C, kNone, 0},
    {"tool.select", "Select tool", "Pick and move objects.", "Tools", "mouse-pointer", letter('V'), 0, kNone, 0},
    {"tool.pen", "Pen tool", "Draw paths point by point.", "Tools", "pen-tool", letter('P'), 0, kNone, 0},
    {"tool.text", "Text tool", "Click to type text on the canvas.", "Tools", "type", letter('T'), 0, kNone, 0},
    {"tool.hand", "Hand tool", "Drag the canvas around without changing anything.", "Tools", "hand", letter('H'), 0, kNone, 0},
    {"tool.zoom", "Zoom tool", "Click to zoom in, hold Alt to zoom out.", "Tools", "zoom-in", letter('Z'), 0, kNone, 0},
    {"tool.eraser", "Eraser", "Rub out parts of the selected object.", "Tools", "x", letter('E'), 0, kNone, 0},
    {"xform.rotateCw", "Rotate clockwise", "Turn the selection a quarter turn to the right.", "Transform", "rotate-cw", letter('R'), A, kNone, 0},
    {"xform.rotateCcw", "Rotate counter-clockwise", "Turn the selection a quarter turn to the left.", "Transform", "rotate-ccw-square", letter('R'), A | S, kNone, 0},
    {"xform.flipH", "Flip horizontally", "Mirror the selection left to right.", "Transform", "flip-horizontal", letter('H'), C | S, kNone, 0},
    {"xform.flipV", "Flip vertically", "Mirror the selection top to bottom.", "Transform", "flip-vertical2", letter('V'), C | S, kNone, 0},
    {"layer.new", "New layer", "Add an empty layer above the current one.", "Layers", "layers", letter('N'), C | S, kNone, 0},
    {"layer.group", "Group layers", "Put the selected layers into one group.", "Layers", "layers", letter('G'), C | S, kNone, 0},
    {"layer.lock", "Lock layer", "Prevent the layer from being edited.", "Layers", "lock", letter('L'), C, kNone, 0},
    {"layer.hide", "Hide layer", "Hide or show the current layer.", "Layers", "eye", letter('B'), C, kNone, 0},
    {"layer.up", "Raise layer", "Move the layer one step towards the front.", "Layers", "arrow-up-to-line", Key::PageUp, C, kNone, 0},
    {"layer.down", "Lower layer", "Move the layer one step towards the back.", "Layers", "arrow-down", Key::PageDown, C, kNone, 0},
    {"window.console", "Show console", "Open the console panel with the log.", "Window", "file", letter('J'), C, kNone, 0},
    {"window.properties", "Show properties", "Open the properties panel for the selection.", "Window", "settings2", fn(4), 0, kNone, 0},
    {"window.fullscreen", "Full screen", "Hide the interface and use the whole display.", "Window", "maximize", fn(11), 0, kNone, 0},
    {"window.next", "Next tab", "Switch to the tab on the right.", "Window", "chevron-right", Key::Tab, C, kNone, 0},
    {"window.prev", "Previous tab", "Switch to the tab on the left.", "Window", "chevron-left", Key::Tab, C | S, kNone, 0},
    {"help.manual", "Open the manual", "Show the user manual in the browser.", "Help", "circle", fn(1), 0, kNone, 0},
    {"help.shortcuts", "Keyboard shortcuts", "Show this list of keyboard shortcuts.", "Help", "circle", fn(1), C, kNone, 0},
    {"help.about", "About", "Show the program version and credits.", "Help", "circle", kNone, 0, kNone, 0},
    {"debug.stats", "Show statistics", "Overlay frame time and memory use on the canvas.", "Debug", "circle", fn(3), 0, kNone, 0},
    {"debug.reload", "Reload theme", "Read the theme files again without restarting.", "Debug", "rotate-cw", fn(5), C, kNone, 0},
};

cmd::ChordSequence chordOf(Key key, uint8_t mods) { return key == kNone ? cmd::ChordSequence{} : cmd::ChordSequence::single({key, mods, false}); }

void fillRegistry(Sample& s) {
  s.registry.addContext("layers", cmd::kWindowContext, false, "The layers panel");
  for (const Row& r : kRows) {
    cmd::CommandDef def;
    def.id = r.id;
    def.label = r.label;
    def.description = r.description;
    def.category = r.category;
    def.icon = r.icon;
    def.defaultChords = {chordOf(r.key, r.mods), chordOf(r.altKey, r.altMods)};
    def.execute = [](const cmd::ExecuteArgs&) { return cmd::ExecuteResult::handled(); };
    s.registry.add(std::move(def));
  }
  // A command of the layers panel context: its Delete coexists with the global Delete (keep both).
  cmd::CommandDef del;
  del.id = "layer.delete";
  del.label = "Delete layer";
  del.description = "Remove the layer when the layers panel has the focus.";
  del.category = "Layers";
  del.icon = "trash-2";
  del.context = "layers";
  del.defaultChords = {cmd::ChordSequence::single({Key::Delete, 0, false}), {}};
  del.execute = [](const cmd::ExecuteArgs&) { return cmd::ExecuteResult::handled(); };
  s.registry.add(std::move(del));
  // Two commands without a chord: assign one of the used chords to try the Replace choice.
  for (const char* id : {"misc.alpha", "misc.beta"}) {
    cmd::CommandDef def;
    def.id = id;
    def.label = std::string(id) == "misc.alpha" ? "Alpha action" : "Beta action";
    def.description = "A sample action without a shortcut; assign one to try the conflict dialog.";
    def.category = "Debug";
    def.icon = "circle";
    def.execute = [](const cmd::ExecuteArgs&) { return cmd::ExecuteResult::handled(); };
    s.registry.add(std::move(def));
  }
}

class HotkeysPage final : public WidgetObject {
 public:
  const char* typeName() const override { return "GalleryHotkeys"; }
  void onAttached() override {
    style().direction = layout::FlexDirection::Column;
    style().gapRow = 8.0;
    style().alignItems = layout::Align::Stretch;
    style().flexGrow = 1.0;
    style().minHeight = layout::Length::px(0);
    sample_ = std::make_unique<Sample>(ui());
    fillRegistry(*sample_);
    HotkeyEditor& editor = ui().create<HotkeyEditor>(id(), sample_->services());
    editor.style().flexGrow = 1.0;
    editor.style().minHeight = layout::Length::px(420);
  }

 private:
  std::unique_ptr<Sample> sample_;
};

// A stand-in for a menu creator: accepts any action drag and lists the ids it received.
class DropZone final : public WidgetObject, public DragTarget {
 public:
  explicit DropZone(DragHub& hub) : hub_(hub) {}
  const char* typeName() const override { return "GalleryDropZone"; }
  void onAttached() override {
    style().flexGrow = 1.0;
    style().minWidth = layout::Length::px(220);
    hub_.addTarget(id(), this);
  }
  void onDetached() override { hub_.removeTarget(id()); }
  bool dragOver(const DragPayload& payload, double, double) override {
    over_ = payload.kind == DragPayload::Kind::Command && !payload.commandId.empty();
    requestPaint();
    return over_;
  }
  void dragLeave() override {
    over_ = false;
    requestPaint();
  }
  bool dragDrop(const DragPayload& payload, double, double) override {
    over_ = false;
    if (payload.kind != DragPayload::Kind::Command || payload.commandId.empty()) return false;
    dropped_.push_back(payload.text + "  (" + payload.commandId + ")");
    if (dropped_.size() > 12) dropped_.erase(dropped_.begin());
    requestPaint();
    return true;
  }
  void paint(PaintContext& ctx) override {
    const float radius = ctx.px(8.0);
    ctx.painter().fillRoundedRect(ctx.box(), render::CornerRadii::uniform(radius), ctx.color(over_ ? "hover" : "panel"));
    ctx.painter().border(ctx.box(), render::CornerRadii::uniform(radius), ctx.hairline(), ctx.color(over_ ? "accent" : "border"));
    const theme::TextStyle body = ctx.style("label.body").text;
    const theme::TextStyle muted = ctx.style("label.muted").text;
    const layout::Rect r = ctx.rect();
    TextOptions o;
    o.padLeft = 12.0;
    ctx.drawText("Drop actions here", body, ctx.toPhysical(r.x, r.y + 8, r.w, 24), o);
    double y = r.y + 40;
    if (dropped_.empty()) ctx.drawText("Drag a row of the list onto this box.", muted, ctx.toPhysical(r.x, y, r.w, 22), o);
    for (const std::string& line : dropped_) {
      ctx.drawText(line, muted, ctx.toPhysical(r.x, y, r.w, 22), o);
      y += 22;
    }
  }
  const std::vector<std::string>& dropped() const { return dropped_; }

 private:
  DragHub& hub_;
  bool over_ = false;
  std::vector<std::string> dropped_;
};

class ActionsPage final : public WidgetObject {
 public:
  const char* typeName() const override { return "GalleryActions"; }
  void onAttached() override {
    style().direction = layout::FlexDirection::Row;
    style().gapColumn = 12.0;
    style().alignItems = layout::Align::Stretch;
    style().flexGrow = 1.0;
    style().minHeight = layout::Length::px(0);
    sample_ = std::make_unique<Sample>(ui());
    fillRegistry(*sample_);
    hub_ = std::make_unique<DragHub>(ui());
    ActionListOptions options;
    options.shortcutHeader = "Shortcut";
    options.columnHeaders = true;
    ActionList& list = ui().create<ActionList>(id(), options);
    list.style().flexGrow = 2.0;
    list.style().flexBasis = layout::Length::px(0);
    list.style().minWidth = layout::Length::px(420);
    list.style().minHeight = layout::Length::px(300);
    list.bindRegistry(&sample_->registry, &sample_->keymap, {false, true, true});
    list.setDragHub(hub_.get());
    ui().create<DropZone>(id(), *hub_);
  }

 private:
  std::unique_ptr<Sample> sample_;
  std::unique_ptr<DragHub> hub_;
};

}  // namespace

void buildGalleryHotkeys(UiContext& ui, WidgetId parent) { ui.create<HotkeysPage>(parent); }
void buildGalleryActions(UiContext& ui, WidgetId parent) { ui.create<ActionsPage>(parent); }

}  // namespace r1ui::widgets
