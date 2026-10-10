// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the GPU render of the hotkey editor gallery page in both themes, written under R1UI_ARTIFACT_DIR
//   for the eye: the idle page (list, drawn keyboard, tabs), an action selected with its modifier layer
//   shown, the Ctrl layer, the Runtime Command Editor tab with a recording in progress, and the conflict
//   dialog. Checks that the renders are not empty, differ between themes and states, and that the
//   validation layer (Debug trees) reports nothing while every widget, icon and dialog is drawn through
//   the real device.
// Callers: CTest (label gpu, offscreen, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/hotkeys/GalleryHotkeys.h"
#include "r1ui/widgets/hotkeys/HotkeyEditor.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::core::events::Key;
using r1ui::core::tree::WidgetId;
namespace Mod = r1ui::core::events::Mod;
namespace cmd = r1ui::commands;

// A column of the picture's size the page is built into.
class Frame final : public WidgetObject {
 public:
  const char* typeName() const override { return "Frame"; }
  void onAttached() override {
    style().direction = r1ui::core::layout::FlexDirection::Column;
    style().alignItems = r1ui::core::layout::Align::Stretch;
    style().width = r1ui::core::layout::Length::px(1256);
    style().height = r1ui::core::layout::Length::px(736);
  }
};

HotkeyEditor* findEditor(UiContext& ui, WidgetId root) {
  HotkeyEditor* found = nullptr;
  ui.tree().forEachDescendant(root, [&](WidgetId id) {
    if (found == nullptr) found = ui.objectAs<HotkeyEditor>(id);
  });
  return found;
}

enum class State { Idle, Selected, CtrlLayer, Command, Conflict, Hover };

BuildFn pageIn(State state) {
  return [state](UiContext& ui, WidgetId parent) {
    // The page fills a box of the size of the picture (the harness wraps its content otherwise).
    Frame& frame = ui.create<Frame>(parent);
    buildGalleryHotkeys(ui, frame.id());
    ui.frame();
    HotkeyEditor* e = findEditor(ui, parent);
    if (e == nullptr || state == State::Idle) return parent;
    switch (state) {
      case State::Selected: e->selectAction("edit.redo"); break;
      case State::CtrlLayer: e->setModifiers(Mod::kCtrl); break;
      case State::Command:
        e->selectAction("file.save");
        e->setTab(HotkeyEditor::Tab::Command);
        ui.frame();
        e->recorder().begin();
        ui.keyDown(Key::Unknown, Mod::kCtrl | Mod::kShift);
        break;
      case State::Conflict:
        e->selectAction("misc.alpha");
        e->setModifiers(Mod::kCtrl);
        e->assign("misc.alpha", 0, cmd::ChordSequence::single({static_cast<Key>('C'), Mod::kCtrl, false}));
        break;
      case State::Hover: {
        e->selectAction("edit.copy");
        ui.frame();
        const auto& caps = e->keyboard().layout().caps();
        for (size_t i = 0; i < caps.size(); ++i) {
          if (caps[i].id == "v") {
            const auto r = e->keyboard().capRect(i);
            ui.pointerMove(r.x + r.w / 2, r.y + r.h / 2);
          }
        }
        break;
      }
      case State::Idle: break;
    }
    ui.frame();
    return parent;
  };
}

}  // namespace

int main() {
  const VisualPaths paths = r1test::visual::paths();
  const char* names[] = {"idle", "selected", "ctrl", "command", "conflict", "hover"};
  constexpr int kStates = 6;
  image::Image first[kStates];
  for (const r1ui::theme::ThemeId theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    for (int s = 0; s < kStates; ++s) {
      RenderSpec spec;
      spec.width = 1280;
      spec.height = 760;
      spec.theme = theme;
      spec.padding = 12;
      spec.background = "panel";
      const image::Image render = renderWidget(pageIn(static_cast<State>(s)), spec, paths);
      const bool dark = theme == r1ui::theme::ThemeId::Dark;
      image::writePng(paths.artifactDir / (std::string("hotkey-editor-") + names[s] + (dark ? "-dark" : "-light") + ".png"), render.width, render.height, render.rgba);
      R1_EXPECT(render.width == 1280 && render.height == 760);
      if (dark) {
        first[s] = render;
      } else {
        R1_EXPECT(render.rgba != first[s].rgba);  // the themes differ
      }
    }
  }
  for (int a = 0; a < kStates; ++a) {
    for (int b = a + 1; b < kStates; ++b) R1_EXPECT(first[a].rgba != first[b].rgba);  // the states differ
  }
  R1_EXPECT(validationMessageCount() == 0);
  return r1test::finish();
}
