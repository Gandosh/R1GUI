// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the GPU render of one dockable menu panel in both themes (PNGs under R1UI_ARTIFACT_DIR for the eye)
//   in four states: idle, one button hovered, one button pressed, and the panel after its menu was deleted.
//   Checks on the pixels: a checked button has an accent outline and a plain one does not, a disabled and a
//   missing button are dimmer than a normal one, hover and press change the button's fill, the themes and
//   states differ, and the validation layer (Debug trees) reports nothing.
// Callers: CTest (label gpu, offscreen, no window).
#include <cmath>
#include <memory>

#include "VisualSupport.h"
#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/CommandRouter.h"
#include "r1ui/commands/custommenu/CustomMenuSet.h"
#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/custommenu/CustomMenuPanel.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::core::tree::WidgetId;
namespace cm = r1ui::commands::custommenu;
namespace cmd = r1ui::commands;

struct Px {
  int r, g, b;
};

Px pixelAt(const image::Image& img, double x, double y) {
  const size_t i = (static_cast<size_t>(y) * img.width + static_cast<size_t>(x)) * 4;
  return {img.rgba[i], img.rgba[i + 1], img.rgba[i + 2]};
}
int luma(const Px& p) { return (p.r * 3 + p.g * 6 + p.b) / 10; }
bool same(const Px& a, const Px& b, int tol) { return std::abs(a.r - b.r) <= tol && std::abs(a.g - b.g) <= tol && std::abs(a.b - b.b) <= tol; }

// Registry, router and menus live as long as the build function; the UiContext goes first.
struct World {
  cmd::CommandRegistry registry;
  cmd::KeybindingOverrides overrides{registry};
  cmd::Keymap keymap{registry, overrides};
  std::unique_ptr<UiClock> clock;
  std::unique_ptr<cmd::CommandRouter> router;
  std::unique_ptr<CommandUiSync> sync;
  cm::CustomMenuSet set;
  bool grid = true;
  // Filled by the build function: the buttons' rectangles in image pixels.
  r1ui::core::layout::Rect button[6];
  bool built = false;
};

void declare(World& w, const std::string& id, const std::string& label, const std::string& icon, cmd::CommandKind kind, bool enabled = true) {
  cmd::CommandDef def;
  def.id = id;
  def.label = label;
  def.icon = icon;
  def.kind = kind;
  if (kind == cmd::CommandKind::Radio) def.radioGroup = "g";
  def.enabled = [enabled] { return enabled; };
  World* world = &w;
  def.checked = [world, kind] { return kind == cmd::CommandKind::Toggle && world->grid; };
  def.execute = [](const cmd::ExecuteArgs&) { return cmd::ExecuteResult::handled(); };
  w.registry.add(def);
}

enum class Scene { Idle, Hover, Pressed, Deleted };

}  // namespace

int main() {
  const VisualPaths paths = r1test::visual::paths();
  const char* names[] = {"idle", "hover", "pressed", "deleted"};
  image::Image renders[2][4];
  r1ui::core::layout::Rect buttons[2][4][6];
  for (int t = 0; t < 2; ++t) {
    const r1ui::theme::ThemeId theme = t == 0 ? r1ui::theme::ThemeId::Dark : r1ui::theme::ThemeId::Light;
    for (int s = 0; s < 4; ++s) {
      auto world = std::make_shared<World>();
      const Scene scene = static_cast<Scene>(s);
      BuildFn build = [world, scene](UiContext& ui, WidgetId parent) {
        World& w = *world;
        w.clock = std::make_unique<UiClock>(ui);
        w.router = std::make_unique<cmd::CommandRouter>(w.registry, w.keymap, *w.clock);
        const CommandServices services{w.registry, w.overrides, w.keymap, *w.router};
        w.sync = std::make_unique<CommandUiSync>(ui, services);
        declare(w, "tool.move", "Move", "move-3d", cmd::CommandKind::Action);
        declare(w, "tool.rotate", "Rotate", "rotate-cw", cmd::CommandKind::Action);
        declare(w, "view.grid", "Show grid", "grid-3x3", cmd::CommandKind::Toggle);
        declare(w, "edit.undo", "Undo", "undo2", cmd::CommandKind::Action);
        declare(w, "edit.redo", "Redo", "redo2", cmd::CommandKind::Action, false);
        const std::string id = w.set.createMenu(cm::MenuKind::Panel, "Quick").id;
        for (const char* c : {"tool.move", "tool.rotate", "view.grid", "edit.undo", "edit.redo", "plugin.missing"}) w.set.addEntry(id, c);
        w.set.setPanelColumns(id, 3);
        w.set.setPanelButtonSize(id, 44);
        CustomMenuPanel& panel = ui.create<CustomMenuPanel>(parent, services, *w.sync, w.set, id);
        panel.style().width = r1ui::core::layout::Length::px(340);
        panel.style().height = r1ui::core::layout::Length::px(170);
        ui.frame();
        for (size_t i = 0; i < 6; ++i) w.button[i] = ui.absRect(panel.button(i)->id());
        w.built = true;
        if (scene == Scene::Hover || scene == Scene::Pressed) {
          const auto r = w.button[0];
          ui.pointerMove(r.x + r.w / 2.0, r.y + r.h / 2.0);
          if (scene == Scene::Pressed) ui.pointerDown(r.x + r.w / 2.0, r.y + r.h / 2.0);
        }
        if (scene == Scene::Deleted) {
          w.set.deleteMenu(id);
          ui.frame();
        }
        return panel.id();
      };
      RenderSpec spec;
      spec.width = 360;
      spec.height = 190;
      spec.theme = theme;
      spec.padding = 10;
      spec.background = "panel-secondary";
      const image::Image render = renderWidget(build, spec, paths);
      image::writePng(paths.artifactDir / (std::string("custommenu-panel-") + names[s] + (t == 0 ? "-dark" : "-light") + ".png"), render.width, render.height, render.rgba);
      renders[t][s] = render;
      for (size_t i = 0; i < 6; ++i) buttons[t][s][i] = world->button[i];
      R1_EXPECT(render.width == 360 && render.height == 190 && world->built);
    }
  }
  for (int t = 0; t < 2; ++t) {
    const image::Image& idle = renders[t][0];
    const r1ui::core::layout::Rect* b = buttons[t][0];
    // A pixel just inside each button's left edge: below the icon row, in the button's own fill.
    const auto edge = [&](const r1ui::core::layout::Rect& r, const image::Image& img) { return pixelAt(img, r.x + 4.0, r.y + r.h / 2.0); };
    const Px normal = edge(b[0], idle);
    const Px checked = edge(b[2], idle);
    const Px disabled = edge(b[4], idle);
    const Px missing = edge(b[5], idle);
    // The checked button (the toggle that is on) is tinted towards the accent and differs from a normal one.
    R1_EXPECT(!same(normal, checked, 4));
    // Hover and press change the fill of the first button, not the others.
    R1_EXPECT(!same(edge(b[0], renders[t][1]), normal, 2));
    R1_EXPECT(!same(edge(b[0], renders[t][2]), edge(b[0], renders[t][1]), 2));
    R1_EXPECT(same(edge(b[1], renders[t][1]), edge(b[1], idle), 2));
    // Disabled and missing buttons are dimmed: closer to the panel background than a normal button.
    const Px background = pixelAt(idle, 2, 2);
    R1_EXPECT(std::abs(luma(disabled) - luma(background)) < std::abs(luma(normal) - luma(background)) + 1);
    R1_EXPECT(same(disabled, missing, 6));
    // After the menu is deleted no button is left to sample: the panel shows a notice only.
    R1_EXPECT(renders[t][3].rgba != idle.rgba);
    for (int s = 0; s < 4; ++s) {
      if (t == 1) R1_EXPECT(renders[0][s].rgba != renders[1][s].rgba);
      for (int u = s + 1; u < 4; ++u) R1_EXPECT(renders[t][s].rgba != renders[t][u].rgba);
    }
  }
  R1_EXPECT(validationMessageCount() == 0);
  return r1test::finish();
}
