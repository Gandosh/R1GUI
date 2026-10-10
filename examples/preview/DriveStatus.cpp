// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of DriveStatus.h.
// Invariants: every rectangle is in physical screen pixels (virtual desktop); a window or widget that is
//   gone is skipped, never an error; the text is built from public accessors only; widgets inside native
//   floating windows are converted with that window's own origin and scale.
// Callers: PreviewApp.
#include "DriveStatus.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

#include "PreviewApp.h"
#include "r1ui/widgets/actions/ActionList.h"
#include "r1ui/widgets/custommenu/CustomMenuPanel.h"
#include "r1ui/widgets/custommenu/creator/CreateCustomMenuWindow.h"
#include "r1ui/widgets/custommenu/creator/PanelPreviewEditor.h"
#include "r1ui/widgets/custommenu/creator/PiePreviewEditor.h"
#include "r1ui/widgets/hotkeys/HotkeyEditor.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/numberfield/NumberField.h"

namespace preview {

namespace rw = r1ui::widgets;
namespace dk = r1ui::dock;
using r1ui::core::tree::WidgetId;

namespace {

std::string hexOf(void* handle) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%p", handle);
  return buffer;
}

std::string rectText(int x, int y, int w, int h) { return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," + std::to_string(h); }

// One UI context (the main window's or a native window's) with where its client area is on the screen.
struct Surface {
  rw::UiContext* ui = nullptr;
  r1ui::platform::Point origin;
  double scale = 1.0;
};

}  // namespace

std::string driveStatus(PreviewApp& app) {
  editor::EditorApp* editor = app.editor();
  if (editor == nullptr) return {};
  std::ostringstream out;
  r1ui::platform::Window& window = app.window();
  rw::NativeFloatingBackend& backend = app.backend();
  const r1ui::platform::Rect outer = window.windowRect();
  out << "main hwnd=" << hexOf(window.nativeHandle().window) << " rect=" << rectText(outer.x, outer.y, outer.width, outer.height) << " dpi=" << window.dpiScale()
      << " mode=" << static_cast<int>(app.mode()) << "\n";
  std::vector<Surface> surfaces;
  surfaces.push_back({app.editorUi(), window.clientOrigin(), window.dpiScale()});
  for (const rw::FloatId id : backend.stacking()) {
    r1ui::platform::Window* win = backend.nativeWindow(id);
    const std::optional<dk::Rect> content = backend.contentRect(id);
    if (win == nullptr || !content) continue;
    const r1ui::platform::Rect r = win->windowRect();
    out << "float id=" << id << " hwnd=" << hexOf(win->nativeHandle().window) << " rect=" << rectText(r.x, r.y, r.width, r.height) << " visible=" << (win->isVisible() ? 1 : 0)
        << " monitor=" << backend.monitorAt({content->x + content->w / 2.0, content->y + content->h / 2.0}) << "\n";
    if (const std::optional<rw::FloatContent> floating = backend.content(id); floating && floating->ui != nullptr) surfaces.push_back({floating->ui, win->clientOrigin(), win->dpiScale()});
  }
  rw::DockHost& dock = editor->dock();
  for (const dk::Area& area : dock.layout().areas()) {
    out << "area id=" << area.id << " main=" << (area.id == dk::kMainAreaId ? 1 : 0) << " panels=";
    std::vector<const dk::Node*> pending;
    if (area.root) pending.push_back(&*area.root);
    while (!pending.empty()) {
      const dk::Node* n = pending.back();
      pending.pop_back();
      for (const dk::PanelId p : n->tabs) out << p << ",";
      for (const dk::Node& c : n->children) pending.push_back(&c);
    }
    out << "\n";
  }
  out << "tabs";
  for (const rw::PanelDescriptor& descriptor : editor->panels().all()) {
    const dk::PanelId p = descriptor.id;
    if (rw::DockTabStrip* strip = dock.stripOf(p)) {
      const dk::Rect r = strip->tabRect(*strip->indexOf(p));
      const dk::Point screen = backend.toScreen(strip->window(), {r.x + r.w / 2.0, r.y + r.h / 2.0});
      const dk::Point physical = backend.screenSpace().toPhysical(screen);
      out << " " << p << "=" << std::lround(physical.x) << "," << std::lround(physical.y);
    }
  }
  out << "\n";

  // Widgets: client origin plus logical rectangle times the display scale.
  const auto rectLine = [&](const char* kind, const std::string& name, const Surface& s, WidgetId id) {
    if (s.ui == nullptr || !s.ui->alive(id)) return;
    const r1ui::core::layout::Rect r = s.ui->absRect(id);
    out << kind << " " << name << "=" << rectText(static_cast<int>(std::lround(s.origin.x + r.x * s.scale)), static_cast<int>(std::lround(s.origin.y + r.y * s.scale)),
                                                   static_cast<int>(std::lround(r.w * s.scale)), static_cast<int>(std::lround(r.h * s.scale)))
        << "\n";
  };
  const auto boxLine = [&](const char* kind, const std::string& name, const Surface& s, double x, double y, double w, double h) {
    out << kind << " " << name << "=" << rectText(static_cast<int>(std::lround(s.origin.x + x * s.scale)), static_cast<int>(std::lround(s.origin.y + y * s.scale)),
                                                   static_cast<int>(std::lround(w * s.scale)), static_cast<int>(std::lround(h * s.scale)))
        << "\n";
  };
  const Surface& mainSurface = surfaces.front();
  rw::UiContext& ui = *mainSurface.ui;
  rectLine("widget", "menubar", mainSurface, editor->menuBarWidget());
  rectLine("widget", "toolbar", mainSurface, editor->toolbarWidget());
  rectLine("widget", "layout", mainSurface, editor->layoutSelectWidget());
  rectLine("widget", "dock", mainSurface, editor->dockWidget());
  rectLine("widget", "status", mainSurface, editor->statusWidget());
  if (rw::CustomizableMenuBar* bar = editor->menuBar(); bar != nullptr && bar->menuBar() != nullptr) {
    rw::MenuBar* menus = bar->menuBar();
    for (int i = 0; i < menus->menuCount(); ++i) {
      const rw::MenuBarItem* item = ui.objectAs<rw::MenuBarItem>(menus->itemWidget(i));
      if (item != nullptr) rectLine("menu", item->title(), mainSurface, menus->itemWidget(i));
    }
  }
  const auto firstOf = [&](WidgetId root, auto test) {
    WidgetId found;
    if (ui.alive(root)) ui.tree().forEachDescendant(root, [&](WidgetId id) {
      if (!found.valid() && test(id)) found = id;
    }, true);
    return found;
  };
  rectLine("widget", "viewport", mainSurface, dock.contentOf(editor::panel::kViewport));
  rectLine("widget", "posx", mainSurface, firstOf(dock.contentOf(editor::panel::kInspector), [&](auto id) { return ui.objectAs<rw::NumberField>(id) != nullptr; }));

  // The windows and panels of the custom menu features, wherever they are shown.
  std::string creatorText;
  for (const Surface& s : surfaces) {
    s.ui->tree().forEachDescendant(s.ui->root(), [&](WidgetId id) {
      if (rw::HotkeyEditor* h = s.ui->objectAs<rw::HotkeyEditor>(id)) {
        rectLine("widget", "hk-editor", s, h->id());
        rectLine("widget", "hk-search", s, h->list().searchField());
        rectLine("widget", "hk-keyboard", s, h->keyboard().id());
        for (const char* label : {"M", "Q", "W", "T"}) {
          const r1ui::commands::KeyCap* cap = nullptr;
          for (const r1ui::commands::KeyCap& c : h->keyboard().layout().caps()) {
            if (c.label == label) cap = &c;
          }
          if (cap == nullptr) continue;
          const size_t index = static_cast<size_t>(cap - h->keyboard().layout().caps().data());
          const r1ui::core::layout::RectD r = h->keyboard().capRect(index);
          boxLine("widget", std::string("hk-cap-") + label, s, r.x, r.y, r.w, r.h);
        }
        const auto& rows = h->list().view().rows();
        for (size_t i = 0; i < rows.size(); ++i) {
          if (rows[i].header) continue;
          const r1ui::core::layout::RectD r = h->list().view().rowRect(static_cast<int>(i));
          if (r.w > 0.0) boxLine("widget", "hk-row0", s, r.x, r.y, r.w, r.h);
          break;
        }
        out << "text hotkeys selected=" << h->selectedAction() << " message=" << h->message() << " sets=" << h->setNames().size() << "\n";
      } else if (rw::CreateCustomMenuWindow* w = s.ui->objectAs<rw::CreateCustomMenuWindow>(id)) {
        rectLine("widget", "creator", s, w->id());
        rectLine("widget", "creator-pie-card", s, w->pieCard());
        rectLine("widget", "creator-panel-card", s, w->panelCard());
        rectLine("widget", "creator-name", s, w->nameField());
        rectLine("widget", "creator-create", s, w->createButton());
        rectLine("widget", "creator-cancel", s, w->cancelButton());
        rectLine("widget", "creator-savefile", s, w->saveFileButton());
        rectLine("widget", "creator-loadfile", s, w->loadFileButton());
        rectLine("widget", "creator-slots", s, w->slotsSelector());
        if (rw::PiePreviewEditor* pie = w->pieEditor()) {
          rectLine("widget", "creator-pie", s, pie->id());
          for (int i = 0; i < 8; ++i) {
            double x = 0.0, y = 0.0;
            if (pie->slotCenter(i, x, y)) boxLine("widget", "creator-slot" + std::to_string(i), s, x - 1.0, y - 1.0, 2.0, 2.0);
          }
        }
        if (rw::PanelPreviewEditor* panel = w->panelEditor()) {
          rectLine("widget", "creator-panelview", s, panel->id());
          const int count = w->session().draft() != nullptr ? static_cast<int>(w->session().draft()->entryCount()) : 0;
          for (int i = 0; i <= count && i < 12; ++i) {
            double x, y, cw, ch;
            if (panel->cellRect(i, x, y, cw, ch)) boxLine("widget", "creator-cell" + std::to_string(i), s, x, y, cw, ch);
          }
        }
        if (rw::ActionList* actions = w->actions()) {
          rectLine("widget", "creator-search", s, actions->searchField());
          const auto& rows = actions->view().rows();
          for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].header) continue;
            const r1ui::core::layout::RectD r = actions->view().rowRect(static_cast<int>(i));
            if (r.w > 0.0) boxLine("widget", "creator-row0", s, r.x, r.y, r.w, r.h);
            break;
          }
        }
        const r1ui::commands::custommenu::MenuDraft* d = w->session().draft();
        creatorText = std::string("text creator chooser=") + (w->showingChooser() ? "1" : "0") + " kind=" + (d != nullptr ? r1ui::commands::custommenu::kindName(d->kind()) : "none") +
                      " filled=" + std::to_string(d != nullptr ? d->filledCount() : 0) + " editing=" + (d != nullptr && d->editing() ? "1" : "0") + " issue=" + w->issue() + " status=" + w->status() + "\n";
      } else if (rw::CustomMenuPanel* panel = s.ui->objectAs<rw::CustomMenuPanel>(id)) {
        rectLine("widget", "cmpanel-" + panel->menuId(), s, panel->id());
        for (size_t i = 0; i < panel->buttonCount() && i < 12; ++i) {
          if (rw::CustomMenuButton* b = panel->button(i)) rectLine("widget", "cmpanel-" + panel->menuId() + "-button" + std::to_string(i), s, b->id());
        }
      }
    }, true);
  }
  out << creatorText;
  for (const auto& menu : editor->menuSet().menus()) {
    out << "cmenu " << menu.id << " " << r1ui::commands::custommenu::kindName(menu.kind) << "|" << menu.name << "|";
    for (const auto& entry : menu.entries) out << entry.commandId << ",";
    out << "\n";
  }
  out << "text viewportpie=" << editor->viewportPieId() << "\n";

  size_t overlays = 0;  // popups and dialogs of every window
  for (const Surface& s : surfaces) overlays += s.ui->overlays().stack().size();
  const r1ui::props::Vec3 cube = editor->model().mesh(0).position;
  const r1ui::props::UndoStack& undo = editor->model().context().undo();
  out << "text tool=" << editor->model().tool << " cube=" << cube.x << "," << cube.y << " undo=" << (undo.canUndo() ? undo.undoLabel() : std::string("-")) << " overlays=" << overlays << "\n";
  out << "text layout=" << editor->layoutName() << " status=" << editor->statusText() << " floating=" << backend.windowCount() << "\n";
  return out.str();
}

}  // namespace preview
