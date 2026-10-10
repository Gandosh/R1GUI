// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the fixture of the menu creator tests: a headless window, a registry of a dozen commands that all
//   carry a description, the usual router, a live CustomMenuSet, a CreatorSession, a CreateCustomMenuWindow
//   with recording hooks, and helpers that click widgets, type into the focused field, drag between two
//   points with real pointer events and find the row of an action in the list.
// Callers: tests/ui-widgets/custommenu/creator/*_test.cpp (and the GPU test of the folder).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/CommandRouter.h"
#include "r1ui/commands/Keymap.h"
#include "r1ui/commands/Overrides.h"
#include "r1ui/commands/custommenu/CustomMenuSet.h"
#include "r1ui/widgets/actions/ActionList.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/custommenu/creator/CreateCustomMenuWindow.h"
#include "r1ui/widgets/custommenu/creator/PanelPreviewEditor.h"
#include "r1ui/widgets/custommenu/creator/PiePreviewEditor.h"
#include "r1ui/widgets/segmented/Segmented.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace r1test {

namespace cmd = r1ui::commands;
namespace cm = r1ui::commands::custommenu;
namespace ev = r1ui::core::events;
using r1ui::core::tree::WidgetId;
using r1ui::widgets::ActionList;
using r1ui::widgets::CreateCustomMenuWindow;
using r1ui::widgets::CreatorHooks;
using r1ui::widgets::CreatorSession;
using r1ui::widgets::PanelPreviewEditor;
using r1ui::widgets::PiePreviewEditor;

struct CreatorFixture {
  explicit CreatorFixture(int width = 1120, int height = 740)
      : t(width, height), clock(t.ui), router(registry, keymap, clock), session(set) {
    t.ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
    declare("tool.move", "Move", "move-3d", "Drag objects in the viewport");
    declare("tool.rotate", "Rotate", "rotate-cw", "Rotate the selection");
    declare("tool.scale", "Scale", "maximize", "Scale the selection");
    declare("tool.select", "Select", "mouse-pointer", "Click objects to select them");
    declare("edit.undo", "Undo", "undo2", "Undo the last change");
    declare("edit.redo", "Redo", "redo2", "Redo the change that was undone");
    declare("edit.copy", "Copy", "copy", "Copy the selection");
    declare("view.grid", "Show grid", "grid-3x3", "Show or hide the grid");
    declare("view.frame", "Frame selection", "scan", "Bring the selection into view");
    declare("file.save", "Save", "save", "Save the document");
    declare("file.open", "Open", "folder-open", "Open a document");
    declare("view.zoomIn", "Zoom in", "zoom-in", "Zoom in");
  }

  ~CreatorFixture() {
    t.ui.overlays().closeAll();
    if (window != nullptr && t.ui.alive(windowId)) t.ui.destroy(windowId);
  }

  void declare(const std::string& id, const std::string& label, const std::string& icon, const std::string& description) {
    cmd::CommandDef def;
    def.id = id;
    def.label = label;
    def.description = description;
    def.icon = icon;
    def.category = id.substr(0, id.find('.'));
    def.execute = [this, id](const cmd::ExecuteArgs&) {
      ++runs[id];
      return cmd::ExecuteResult::handled();
    };
    R1_EXPECT(registry.add(def).ok);
  }

  r1ui::widgets::CommandServices services() { return {registry, overrides, keymap, router}; }

  CreateCustomMenuWindow& makeWindow() {
    CreatorHooks hooks;
    hooks.committed = [this](const std::string& id, bool edited) {
      committed.push_back(id);
      committedEdit.push_back(edited);
    };
    hooks.cancelled = [this] { ++cancelled; };
    hooks.saveFile = [this](const cm::CustomMenu& menu) { saved.push_back(menu); };
    hooks.loadFile = [this] { ++loadRequests; };
    CreateCustomMenuWindow& w = t.ui.create<CreateCustomMenuWindow>(t.ui.root(), services(), session, std::move(hooks));
    w.style().width = r1ui::core::layout::Length::px(width());
    w.style().height = r1ui::core::layout::Length::px(height());
    window = &w;
    windowId = w.id();
    settle();
    return w;
  }

  double width() const { return t.ui.viewportWidth(); }
  double height() const { return t.ui.viewportHeight(); }

  // Timers (the rebuild), layout and a frame, a few times.
  void settle() {
    for (int i = 0; i < 4; ++i) {
      t.ui.setTime(t.ui.now() + 40);
      t.ui.tick();
      if (window != nullptr && t.ui.alive(windowId)) {
        if (ActionList* list = window->actions()) list->flush();
      }
      t.layout();
    }
  }

  void click(WidgetId id, ev::Button button = ev::Button::Left) {
    const auto r = t.ui.absRect(id);
    const double x = r.x + r.w / 2.0;
    const double y = r.y + r.h / 2.0;
    t.ui.pointerMove(x, y);
    t.ui.pointerDown(x, y, button);
    t.ui.pointerUp(x, y, button);
    settle();
  }

  void clickAt(double x, double y, ev::Button button = ev::Button::Left) {
    t.ui.pointerMove(x, y);
    t.ui.pointerDown(x, y, button);
    t.ui.pointerUp(x, y, button);
    settle();
  }

  // Clicks item `index` of a Segmented by scanning for the x where itemAt answers it.
  void clickSegment(WidgetId id, int index) {
    auto* seg = t.ui.objectAs<r1ui::widgets::Segmented>(id);
    R1_EXPECT(seg != nullptr);
    if (seg == nullptr) return;
    const auto r = t.ui.absRect(id);
    for (int x = 0; x < r.w; ++x) {
      if (seg->itemAt(static_cast<double>(x)) == index) {
        clickAt(r.x + x + 2.0, r.y + r.h / 2.0);
        return;
      }
    }
    R1_EXPECT(false);
  }

  void key(ev::Key k, uint8_t mods = 0) {
    t.ui.keyDown(k, mods);
    t.ui.keyUp(k, mods);
    settle();
  }

  // Focuses the field, selects its text and types `text`.
  void typeInto(WidgetId field, const std::string& text) {
    t.ui.focusWidget(field, ev::FocusReason::Keyboard);
    t.ui.keyDown(static_cast<ev::Key>('A'), ev::Mod::kCtrl);
    t.ui.keyUp(static_cast<ev::Key>('A'), ev::Mod::kCtrl);
    if (text.empty()) {  // clearing the field: the selection is replaced by nothing
      t.ui.keyDown(ev::Key::Backspace);
      t.ui.keyUp(ev::Key::Backspace);
    }
    for (const char c : text) t.ui.textInput(static_cast<char32_t>(static_cast<unsigned char>(c)));
    settle();
  }

  // A pointer drag with real events: press, move past the threshold, follow a straight line, release.
  // Returns after the release; `holdBeforeRelease` runs with the button still down.
  template <class Between = void (*)()>
  void drag(double x0, double y0, double x1, double y1, Between between = [] {}) {
    t.ui.setTime(t.ui.now() + 40);
    t.ui.pointerMove(x0, y0);
    t.ui.pointerDown(x0, y0);
    const int steps = 8;
    for (int i = 1; i <= steps; ++i) {
      const double f = static_cast<double>(i) / steps;
      t.ui.setTime(t.ui.now() + 16);
      t.ui.pointerMove(x0 + (x1 - x0) * f + (i == 1 ? 9.0 : 0.0), y0 + (y1 - y0) * f);
    }
    t.ui.pointerMove(x1, y1);
    between();
    t.ui.pointerUp(x1, y1);
    settle();
  }

  // Drags the list row of `commandId` to a window point.
  bool dragAction(const std::string& commandId, double x, double y) {
    ActionList* list = window != nullptr ? window->actions() : nullptr;
    if (list == nullptr || !list->view().selectAction(commandId)) return false;
    settle();
    const auto rect = list->view().rowRect(list->view().selectedRow());
    if (rect.w <= 0.0) return false;
    drag(rect.x + 40.0, rect.y + rect.h / 2.0, x, y);
    return true;
  }

  r1test::TestUi t;
  cmd::CommandRegistry registry;
  cmd::KeybindingOverrides overrides{registry};
  cmd::Keymap keymap{registry, overrides};
  r1ui::widgets::UiClock clock;
  cmd::CommandRouter router;
  cm::CustomMenuSet set;
  CreatorSession session;
  CreateCustomMenuWindow* window = nullptr;
  WidgetId windowId;
  std::map<std::string, int> runs;
  std::vector<std::string> committed;
  std::vector<bool> committedEdit;
  std::vector<cm::CustomMenu> saved;
  int cancelled = 0;
  int loadRequests = 0;
};

}  // namespace r1test
