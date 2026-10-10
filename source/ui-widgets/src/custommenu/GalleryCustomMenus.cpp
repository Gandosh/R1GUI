// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery page of the custom menu group (GalleryCustomMenus.h).
// Invariants: only layout boxes, Labels, the menu bar, the pie and panel widgets are created; the sample
//   commands only touch the page's own state and end with registry.touch(); the page owns every object it
//   points at (members are declared in construction order, so the registry outlives its listeners).
// Callers: the gallery preview, the group's gallery tests.
#include "r1ui/widgets/custommenu/GalleryCustomMenus.h"

#include <memory>
#include <string>
#include <vector>

#include "r1ui/commands/custommenu/CustomMenuSet.h"
#include "r1ui/widgets/commands/CommandMenus.h"
#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/custommenu/CustomMenuCommands.h"
#include "r1ui/widgets/custommenu/CustomMenuPanel.h"
#include "r1ui/widgets/custommenu/CustomMenusMenu.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/pie/PieMenu.h"
#include "r1ui/widgets/pie/PieTrigger.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

namespace cm = commands::custommenu;
namespace cmd = commands;
namespace layout = core::layout;
using core::tree::WidgetId;
using theme::State::kNone;
using theme::StyleProperty;

constexpr theme::StyleRuleEntry kRows[] = {
    {"custommenu.gallery.box", kNone, StyleProperty::Background, "color:panel-secondary"},
    {"custommenu.gallery.box", kNone, StyleProperty::BorderColor, "color:border"},
    {"custommenu.gallery.box", kNone, StyleProperty::BorderWidth, "number:1"},
    {"custommenu.gallery.box", kNone, StyleProperty::Radius, "radius:md"},
};

// A plain flex container; `framed` boxes draw the panel background and a border (the dock body look).
class GalleryBox final : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows() { return kRows; }
  explicit GalleryBox(bool framed = false) : framed_(framed) {}
  const char* typeName() const override { return "GalleryCustomMenusBox"; }
  void paint(PaintContext& ctx) override {
    if (framed_) ctx.fillBox(ctx.style("custommenu.gallery.box"));
  }

 private:
  bool framed_;
};

GalleryBox& column(UiContext& ui, WidgetId parent, double gap) {
  GalleryBox& box = ui.create<GalleryBox>(parent);
  box.style().direction = layout::FlexDirection::Column;
  box.style().gapRow = gap;
  box.style().flexShrink = 0.0;
  return box;
}

GalleryBox& wrapRow(UiContext& ui, WidgetId parent, double gap) {
  GalleryBox& box = ui.create<GalleryBox>(parent);
  box.style().direction = layout::FlexDirection::Row;
  box.style().wrap = layout::FlexWrap::Wrap;
  box.style().gapColumn = gap;
  box.style().gapRow = gap;
  box.style().alignItems = layout::Align::Start;
  box.style().flexShrink = 0.0;
  return box;
}

// Everything the page owns; member order is construction order.
struct Sample {
  explicit Sample(UiContext& ui) : clock(ui), router(registry, keymap, clock), sync(ui, services()) {}
  CommandServices services() { return {registry, overrides, keymap, router}; }

  cmd::CommandRegistry registry;
  cmd::KeybindingOverrides overrides{registry};
  cmd::Keymap keymap{registry, overrides};
  UiClock clock;
  cmd::CommandRouter router;
  CommandUiSync sync;
  cm::CustomMenuSet set;
  std::unique_ptr<CustomMenuCommands> menuCommands;

  std::string tool = "tool.select";
  bool grid = true;
  std::string last = "Hold the right mouse button in the box above to open the pie menu";
};

class GalleryCustomMenusPage final : public WidgetObject {
 public:
  const char* typeName() const override { return "GalleryCustomMenus"; }

  void onAttached() override {
    sample_ = std::make_unique<Sample>(ui());
    Sample& s = *sample_;
    declareCommands(s);
    buildMenus(s);
    CustomMenuHooks hooks;
    hooks.openPanel = [this](const std::string& menuId) { say("Open or focus the panel of " + menuId); };
    hooks.edit = [this](const std::string& menuId) { say("Edit " + menuId + " (the creator window is a later slice)"); };
    hooks.save = [this](const std::string& menuId) { say("Save " + menuId + " to a .r1mn file"); };
    hooks.create = [this] { say("Create a custom menu"); };
    hooks.load = [this] { say("Load a .r1mn file"); };
    s.menuCommands = std::make_unique<CustomMenuCommands>(s.registry, s.set, std::move(hooks));

    style().direction = layout::FlexDirection::Column;
    style().gapRow = 16.0;
    style().flexShrink = 0.0;
    style().padding[layout::kLeft] = style().padding[layout::kRight] = style().padding[layout::kTop] = style().padding[layout::kBottom] = 12.0;

    ui().create<Label>(id(), "Custom menus", LabelRole::Title);
    ui().create<Label>(id(), "Pie menus open while the right mouse button is held; dockable menus are panels of action buttons. Both are made of ordinary commands.", LabelRole::Muted);

    buildPies(s);
    buildPanels(s);
    buildLive(s);
    refreshAttachment_ = s.sync.attach([this] { refreshStatus(); });
    refreshStatus();
  }

  void onDetached() override { refreshAttachment_.reset(); }

 private:
  void say(std::string text) {
    sample_->last = std::move(text);
    sample_->registry.touch();
  }

  void declare(Sample& s, const std::string& id, const std::string& label, const std::string& icon, cmd::CommandKind kind, std::function<bool()> enabled = {}) {
    cmd::CommandDef def;
    def.id = id;
    def.label = label;
    def.description = label + " (gallery command)";
    def.icon = icon;
    def.category = "Gallery";
    def.kind = kind;
    if (kind == cmd::CommandKind::Radio) def.radioGroup = "tools";
    def.enabled = std::move(enabled);
    Sample* sample = &s;
    if (kind == cmd::CommandKind::Radio) {
      def.checked = [sample, id] { return sample->tool == id; };
    } else if (kind == cmd::CommandKind::Toggle) {
      def.checked = [sample] { return sample->grid; };
    }
    def.execute = [this, sample, id, label, kind](const cmd::ExecuteArgs&) {
      if (kind == cmd::CommandKind::Radio) sample->tool = id;
      if (kind == cmd::CommandKind::Toggle) sample->grid = !sample->grid;
      say("Ran: " + label);
      return cmd::ExecuteResult::handled();
    };
    s.registry.add(std::move(def));
  }

  void declareCommands(Sample& s) {
    using K = cmd::CommandKind;
    declare(s, "tool.select", "Select", "mouse-pointer", K::Radio);
    declare(s, "tool.move", "Move", "move-3d", K::Radio);
    declare(s, "tool.rotate", "Rotate", "rotate-cw", K::Radio);
    declare(s, "tool.pen", "Pen", "pen-tool", K::Radio);
    declare(s, "tool.hand", "Hand", "hand", K::Radio);
    declare(s, "edit.undo", "Undo", "undo2", K::Action);
    declare(s, "edit.redo", "Redo", "redo2", K::Action, [] { return false; });
    declare(s, "edit.copy", "Copy", "copy", K::Action);
    declare(s, "edit.cut", "Cut", "scissors", K::Action);
    declare(s, "view.grid", "Show grid", "grid-3x3", K::Toggle);
    declare(s, "view.zoomIn", "Zoom in", "zoom-in", K::Action);
    declare(s, "file.save", "Save", "save", K::Action);
    declare(s, "file.open", "Open", "folder-open", K::Action);
  }

  void buildMenus(Sample& s) {
    cm::CustomMenuSet& set = s.set;
    const std::string tools = set.createMenu(cm::MenuKind::Pie, "Tools").id;
    set.setSlot(tools, 0, "tool.select");
    set.setSlot(tools, 1, "tool.move");
    set.setSlot(tools, 2, "tool.rotate");
    set.setSlot(tools, 3, "view.zoomIn");
    set.setSlot(tools, 4, "edit.undo");
    set.setSlot(tools, 5, "edit.redo");  // disabled: drawn dim and never chosen
    set.setSlot(tools, 7, "tool.pen");   // slot 6 stays empty
    toolsPie_ = tools;

    const std::string edit = set.createMenu(cm::MenuKind::Pie, "Edit").id;
    set.setPieSlotCount(edit, 6);
    set.setSlot(edit, 0, "edit.copy");
    set.setSlot(edit, 1, "edit.cut");
    set.setSlot(edit, 3, "plugin.removed", "Old plugin");  // missing command
    set.setSlot(edit, 4, "edit.undo");

    const std::string compass = set.createMenu(cm::MenuKind::Pie, "Four").id;
    set.setPieSlotCount(compass, 4);
    set.setSlot(compass, 0, "file.open");
    set.setSlot(compass, 1, "file.save");
    set.setSlot(compass, 2, "edit.copy", "Duplicate");

    const std::string quick = set.createMenu(cm::MenuKind::Panel, "Quick tools").id;
    for (const char* c : {"tool.select", "tool.move", "tool.rotate", "tool.pen", "tool.hand", "view.grid", "edit.undo", "edit.redo", "plugin.removed"}) set.addEntry(quick, c);
    set.setPanelColumns(quick, 3);

    const std::string icons = set.createMenu(cm::MenuKind::Panel, "Icons only").id;
    for (const char* c : {"file.open", "file.save", "edit.undo", "edit.copy", "edit.cut", "view.zoomIn", "view.grid", "tool.hand"}) set.addEntry(icons, c);
    set.setPanelColumns(icons, 4);
    set.setPanelButtonSize(icons, 36);
    set.setPanelShowLabels(icons, false);

    const std::string tall = set.createMenu(cm::MenuKind::Panel, "Big buttons").id;
    for (const char* c : {"tool.move", "tool.rotate", "view.grid"}) set.addEntry(tall, c);
    set.setPanelColumns(tall, 2);
    set.setPanelButtonSize(tall, 64);
  }

  void buildPies(Sample& s) {
    ui().create<Label>(id(), "Pie menus", LabelRole::Heading);
    GalleryBox& row = wrapRow(ui(), id(), 16.0);
    int shown = 0;
    for (const cm::CustomMenu& menu : s.set.menus()) {
      if (menu.kind != cm::MenuKind::Pie) continue;
      GalleryBox& cell = column(ui(), row.id(), 4.0);
      ui().create<Label>(cell.id(), menu.name + " (" + std::to_string(menu.slotCount) + " slots)" + (shown == 0 ? ", slot 2 highlighted" : ""), LabelRole::Caption);
      PieMenu& pie = ui().create<PieMenu>(cell.id(), pieSlotViews(s.services(), menu));
      if (shown == 0) pie.setHighlight(1);
      ++shown;
    }
  }

  void buildPanels(Sample& s) {
    ui().create<Label>(id(), "Dockable menus", LabelRole::Heading);
    GalleryBox& row = wrapRow(ui(), id(), 16.0);
    for (const cm::CustomMenu& menu : s.set.menus()) {
      if (menu.kind != cm::MenuKind::Panel) continue;
      GalleryBox& cell = column(ui(), row.id(), 4.0);
      ui().create<Label>(cell.id(), menu.name, LabelRole::Caption);
      GalleryBox& frame = ui().create<GalleryBox>(cell.id(), true);
      frame.style().width = layout::Length::px(300);
      frame.style().height = layout::Length::px(menu.panel.buttonSize >= 56 ? 170 : 200);
      frame.style().flexShrink = 0.0;
      ui().create<CustomMenuPanel>(frame.id(), s.services(), s.sync, s.set, menu.id);
    }
  }

  void buildLive(Sample& s) {
    ui().create<Label>(id(), "Custom Menus main menu and the live pie", LabelRole::Heading);
    GalleryBox& row = wrapRow(ui(), id(), 16.0);
    GalleryBox& left = column(ui(), row.id(), 8.0);
    MenuBar& bar = ui().create<MenuBar>(left.id());
    bindCommandMenuBar(bar, s.services(), {customMenusMenuTitle(s.set)});

    GalleryBox& frame = ui().create<GalleryBox>(row.id(), true);
    frame.style().width = layout::Length::px(420);
    frame.style().height = layout::Length::px(150);
    frame.style().flexShrink = 0.0;
    Sample* sample = &s;
    const std::string toolsId = toolsPie_;
    PieTrigger& trigger = ui().create<PieTrigger>(frame.id(), s.services(), [sample, toolsId](double, double) { return sample->set.find(toolsId) ? std::optional<cm::CustomMenu>(*sample->set.find(toolsId)) : std::nullopt; });
    trigger.style().justifyContent = layout::Justify::Center;
    trigger.style().alignItems = layout::Align::Center;
    trigger.style().padding[layout::kLeft] = trigger.style().padding[layout::kRight] = 12.0;
    ui().create<Label>(trigger.id(), "Hold the right mouse button here", LabelRole::Muted).setAlign(TextAlign::Center);
    trigger.setOnFallback([this](double, double) { say("A quick right click would open the normal context menu"); });
    trigger.setOnCancelled([this] { say("Pie cancelled"); });
    status_ = ui().create<Label>(id(), "", LabelRole::Body).id();
  }

  void refreshStatus() {
    if (!sample_) return;
    if (Label* label = ui().objectAs<Label>(status_)) label->setText(sample_->last + "   |   Tool: " + sample_->tool.substr(5) + "   Grid: " + (sample_->grid ? "on" : "off"));
  }

  std::unique_ptr<Sample> sample_;
  CommandUiSync::Attachment refreshAttachment_;
  std::string toolsPie_;
  WidgetId status_;
};

}  // namespace

void buildGalleryCustomMenus(UiContext& ui, WidgetId parent) { ui.create<GalleryCustomMenusPage>(parent); }

}  // namespace r1ui::widgets
