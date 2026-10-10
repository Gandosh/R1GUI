// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery page of the menu creator (GalleryCreator.h).
// Invariants: the page owns every object the window points at (members are declared in construction order,
//   so the registry outlives the router and the window); the sample commands only count their runs and end
//   with registry.touch(); file work goes through error-returning calls and never throws.
// Callers: the gallery preview, the group's gallery tests.
#include "r1ui/widgets/custommenu/creator/GalleryCreator.h"

#include <filesystem>
#include <memory>
#include <string>

#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/CommandRouter.h"
#include "r1ui/commands/Keymap.h"
#include "r1ui/commands/Overrides.h"
#include "r1ui/commands/custommenu/CustomMenuIo.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/custommenu/creator/CreateCustomMenuWindow.h"
#include "r1ui/widgets/filepath/FilePathDialog.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

namespace cm = commands::custommenu;
namespace fs = std::filesystem;
namespace layout = core::layout;
using core::tree::WidgetId;
using theme::State::kNone;
using theme::StyleProperty;

constexpr theme::StyleRuleEntry kRows[] = {
    {"creator.gallery.frame", kNone, StyleProperty::Background, "color:panel"},
    {"creator.gallery.frame", kNone, StyleProperty::BorderColor, "color:border"},
    {"creator.gallery.frame", kNone, StyleProperty::BorderWidth, "number:1"},
    {"creator.gallery.frame", kNone, StyleProperty::Radius, "radius:md"},
};

class Frame final : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows() { return kRows; }
  const char* typeName() const override { return "GalleryCreatorFrame"; }
  void paint(PaintContext& ctx) override { ctx.fillBox(ctx.style("creator.gallery.frame")); }
};

struct Sample {
  explicit Sample(UiContext& ui) : clock(ui), router(registry, keymap, clock), session(set) {}
  commands::CommandRegistry registry;
  commands::KeybindingOverrides overrides{registry};
  commands::Keymap keymap{registry, overrides};
  UiClock clock;
  commands::CommandRouter router;
  cm::CustomMenuSet set;
  CreatorSession session;
  std::string last = "Pick a type, drag actions from the list onto the preview, name the menu and press Create.";
};

class GalleryCreatorPage final : public WidgetObject {
 public:
  const char* typeName() const override { return "GalleryCreator"; }

  void onAttached() override {
    sample_ = std::make_unique<Sample>(ui());
    Sample& s = *sample_;
    declareCommands(s);
    const std::string pie = s.set.createMenu(cm::MenuKind::Pie, "Tools").id;
    s.set.setSlot(pie, 0, "tool.select");
    s.set.setSlot(pie, 2, "tool.move");
    s.set.createMenu(cm::MenuKind::Panel, "Quick tools");

    style().direction = layout::FlexDirection::Column;
    style().gapRow = 12.0;
    style().flexShrink = 0.0;
    for (double& p : style().padding) p = 12.0;
    ui().create<Label>(id(), "Create Custom Menu", LabelRole::Heading);
    ui().create<Label>(id(), "The window the preview opens from Custom Menus > Create Custom Menu... It edits a copy; Create commits it, Cancel throws it away.", LabelRole::Muted);

    Frame& frame = ui().create<Frame>(id());
    frame.style().width = layout::Length::px(1060.0);
    frame.style().height = layout::Length::px(680.0);
    frame.style().flexShrink = 0.0;
    frame_ = frame.id();
    buildWindow();
    status_ = ui().create<Label>(id(), s.last, LabelRole::Body).id();
    ui().create<Label>(id(), "Existing menus (names must differ): Tools, Quick tools", LabelRole::Muted);
  }

 private:
  void say(std::string text) {
    sample_->last = std::move(text);
    if (Label* label = ui().objectAs<Label>(status_)) label->setText(sample_->last);
  }

  void declare(Sample& s, const std::string& id, const std::string& label, const std::string& icon, const std::string& description) {
    commands::CommandDef def;
    def.id = id;
    def.label = label;
    def.description = description;
    def.icon = icon;
    def.category = id.substr(0, id.find('.'));
    Sample* sample = &s;
    def.execute = [sample, label](const commands::ExecuteArgs&) {
      sample->last = "Ran: " + label;
      return commands::ExecuteResult::handled();
    };
    s.registry.add(std::move(def));
  }

  void declareCommands(Sample& s) {
    declare(s, "tool.select", "Select", "mouse-pointer", "Click objects to select them");
    declare(s, "tool.move", "Move", "move-3d", "Drag objects in the viewport");
    declare(s, "tool.rotate", "Rotate", "rotate-cw", "Rotate the selection");
    declare(s, "tool.scale", "Scale", "maximize", "Scale the selection");
    declare(s, "edit.undo", "Undo", "undo2", "Undo the last change");
    declare(s, "edit.redo", "Redo", "redo2", "Redo the change that was undone");
    declare(s, "edit.copy", "Copy", "copy", "Copy the selection to the clipboard");
    declare(s, "edit.cut", "Cut", "scissors", "Cut the selection to the clipboard");
    declare(s, "view.grid", "Show grid", "grid-3x3", "Show or hide the grid in the viewport");
    declare(s, "view.frame", "Frame selection", "scan", "Bring the selection into view");
    declare(s, "view.wireframe", "Wireframe", "box", "Draw objects as outlines");
    declare(s, "file.save", "Save", "save", "Save the document");
    declare(s, "file.open", "Open", "folder-open", "Open a document");
  }

  void buildWindow() {
    Sample& s = *sample_;
    CreatorHooks hooks;
    hooks.committed = [this](const std::string& menuId, bool edited) {
      const cm::CustomMenu* menu = sample_->set.find(menuId);
      say(std::string(edited ? "Saved " : "Created ") + (menu != nullptr ? "\"" + menu->name + "\"" : menuId) + ". The sample set now holds " + std::to_string(sample_->set.size()) + " menus.");
    };
    hooks.cancelled = [this] { say("Cancelled: nothing was created."); };
    hooks.saveFile = [this](UiContext& context, WidgetId owner, const cm::CustomMenu& menu) {
      FilePathOptions o;
      o.mode = FilePathMode::Save;
      o.title = "Save custom menu";
      o.description = "Write this menu to a .r1mn file.";
      o.folder = fs::temp_directory_path() / "r1gui-gallery-menus";
      o.extension = cm::kMenuFileExtension;
      o.initialName = menu.name;
      o.owner = owner;
      openFilePathDialog(context, o, [this, menu](const fs::path& path) {
        std::string error;
        say(cm::saveMenuFile(menu, path, error) ? "Saved " + path.string() : "Could not save: " + error);
      });
    };
    hooks.loadFile = [this](UiContext& context, WidgetId owner) {
      FilePathOptions o;
      o.mode = FilePathMode::Open;
      o.title = "Load custom menu";
      o.description = "Start from the menu in a .r1mn file.";
      o.folder = fs::temp_directory_path() / "r1gui-gallery-menus";
      o.extension = cm::kMenuFileExtension;
      o.owner = owner;
      openFilePathDialog(context, o, [this](const fs::path& path) {
        const cm::MenuParseResult parsed = cm::loadMenuFile(path);
        if (!parsed.ok) {
          say("Could not load: " + parsed.error);
          return;
        }
        say(sample_->session.beginFromFile(parsed.menu) ? "Loaded " + path.string() : "That menu cannot be edited here.");
      });
    };
    CreateCustomMenuWindow& window = ui().create<CreateCustomMenuWindow>(frame_, CommandServices{s.registry, s.overrides, s.keymap, s.router}, s.session, std::move(hooks));
    window.style().width = layout::Length::percent(100.0);
    window.style().height = layout::Length::percent(100.0);
  }

  std::unique_ptr<Sample> sample_;
  WidgetId frame_;
  WidgetId status_;
};

}  // namespace

void buildGalleryCreator(UiContext& ui, WidgetId parent) { ui.create<GalleryCreatorPage>(parent); }

}  // namespace r1ui::widgets
