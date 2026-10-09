// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of DriveStatus.h.
// Invariants: every rectangle is in physical screen pixels (virtual desktop); a window or widget that is
//   gone is skipped, never an error; the text is built from public accessors only.
// Callers: PreviewApp.
#include "DriveStatus.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

#include "PreviewApp.h"
#include "r1ui/widgets/commands/KeybindingEditor.h"
#include "r1ui/widgets/customize/CustomizeToolStrip.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/numberfield/NumberField.h"

namespace preview {

namespace rw = r1ui::widgets;
namespace dk = r1ui::dock;

namespace {

std::string hexOf(void* handle) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%p", handle);
  return buffer;
}

std::string rectText(int x, int y, int w, int h) { return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," + std::to_string(h); }

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
  for (const rw::FloatId id : backend.stacking()) {
    r1ui::platform::Window* win = backend.nativeWindow(id);
    const std::optional<dk::Rect> content = backend.contentRect(id);
    if (win == nullptr || !content) continue;
    const r1ui::platform::Rect r = win->windowRect();
    out << "float id=" << id << " hwnd=" << hexOf(win->nativeHandle().window) << " rect=" << rectText(r.x, r.y, r.width, r.height) << " visible=" << (win->isVisible() ? 1 : 0)
        << " monitor=" << backend.monitorAt({content->x + content->w / 2.0, content->y + content->h / 2.0}) << "\n";
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
  for (dk::PanelId p = 1; p <= editor::panel::kCount; ++p) {
    if (rw::DockTabStrip* strip = dock.stripOf(p)) {
      const dk::Rect r = strip->tabRect(*strip->indexOf(p));
      const dk::Point screen = backend.toScreen(strip->window(), {r.x + r.w / 2.0, r.y + r.h / 2.0});
      const dk::Point physical = backend.screenSpace().toPhysical(screen);
      out << " " << p << "=" << std::lround(physical.x) << "," << std::lround(physical.y);
    }
  }
  out << "\n";

  // Widgets of the main window: client origin plus logical rectangle times the display scale.
  rw::UiContext& ui = *app.editorUi();
  const r1ui::platform::Point origin = window.clientOrigin();
  const double scale = window.dpiScale();
  const auto widgetLine = [&](const char* kind, const std::string& name, r1ui::core::tree::WidgetId id) {
    if (!ui.alive(id)) return;
    const r1ui::core::layout::Rect r = ui.absRect(id);
    out << kind << " " << name << "=" << rectText(static_cast<int>(std::lround(origin.x + r.x * scale)), static_cast<int>(std::lround(origin.y + r.y * scale)),
                                                   static_cast<int>(std::lround(r.w * scale)), static_cast<int>(std::lround(r.h * scale)))
        << "\n";
  };
  widgetLine("widget", "menubar", editor->menuBarWidget());
  widgetLine("widget", "toolbar", editor->toolbarWidget());
  widgetLine("widget", "layout", editor->layoutSelectWidget());
  widgetLine("widget", "dock", editor->dockWidget());
  widgetLine("widget", "status", editor->statusWidget());
  if (rw::CustomizableMenuBar* bar = editor->menuBar(); bar != nullptr && bar->menuBar() != nullptr) {
    rw::MenuBar* menus = bar->menuBar();
    for (int i = 0; i < menus->menuCount(); ++i) {
      const rw::MenuBarItem* item = ui.objectAs<rw::MenuBarItem>(menus->itemWidget(i));
      if (item != nullptr) widgetLine("menu", item->title(), menus->itemWidget(i));
    }
  }
  // Controls inside panels (only while the panel is open and shown): the first number field of the
  // inspector (Position X), the viewport, the customize tool strip, the shortcut editor's search and the
  // Move tool's first chord box.
  const auto firstOf = [&](r1ui::core::tree::WidgetId root, auto test) {
    r1ui::core::tree::WidgetId found;
    if (ui.alive(root)) ui.tree().forEachDescendant(root, [&](r1ui::core::tree::WidgetId id) {
      if (!found.valid() && test(id)) found = id;
    }, true);
    return found;
  };
  widgetLine("widget", "viewport", dock.contentOf(editor::panel::kViewport));
  widgetLine("widget", "posx", firstOf(dock.contentOf(editor::panel::kInspector), [&](auto id) { return ui.objectAs<rw::NumberField>(id) != nullptr; }));
  const r1ui::core::tree::WidgetId strip = firstOf(dock.contentOf(editor::panel::kQuickActions), [&](auto id) { return ui.objectAs<rw::CustomizeToolStrip>(id) != nullptr; });
  if (rw::CustomizeToolStrip* s = ui.objectAs<rw::CustomizeToolStrip>(strip)) {
    widgetLine("widget", "newmenu", s->newMenuButton());
    widgetLine("widget", "customize-toggle", s->toggleButton());
  }
  const r1ui::core::tree::WidgetId keys = firstOf(dock.contentOf(editor::panel::kShortcuts), [&](auto id) { return ui.objectAs<rw::KeybindingEditor>(id) != nullptr; });
  if (rw::KeybindingEditor* k = ui.objectAs<rw::KeybindingEditor>(keys)) {
    widgetLine("widget", "kb-search", k->searchInput());
    widgetLine("widget", "kb-move0", k->boxOf("tool.move", 0));
  }
  const r1ui::props::Vec3 cube = editor->model().mesh(0).position;
  const r1ui::props::UndoStack& undo = editor->model().context().undo();
  out << "text tool=" << editor->model().tool << " cube=" << cube.x << "," << cube.y << " undo=" << (undo.canUndo() ? undo.undoLabel() : std::string("-")) << " overlays=" << ui.overlays().stack().size() << "\n";
  out << "text layout=" << editor->layoutName() << " status=" << editor->statusText() << " edit=" << (editor->controller().editMode() ? 1 : 0)
      << " floating=" << backend.windowCount() << "\n";
  return out.str();
}

}  // namespace preview
