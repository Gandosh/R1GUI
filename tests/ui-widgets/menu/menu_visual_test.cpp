// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the visual oracle of the menu widgets against the reference captures in both themes: the
//   canvas context menu (content, rows idle / hover / disabled / component tone / submenu trigger),
//   its Copy/Paste as submenu, the menu bar title (idle and hover), the File, Edit and View menus
//   under the menu bar, the one-item page menu, the toolbar flyout and the two-line "add variable"
//   menu. Each case renders the menu at the position of the reference screen on a 1440 x 900 canvas
//   and compares the crop with the reference under a tolerance profile (numbers are printed; nothing
//   is loosened here).
// Callers: CTest (label gpu, offscreen, no window).
// Method notes: text in the reference has LCD subpixel antialiasing, ours is grayscale, so text
//   crops compare luminance only (documented per case); screens that include UI around a menu (the
//   side panels beside the menu bar menus) compare only the menu rectangles (`only`).
#include "ReferenceMenus.h"
#include "ScreenCompare.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/menu/MenuPanel.h"

namespace {

using namespace r1test::screen;
using r1ui::theme::ThemeId;
namespace m = r1test::menus;

MenuPanel* panelOf(UiContext& ui, const MenuController& c, int level) { return ui.objectAs<MenuPanel>(c.panelAt(level)); }

// Opens the canvas context menu where the reference screen shows it. `submenuRow` >= 0 also opens the
// submenu of that row (keeping its row highlighted like an open submenu trigger); the returned widget
// is row `row` of the submenu when `inSubmenu`, else of the root menu.
BuildFn contextMenu(int row, bool openSubmenu, bool inSubmenu) {
  return [=](UiContext& ui, WidgetId) {
    MenuController controller(ui);
    controller.openContextMenu(m::contextMenu(), 412, 188);
    MenuPanel* root = panelOf(ui, controller, 0);
    ui.frame();  // the submenu anchors at the laid-out row
    if (openSubmenu) {
      root->setHighlight(28, false);
      root->itemActivated(28, false);  // click on the trigger opens its submenu
    }
    MenuPanel* target = inSubmenu ? panelOf(ui, controller, 1) : root;
    return target->itemWidget(row);
  };
}

BuildFn menuAt(MenuSpec (*spec)(), int x, int y, int row, double minWidth = 0.0, bool shadowOverlay = false) {
  return [=](UiContext& ui, WidgetId) {
    MenuController controller(ui);
    MenuSpec s = spec();
    if (minWidth > 0.0) s.minWidth = minWidth;
    MenuOpenOptions o;
    o.anchor = {x, y, 0, 0};
    o.placement = Placement::Manual;
    o.windowMargin = 0.0;
    if (shadowOverlay) o.shadow = "overlay";
    controller.open(std::move(s), o);
    return panelOf(ui, controller, 0)->itemWidget(row);
  };
}

// The menu bar of the reference at its screen position; opens menu `open` (or none with -1).
BuildFn menuBar(int open, int submenuRow = -1) {
  return [=](UiContext& ui, WidgetId parent) {
    MenuBar& bar = ui.create<MenuBar>(parent);
    bar.style().margin[r1ui::core::layout::kLeft] = r1ui::core::layout::Length::px(6);
    bar.style().margin[r1ui::core::layout::kTop] = r1ui::core::layout::Length::px(36);
    bar.addMenu("File", m::fileMenu());
    bar.addMenu("Edit", m::editMenu());
    bar.addMenu("View", m::viewMenu());
    bar.addMenu("Object", MenuSpec{{menuAction("x", "Object")}});
    ui.frame();
    if (open >= 0) {
      bar.openMenu(open);
      ui.frame();  // a submenu anchors at the laid-out row
      if (submenuRow >= 0) {
        MenuPanel* panel = ui.objectAs<MenuPanel>(bar.controller().panelAt(0));
        panel->setHighlight(submenuRow, false);
        panel->itemActivated(submenuRow, false);
      }
    }
    return bar.itemWidget(0);
  };
}

Case crop(const char* reference, double x, double y, ThemeId theme, VisualState state = VisualState::Idle) {
  Case c;
  c.reference = reference;
  c.clipX = x;
  c.clipY = y;
  c.theme = theme;
  c.state = state;
  c.luminance = true;  // reference text has LCD subpixel antialiasing; ours is grayscale
  c.profile = "text";
  return c;
}

}  // namespace

int main() {
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    // ---- canvas context menu ----
    R1_EXPECT_CROP(contextMenu(7, false, false), crop("widget-menu-content-idle", 406, 182, theme));
    R1_EXPECT_CROP(contextMenu(7, false, false), crop("widget-menu-item-idle", 411, 364, theme));
    R1_EXPECT_CROP(contextMenu(7, false, false), crop("widget-menu-item-hover", 411, 364, theme, VisualState::Hover));
    R1_EXPECT_CROP(contextMenu(12, false, false), crop("widget-menu-item-disabled-idle", 411, 485, theme));
    R1_EXPECT_CROP(contextMenu(12, false, false), crop("widget-menu-item-disabled-hover", 411, 485, theme, VisualState::Hover));
    R1_EXPECT_CROP(contextMenu(20, false, false), crop("widget-menu-item-component-idle", 411, 690, theme));
    R1_EXPECT_CROP(contextMenu(20, false, false), crop("widget-menu-item-component-hover", 411, 690, theme, VisualState::Hover));
    R1_EXPECT_CROP(contextMenu(28, false, false), crop("widget-menu-item-submenu-idle", 411, 857, theme));
    R1_EXPECT_CROP(contextMenu(28, true, false), crop("widget-menu-item-submenu-open", 411, 857, theme));
    R1_EXPECT_CROP(contextMenu(28, true, true), crop("widget-menu-submenu-content-idle", 625, 711, theme));
    R1_EXPECT_CROP(contextMenu(1, true, true), crop("widget-menu-item-in-submenu-hover", 630, 744, theme, VisualState::Hover));

    // ---- menu bar and its menus ----
    {
      Case idle = crop("widget-menubar-item-idle", 0, 30, theme);
      idle.canvasToken = "panel";
      idle.page = false;
      R1_EXPECT_CROP(menuBar(-1), idle);
      Case hover = crop("widget-menubar-item-hover", 0, 30, theme, VisualState::Hover);
      hover.canvasToken = "panel";
      hover.page = false;
      R1_EXPECT_CROP(menuBar(-1), hover);
    }
    {
      // File menu with the Export selection submenu: compare the two menu rectangles only.
      Case file = crop("screen-file-menu-submenu-open", 0, 0, theme);
      file.cropW = 400;
      file.cropH = 320;
      file.only = {{6, 66, 205, 228}, {213, 200, 172, 92}};
      R1_EXPECT_CROP(menuBar(0, 6), file);
      Case edit = crop("screen-menubar-edit-open", 0, 0, theme);
      edit.cropW = 300;
      edit.cropH = 360;
      edit.only = {{43, 66, 205, 276}};
      R1_EXPECT_CROP(menuBar(1), edit);
      Case view = crop("screen-menubar-view-open", 0, 0, theme);
      view.cropW = 340;
      view.cropH = 360;
      view.only = {{83, 66, 205, 276}};
      R1_EXPECT_CROP(menuBar(2), view);
    }

    // ---- page menu, toolbar flyout, two-line items ----
    // The page menu opens over the side panel and the two-line menu over the (dimmed) dialog, so
    // their surroundings are the panel colour; the flyout sits on the canvas above the floating
    // toolbar, whose top edge shows in the last rows of the content crop and is ignored.
    const auto onPanel = [](Case c) {
      c.canvasToken = "panel";
      c.page = false;
      return c;
    };
    R1_EXPECT_CROP(menuAt(&m::pageMenu, 131, 172, 0, 144.0), onPanel(crop("widget-menu-item-disabled-page-delete-idle", 130, 171, theme)));
    R1_EXPECT_CROP(menuAt(&m::pageMenu, 131, 172, 0, 144.0), onPanel(crop("widget-menu-item-disabled-page-delete-hover", 130, 171, theme, VisualState::Hover)));
    Case flyout = crop("widget-flyout-content-idle", 613, 767, theme);
    flyout.ignore = {{0, 75, 149, 3}};
    flyout.page = false;
    R1_EXPECT_CROP(menuAt(&m::flyoutMenu, 619, 773, 0, 138.0), flyout);
    Case flyoutItem = crop("widget-flyout-item-idle", 618, 800, theme);
    flyoutItem.page = false;
    R1_EXPECT_CROP(menuAt(&m::flyoutMenu, 619, 773, 1, 138.0), flyoutItem);
    Case flyoutHover = crop("widget-flyout-item-hover", 618, 800, theme, VisualState::Hover);
    flyoutHover.page = false;
    R1_EXPECT_CROP(menuAt(&m::flyoutMenu, 619, 773, 1, 138.0), flyoutHover);
    R1_EXPECT_CROP(menuAt(&m::addVariableMenu, 911, 567, 0, 192.0), onPanel(crop("widget-menu-content-described-idle", 905, 561, theme)));
    R1_EXPECT_CROP(menuAt(&m::addVariableMenu, 911, 567, 1, 192.0), onPanel(crop("widget-menu-item-described-idle", 910, 607, theme)));
    R1_EXPECT_CROP(menuAt(&m::addVariableMenu, 911, 567, 1, 192.0), onPanel(crop("widget-menu-item-described-hover", 910, 607, theme, VisualState::Hover)));
  }
  return r1test::finish();
}
