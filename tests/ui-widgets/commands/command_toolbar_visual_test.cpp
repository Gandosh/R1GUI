// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the visual oracle of a toolbar bound to commands: the tool bar of the reference capture
//   (select tool, two flyout groups, pen, text, hand, a separator, three actions with a status dot) is
//   declared as commands and built with bindCommandToolbar; the result is compared with the same
//   regions of the same reference screens as the hand-built Toolbar (toolbar_visual_test): the whole
//   bar idle and with the rectangle group hovered, the first button idle and hover, and the theme
//   toggle idle and hover. It also renders the hand-built and the command-built bar and requires the
//   two images to be pixel-identical.
// Callers: CTest (label gpu, offscreen, no window).
// Documented differences are those of toolbar_visual_test (half-pixel offset of the capture, the page
//   colour outside the rounded surface, the tooltip over the hovered button).
#include "../g3support/RegionCompare.h"
#include "r1ui/widgets/commands/CommandToolbar.h"
#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/toolbar/Toolbar.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;
namespace cmd = r1ui::commands;
using r1ui::theme::ThemeId;
using r1test::visual::RegionSpec;

// The command objects of one render, owned by a (display none) widget of the scene so they live as long
// as the UiContext they refresh.
struct Bundle {
  explicit Bundle(UiContext& ui) : clock(ui), router(registry, keymap, clock), sync(ui, services()) {}
  CommandServices services() { return {registry, overrides, keymap, router}; }

  void declare(const char* id, const char* label, const char* icon, cmd::CommandKind kind, char key, bool checked = false) {
    cmd::CommandDef def;
    def.id = id;
    def.label = label;
    def.icon = icon;
    def.kind = kind;
    if (kind == cmd::CommandKind::Radio) def.radioGroup = "tools";
    if (key != 0) def.defaultChords[0] = cmd::ChordSequence::single({static_cast<cmd::Key>(key), 0, false});
    def.checked = [checked] { return checked; };
    def.execute = [](const cmd::ExecuteArgs&) { return cmd::ExecuteResult::handled(); };
    R1_EXPECT(registry.add(std::move(def)).ok);
  }

  cmd::CommandRegistry registry;
  cmd::KeybindingOverrides overrides{registry};
  cmd::Keymap keymap{registry, overrides};
  UiClock clock;
  cmd::CommandRouter router;
  CommandUiSync sync;
  std::unique_ptr<CommandToolbarBinding> binding;
};

class BundleHolder final : public WidgetObject {
 public:
  const char* typeName() const override { return "BundleHolder"; }
  void onAttached() override {
    style().display = layout::Display::None;
    bundle = std::make_unique<Bundle>(ui());
  }
  std::unique_ptr<Bundle> bundle;
};

// Declares the capture's tools as commands. `V P T H` etc. are the chords the reference tooltips show.
Bundle& declareTools(UiContext& ui, WidgetId parent) {
  Bundle& b = *ui.create<BundleHolder>(parent).bundle;
  using K = cmd::CommandKind;
  b.declare("tool.select", "Select", "mouse-pointer", K::Radio, 'V', true);
  b.declare("tool.frame", "Frame", "frame", K::Radio, 'F');
  b.declare("tool.section", "Section", "layout-grid", K::Radio, 'S');
  b.declare("tool.rect", "Rectangle", "square", K::Radio, 'R');
  b.declare("tool.ellipse", "Ellipse", "circle", K::Radio, 'O');
  b.declare("tool.pen", "Pen", "pen-tool", K::Radio, 'P');
  b.declare("tool.text", "Text", "type", K::Radio, 'T');
  b.declare("tool.hand", "Hand", "hand", K::Radio, 'H');
  b.declare("app.components", "Components", "blocks", K::Action, 0);
  b.declare("app.grid", "Grid", "grid-3x3", K::Action, 0);
  b.declare("app.settings", "Frame selection", "server-cog", K::Action, 0);
  return b;
}

// The bar at (left, top); `hoverButton` >= 0 puts the pointer over that button.
r1test::visual::Build barAt(int left, int top, int hoverButton) {
  return [=](UiContext& ui, WidgetId parent) {
    ui.rootStyle().alignItems = layout::Align::Start;
    Bundle& b = declareTools(ui, parent);
    Toolbar& bar = ui.create<Toolbar>(parent);
    bar.style().margin[layout::kLeft] = layout::Length::px(left);
    bar.style().margin[layout::kTop] = layout::Length::px(top);
    using I = CommandToolbarItem;
    b.binding = bindCommandToolbar(ui, bar, b.sync,
                                   {I::command("tool.select"), I::group({"tool.frame", "tool.section"}), I::group({"tool.rect", "tool.ellipse"}), I::command("tool.pen"), I::command("tool.text"),
                                    I::command("tool.hand"), I::separator(), I::command("app.components"), I::command("app.grid"), I::command("app.settings")});
    b.binding->button("app.settings")->setBadge(true);
    if (hoverButton >= 0) {
      ui.frame();
      const layout::Rect r = ui.absRect(bar.button(static_cast<size_t>(hoverButton))->id());
      ui.pointerMove(r.x + r.w / 2.0, r.y + r.h / 2.0);
    }
    return bar.id();
  };
}

// The hand-built twin (the code of toolbar_visual_test).
r1test::visual::Build handBar() {
  return [](UiContext& ui, WidgetId parent) {
    ui.rootStyle().alignItems = layout::Align::Start;
    Toolbar& bar = ui.create<Toolbar>(parent);
    bar.addTool("select", "mouse-pointer", "Select (V)");
    bar.addToolGroup({{"frame", "frame", "Frame", "F"}, {"section", "layout-grid", "Section", "S"}});
    bar.addToolGroup({{"rect", "square", "Rectangle", "R"}, {"ellipse", "circle", "Ellipse", "O"}});
    bar.addTool("pen", "pen-tool", "Pen (P)");
    bar.addTool("text", "type", "Text (T)");
    bar.addTool("hand", "hand", "Hand (H)");
    bar.addSeparator();
    bar.addAction("components", "blocks", "Components", [](ToolbarButton&) {});
    bar.addAction("grid", "grid-3x3", "Grid", [](ToolbarButton&) {});
    bar.addAction("settings", "server-cog", "Frame selection", [](ToolbarButton&) {}).setBadge(true);
    bar.setActiveTool("select");
    return bar.id();
  };
}

// The capture's theme toggle: on in the dark theme, off in the light one.
r1test::visual::Build themeToggleAt(int left, int top, bool hover, bool on) {
  return [=](UiContext& ui, WidgetId parent) {
    ui.rootStyle().alignItems = layout::Align::Start;
    Bundle& b = *ui.create<BundleHolder>(parent).bundle;
    cmd::CommandDef def;
    def.id = "app.theme";
    def.label = "Dark theme";
    def.icon = "moon";
    def.kind = cmd::CommandKind::Toggle;
    def.checked = [on] { return on; };
    def.execute = [](const cmd::ExecuteArgs&) { return cmd::ExecuteResult::handled(); };
    R1_EXPECT(b.registry.add(std::move(def)).ok);
    Toolbar& bar = ui.create<Toolbar>(parent);
    bar.style().margin[layout::kLeft] = layout::Length::px(left);
    bar.style().margin[layout::kTop] = layout::Length::px(top);
    b.binding = bindCommandToolbar(ui, bar, b.sync, {CommandToolbarItem::command("app.theme")});
    if (hover) {
      ui.frame();
      const layout::Rect r = ui.absRect(b.binding->button("app.theme")->id());
      ui.pointerMove(r.x + r.w / 2.0, r.y + r.h / 2.0);
    }
    return bar.id();
  };
}

const std::vector<r1test::visual::VisualSpec::Ignore> kHalfPixelEdges = {{5, 6, 2, 32}, {37, 6, 2, 32}};

}  // namespace

int main() {
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    R1_EXPECT_MATCHES_REGION(barAt(0, 0, -1), (RegionSpec{.reference = "screen-toolbar-flyout-open", .x = 548, .y = 842, .w = 344, .h = 42, .theme = theme, .profile = "text", .tag = "cmd-toolbar-bar-idle", .luminance = true, .clip = {0, 0, 344, 42, 12}}));
    R1_EXPECT_MATCHES_REGION(barAt(0, 0, 2), (RegionSpec{.reference = "screen-toolbar-tooltip", .x = 548, .y = 842, .w = 344, .h = 42, .theme = theme, .profile = "text", .tag = "cmd-toolbar-bar-hover-rect", .ignore = {{60, 0, 80, 3}}, .luminance = true, .clip = {0, 0, 344, 42, 12}}));
    R1_EXPECT_MATCHES_REGION(barAt(1, 1, -1), (RegionSpec{.reference = "widget-toolbar-button-idle", .x = 0, .y = 0, .w = 44, .h = 44, .theme = theme, .profile = "text", .tag = "cmd-toolbar-button-idle", .ignore = kHalfPixelEdges, .luminance = true, .clip = {1, 1, 42, 42, 12}}));
    R1_EXPECT_MATCHES_REGION(barAt(1, 1, 0), (RegionSpec{.reference = "widget-toolbar-button-hover", .x = 0, .y = 0, .w = 44, .h = 44, .theme = theme, .profile = "text", .tag = "cmd-toolbar-button-hover", .ignore = kHalfPixelEdges, .luminance = true, .clip = {1, 1, 42, 42, 12}}));
    R1_EXPECT_MATCHES_REGION(themeToggleAt(1, 1, false, theme == ThemeId::Dark), (RegionSpec{.reference = "widget-toolbar-button-active-theme-idle", .x = 0, .y = 0, .w = 44, .h = 44, .theme = theme, .profile = "text", .tag = "cmd-toolbar-toggle-idle", .luminance = true, .clip = {1, 1, 42, 42, 12}}));
    R1_EXPECT_MATCHES_REGION(themeToggleAt(1, 1, true, theme == ThemeId::Dark), (RegionSpec{.reference = "widget-toolbar-button-active-theme-hover", .x = 0, .y = 0, .w = 44, .h = 44, .theme = theme, .profile = "text", .tag = "cmd-toolbar-toggle-hover", .luminance = true, .clip = {1, 1, 42, 42, 12}}));

    // The command-built bar is pixel-identical to the hand-built one (same layout, same icons, same state).
    r1ui::widgets::testing::RenderSpec spec;
    spec.width = 344;
    spec.height = 42;
    spec.theme = theme;
    spec.padding = 0;
    const auto paths = r1test::visual::paths();
    const auto hand = r1ui::widgets::testing::renderWidget(handBar(), spec, paths);
    const auto built = r1ui::widgets::testing::renderWidget(barAt(0, 0, -1), spec, paths);
    const bool same = hand.width == built.width && hand.height == built.height && hand.rgba == built.rgba;
    std::printf("visual cmd-toolbar-vs-hand %-5s %s\n", theme == ThemeId::Light ? "light" : "dark", same ? "identical" : "DIFFERENT");
    R1_EXPECT(same);
  }
  R1_EXPECT(r1ui::widgets::testing::validationMessageCount() == 0);
  return r1test::finish();
}
