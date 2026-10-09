// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the GPU render of the command gallery page in both themes (written under R1UI_ARTIFACT_DIR for
//   the eye), plus three states of the keybinding editor inside it: a box in capture mode with the live
//   modifier preview, the conflict popup under the box, and the filtered table. Checks that the renders
//   are not empty, differ between themes and states, and that the validation layer (Debug trees) reports
//   nothing while every widget, icon and popup is drawn through the real device.
// Callers: CTest (label gpu, offscreen, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/commands/GalleryCommands.h"
#include "r1ui/widgets/commands/KeybindingEditor.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::core::events::Key;
using r1ui::core::tree::WidgetId;
namespace Mod = r1ui::core::events::Mod;

KeybindingEditor* findEditor(UiContext& ui, WidgetId root) {
  KeybindingEditor* found = nullptr;
  ui.tree().forEachDescendant(root, [&](WidgetId id) {
    if (found == nullptr) found = ui.objectAs<KeybindingEditor>(id);
  });
  return found;
}

void clickLeftOf(UiContext& ui, WidgetId box) {
  const auto r = ui.absRect(box);
  ui.pointerMove(r.x + 20, r.y + r.h / 2);
  ui.pointerDown(r.x + 20, r.y + r.h / 2);
  ui.pointerUp(r.x + 20, r.y + r.h / 2);
  ui.frame();
}

// The page, then `state` steps of interaction with the editor.
enum class State { Idle, Capture, Conflict, Filtered };

BuildFn pageIn(State state) {
  return [state](UiContext& ui, WidgetId parent) {
    buildGalleryCommands(ui, parent);
    ui.frame();
    if (state == State::Idle) return parent;
    KeybindingEditor* editor = findEditor(ui, parent);
    if (editor == nullptr) return parent;
    if (state == State::Filtered) {
      editor->setFilter("ctrl");
      ui.frame();
      return parent;
    }
    editor->setFilter("Copy");
    ui.frame();
    clickLeftOf(ui, editor->boxOf("edit.copy", 0));
    ui.keyDown(Key::Unknown, Mod::kCtrl | Mod::kShift);
    if (state == State::Conflict) {
      ui.keyDown(static_cast<Key>('V'), Mod::kCtrl);
    }
    ui.frame();
    return parent;
  };
}

}  // namespace

int main() {
  const VisualPaths paths = r1test::visual::paths();
  const char* names[] = {"idle", "capture", "conflict", "filtered"};
  image::Image first[4];
  for (const r1ui::theme::ThemeId theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    for (int s = 0; s < 4; ++s) {
      RenderSpec spec;
      spec.width = 1100;
      spec.height = 760;
      spec.theme = theme;
      spec.padding = 0;
      spec.background = "panel";
      const image::Image render = renderWidget(pageIn(static_cast<State>(s)), spec, paths);
      const bool dark = theme == r1ui::theme::ThemeId::Dark;
      image::writePng(paths.artifactDir / (std::string("gallery-commands-") + names[s] + (dark ? "-dark" : "-light") + ".png"), render.width, render.height, render.rgba);
      R1_EXPECT(render.width == 1100 && render.height == 760);
      if (dark) {
        first[s] = render;
      } else {
        R1_EXPECT(render.rgba != first[s].rgba);  // the themes differ
      }
    }
  }
  R1_EXPECT(first[0].rgba != first[1].rgba && first[1].rgba != first[2].rgba && first[0].rgba != first[3].rgba);  // the states differ
  R1_EXPECT(validationMessageCount() == 0);
  return r1test::finish();
}
