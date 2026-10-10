// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the GPU render of the pie menu in both themes (PNGs under R1UI_ARTIFACT_DIR for the eye): an
//   8-slot pie with the up-right slot highlighted, the same without a highlight, the slot straight down
//   highlighted, a 6-slot pie with empty, dim (disabled), missing and checked slots, a 4-slot pie, and the
//   live path: a real right-button gesture that draws the pie in the overlay layer over an area. Checks:
//   the highlighted slot's pill is in the accent colour while the same spot is not when nothing is
//   highlighted, an empty slot leaves the backdrop colour at its centre, the live pie is centred on the
//   press point, the themes and the scenes differ, and the validation layer (Debug trees) reports nothing.
// Callers: CTest (label gpu, offscreen, no window).
#include <cmath>
#include <memory>

#include "VisualSupport.h"
#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/CommandRouter.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/pie/PieMenu.h"
#include "r1ui/widgets/pie/PieTrigger.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::core::tree::WidgetId;

std::vector<PieSlotView> eightSlots() {
  const char* labels[] = {"Select", "Move", "Rotate", "Zoom in", "Undo", "Redo", "Hand", "Pen tool"};
  const char* icons[] = {"mouse-pointer", "move-3d", "rotate-cw", "zoom-in", "undo2", "redo2", "hand", "pen-tool"};
  std::vector<PieSlotView> slots;
  for (int i = 0; i < 8; ++i) {
    PieSlotView v;
    v.label = labels[i];
    v.icon = icons[i];
    v.filled = true;
    v.selectable = true;
    slots.push_back(v);
  }
  return slots;
}

std::vector<PieSlotView> sixSlots() {
  std::vector<PieSlotView> slots(6);
  slots[0] = {"Copy", "copy", true, true, false, false};
  slots[1] = {"Cut", "scissors", true, true, true, false};        // a toggle that is on
  slots[2] = {"Paste", "clipboard", true, false, false, false};   // disabled: dim
  slots[3] = {};                                                  // empty
  slots[4] = {"Old plugin", "", true, false, false, true};        // missing command
  slots[5] = {"Save", "save", true, true, false, false};
  return slots;
}

std::vector<PieSlotView> fourSlots() {
  std::vector<PieSlotView> slots = eightSlots();
  slots.resize(4);
  slots[2].label = "A rather long label";
  return slots;
}

struct Scene {
  const char* name;
  std::vector<PieSlotView> slots;
  int highlight;
};

// Where the pie and the sampled slots ended up (absolute logical pixels = image pixels at scale 1).
struct Placed {
  r1ui::core::layout::Rect rect;
  r1ui::core::layout::RectD slotA;  // the highlighted slot (or slot 1 when nothing is highlighted)
  r1ui::core::layout::RectD empty;  // an empty slot, when the scene has one
  bool hasEmpty = false;
};

struct Px {
  int r, g, b;
};
Px pixelAt(const image::Image& img, int x, int y) {
  const size_t i = (static_cast<size_t>(y) * img.width + static_cast<size_t>(x)) * 4;
  return {img.rgba[i], img.rgba[i + 1], img.rgba[i + 2]};
}
bool near(const Px& p, const Px& q, int tol) { return std::abs(p.r - q.r) <= tol && std::abs(p.g - q.g) <= tol && std::abs(p.b - q.b) <= tol; }

// The pixel just inside the left end of a slot's pill (left of the icon, so it shows the pill's fill).
Px pillFill(const image::Image& img, const Placed& p) {
  return pixelAt(img, static_cast<int>(p.rect.x + p.slotA.x + 8.0), static_cast<int>(p.rect.y + p.slotA.y + p.slotA.h / 2.0));
}

// Everything a live gesture needs, kept alive by the build function until the render is done.
struct Live {
  r1ui::commands::CommandRegistry registry;
  r1ui::commands::KeybindingOverrides overrides{registry};
  r1ui::commands::Keymap keymap{registry, overrides};
  std::unique_ptr<UiClock> clock;
  std::unique_ptr<r1ui::commands::CommandRouter> router;
  Placed placed;
  double pressX = 300.0;
  double pressY = 215.0;
};

}  // namespace

int main() {
  const VisualPaths paths = r1test::visual::paths();
  const Scene scenes[] = {
      {"highlight-up-right", eightSlots(), 1},
      {"idle", eightSlots(), -1},
      {"highlight-down", eightSlots(), 4},
      {"six-mixed", sixSlots(), 5},
      {"four-long-label", fourSlots(), 2},
  };
  constexpr int kScenes = 5;
  const Px accent[2] = {{59, 130, 246}, {37, 99, 235}};  // dark, light (tokens.json)

  image::Image renders[2][kScenes + 1];
  Placed places[2][kScenes + 1];
  for (int t = 0; t < 2; ++t) {
    const r1ui::theme::ThemeId theme = t == 0 ? r1ui::theme::ThemeId::Dark : r1ui::theme::ThemeId::Light;
    const char* suffix = t == 0 ? "-dark" : "-light";
    for (int s = 0; s < kScenes; ++s) {
      auto placed = std::make_shared<Placed>();
      const Scene& scene = scenes[s];
      BuildFn build = [&scene, placed](UiContext& ui, WidgetId parent) {
        PieMenu& pie = ui.create<PieMenu>(parent, scene.slots);
        pie.setHighlight(scene.highlight);
        ui.frame();
        placed->rect = ui.absRect(pie.id());
        placed->slotA = pie.slotRect(scene.highlight >= 0 ? scene.highlight : 1);
        for (int i = 0; i < pie.slotCount(); ++i) {
          if (!scene.slots[static_cast<size_t>(i)].filled) {
            placed->empty = pie.slotRect(i);
            placed->hasEmpty = true;
          }
        }
        return pie.id();
      };
      RenderSpec spec;
      spec.width = 380;
      spec.height = 380;
      spec.theme = theme;
      spec.padding = 8;
      spec.background = "canvas";
      const image::Image render = renderWidget(build, spec, paths);
      image::writePng(paths.artifactDir / (std::string("pie-") + scene.name + suffix + ".png"), render.width, render.height, render.rgba);
      R1_EXPECT(render.width == 380 && render.height == 380);
      renders[t][s] = render;
      places[t][s] = *placed;
    }

    // The live path: a real gesture, the pie drawn by the trigger in the overlay layer.
    auto live = std::make_shared<Live>();
    for (int i = 0; i < 8; ++i) {
      r1ui::commands::CommandDef def;
      def.id = "live.cmd" + std::to_string(i);
      def.label = eightSlots()[static_cast<size_t>(i)].label;
      def.icon = eightSlots()[static_cast<size_t>(i)].icon;
      def.execute = [](const r1ui::commands::ExecuteArgs&) { return r1ui::commands::ExecuteResult::handled(); };
      R1_EXPECT(live->registry.add(def).ok);
    }
    r1ui::commands::custommenu::CustomMenu pie = r1ui::commands::custommenu::makeEmptyMenu(r1ui::commands::custommenu::MenuKind::Pie, "Live");
    for (int i = 0; i < 8; ++i) pie.entries[static_cast<size_t>(i)].commandId = "live.cmd" + std::to_string(i);
    BuildFn build = [live, pie](UiContext& ui, WidgetId parent) {
      live->clock = std::make_unique<UiClock>(ui);
      live->router = std::make_unique<r1ui::commands::CommandRouter>(live->registry, live->keymap, *live->clock);
      const CommandServices services{live->registry, live->overrides, live->keymap, *live->router};
      PieTrigger& trigger = ui.create<PieTrigger>(parent, services, [pie](double, double) { return std::optional<r1ui::commands::custommenu::CustomMenu>(pie); });
      trigger.style().justifyContent = r1ui::core::layout::Justify::Center;
      trigger.style().alignItems = r1ui::core::layout::Align::Center;
      ui.create<Label>(trigger.id(), "Hold the right mouse button here", LabelRole::Muted);
      ui.frame();
      ui.setTime(1000);
      ui.pointerMove(live->pressX, live->pressY);
      ui.pointerDown(live->pressX, live->pressY, r1ui::core::events::Button::Right);
      ui.setTime(1200);
      ui.tick();
      ui.frame();
      const PiePoint toward = pieSlotOffset(8, 1, 90.0);
      ui.pointerMove(live->pressX + toward.x, live->pressY + toward.y);
      ui.frame();
      PieMenu* menu = ui.objectAs<PieMenu>(trigger.pieWidget());
      if (menu != nullptr) {
        live->placed.rect = ui.absRect(menu->id());
        live->placed.slotA = menu->slotRect(1);
      }
      return trigger.id();
    };
    RenderSpec spec;
    spec.width = 600;
    spec.height = 430;
    spec.theme = theme;
    spec.padding = 0;
    spec.background = "canvas";
    const image::Image render = renderWidget(build, spec, paths);
    image::writePng(paths.artifactDir / (std::string("pie-live-gesture") + suffix + ".png"), render.width, render.height, render.rgba);
    renders[t][kScenes] = render;
    places[t][kScenes] = live->placed;
    // The live pie sits on the press point.
    R1_EXPECT(live->placed.rect.w > 0);
    R1_EXPECT(std::abs(live->placed.rect.x + live->placed.rect.w / 2.0 - live->pressX) <= 1.5);
    R1_EXPECT(std::abs(live->placed.rect.y + live->placed.rect.h / 2.0 - live->pressY) <= 1.5);
  }

  for (int t = 0; t < 2; ++t) {
    // Highlighted slot = accent fill; the same slot idle is the neutral pill colour.
    R1_EXPECT(near(pillFill(renders[t][0], places[t][0]), accent[t], 10));
    R1_EXPECT(near(pillFill(renders[t][2], places[t][2]), accent[t], 10));
    R1_EXPECT(near(pillFill(renders[t][5], places[t][5]), accent[t], 10));
    R1_EXPECT(!near(pillFill(renders[t][1], places[t][1]), accent[t], 40));
    // A pill that is not highlighted is not accent in the highlighted renders either (slot 3 of scene 0).
    {
      Placed p = places[t][0];
      PieMenu probe(eightSlots());
      p.slotA = probe.slotRect(3);
      R1_EXPECT(!near(pillFill(renders[t][0], p), accent[t], 40));
    }
    // The empty slot of the six-slot pie draws no pill: its centre shows the backdrop, not a pill fill.
    const Placed& six = places[t][3];
    R1_EXPECT(six.hasEmpty);
    Placed pill = six;
    pill.slotA = six.empty;
    const Px emptyCentreLeft = pillFill(renders[t][3], pill);
    const Px filledPill = pillFill(renders[t][3], places[t][3]);
    R1_EXPECT(!near(emptyCentreLeft, filledPill, 3));
    // Themes and scenes differ.
    for (int s = 0; s <= kScenes; ++s) {
      if (t == 1) R1_EXPECT(renders[0][s].rgba != renders[1][s].rgba);
      for (int u = s + 1; u <= kScenes; ++u) R1_EXPECT(renders[t][s].rgba != renders[t][u].rgba);
    }
  }
  R1_EXPECT(validationMessageCount() == 0);
  return r1test::finish();
}
