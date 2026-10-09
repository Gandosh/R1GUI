// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GalleryOverlays.h: the static previews and live triggers of the menu,
//   tooltip, popover, dialog and toast widgets in flexbox rows of labelled cells.
// Invariants: every preview is built from the real widgets (OverlayHost surfaces, MenuPanel,
//   TooltipContent, DialogParts, ToastWidget), never from a copy of their painting; the group root
//   owns the controllers, and closes what it opened when it is destroyed.
// Callers: the gallery preview, tests.
#include "r1ui/widgets/menu/GalleryOverlays.h"

#include <memory>

#include "r1ui/widgets/dialog/Dialog.h"
#include "r1ui/widgets/dialog/DialogParts.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/menu/MenuPanel.h"
#include "r1ui/widgets/overlay/OverlayHost.h"
#include "r1ui/widgets/popover/Popover.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/toast/Toast.h"
#include "r1ui/widgets/toast/ToastParts.h"
#include "r1ui/widgets/tooltip/TooltipContent.h"

namespace r1ui::widgets {

using core::tree::WidgetId;
namespace layout = core::layout;

namespace {

// The root of the group: owns the live controllers and closes what they opened when destroyed.
class GalleryRoot final : public WidgetObject {
 public:
  explicit GalleryRoot(UiContext& ui) : menus(ui), toasts(ui) {}
  const char* typeName() const override { return "GalleryOverlays"; }
  void onAttached() override {
    layout::Style& s = style();
    s.direction = layout::FlexDirection::Column;
    s.gapRow = 20.0;
    s.alignItems = layout::Align::Start;
    s.padding[layout::kLeft] = s.padding[layout::kRight] = 16.0;
    s.padding[layout::kTop] = s.padding[layout::kBottom] = 16.0;
    tooltips = RichTooltips::install(ui());
  }
  void onDetached() override {
    menus.close();
    toasts.dismissAll();
  }

  MenuController menus;
  ToastManager toasts;
  std::shared_ptr<RichTooltips> tooltips;
};

DialogBox& box(UiContext& ui, WidgetId parent, layout::FlexDirection direction, double gap, bool wrap = false) {
  DialogBox& b = ui.create<DialogBox>(parent);
  b.style().direction = direction;
  b.style().gapRow = gap;
  b.style().gapColumn = gap;
  b.style().alignItems = layout::Align::Start;
  if (wrap) b.style().wrap = layout::FlexWrap::Wrap;
  return b;
}

// A label that keeps its natural width (the foundation label shrinks and ellipsizes in a fit-content box).
Label& label(UiContext& ui, WidgetId parent, const char* text, LabelRole role) {
  Label& l = ui.create<Label>(parent, text, role);
  l.style().flexShrink = 0.0;
  l.setEllipsis(false);
  return l;
}

DialogBox& section(UiContext& ui, WidgetId parent, const char* title) {
  DialogBox& s = box(ui, parent, layout::FlexDirection::Column, 8.0);
  label(ui, s.id(), title, LabelRole::Title);
  return box(ui, s.id(), layout::FlexDirection::Row, 16.0, true);
}

// One cell: a caption above a sample the caller adds to the returned column.
DialogBox& cell(UiContext& ui, WidgetId parent, const char* caption) {
  DialogBox& c = box(ui, parent, layout::FlexDirection::Column, 6.0);
  label(ui, c.id(), caption, LabelRole::Caption);
  return c;
}

// An overlay surface placed in normal flow, so the real popup look can be shown next to others.
OverlayHost& surface(UiContext& ui, WidgetId parent, OverlaySurface kind, const char* shadow = "") {
  OverlayHost& host = ui.create<OverlayHost>(parent, kind, 0.0, true, shadow);
  host.style().position = layout::Position::Relative;
  for (int e = 0; e < 4; ++e) host.style().inset[e] = layout::Length::autoValue();
  ui.invalidator().setVisible(host.id(), true);
  host.markShown(ui.now());
  return host;
}

void menuPreview(UiContext& ui, WidgetId parent, const char* caption, MenuSpec spec, int highlight = -1, double maxHeight = 0.0, const char* shadow = "") {
  DialogBox& c = cell(ui, parent, caption);
  OverlayHost& host = surface(ui, c.id(), OverlaySurface::Menu, shadow);
  if (maxHeight > 0.0) host.style().maxHeight = layout::Length::px(maxHeight);
  MenuPanel& panel = ui.create<MenuPanel>(host.id(), std::move(spec.items), spec.minWidth, spec.look, nullptr);
  if (highlight >= 0) panel.setHighlight(highlight, false);
}

MenuSpec oneRow(MenuItemSpec item) {
  MenuSpec s;
  s.items = {std::move(item)};
  return s;
}

MenuSpec sampleContextMenu() {
  MenuSpec s;
  s.minWidth = 224.0;
  s.look.arrowGlyph = true;
  MenuItemSpec group = menuAction("group", "Group selection", "Ctrl+G");
  group.enabled = false;
  MenuItemSpec component = menuAction("component", "Create component", "Ctrl+Alt+K");
  component.tone = MenuTone::Component;
  s.items = {menuAction("copy", "Copy", "Ctrl+C"), menuAction("cut", "Cut", "Ctrl+X"), menuAction("paste", "Paste here", "Ctrl+V"), menuSeparator(),
             group, menuAction("flatten", "Flatten", "Alt+Shift+F", "list-collapse"), menuSeparator(), component, menuSeparator(),
             menuSubmenu("Copy/Paste as", {menuAction("text", "Copy as text"), menuAction("svg", "Copy as SVG")})};
  return s;
}

MenuSpec fileLikeMenu() {
  MenuSpec s;
  s.minWidth = 208.0;
  s.items = {menuAction("new", "New", "Ctrl+N"), menuAction("open", "Open...", "Ctrl+O"), menuSeparator(), menuSubmenu("Export selection...", {menuAction("png", "PNG")}),
             menuSeparator(), menuCheck("autosave", "Auto-save to local file", true), menuRadio("light", "Light theme", false), menuRadio("dark", "Dark theme", true)};
  return s;
}

MenuSpec headingMenu() {
  MenuSpec s;
  s.minWidth = 180.0;
  s.items = {menuHeading("Align"), menuAction("left", "Left", "Alt+A"), menuAction("right", "Right", "Alt+D"), menuSeparator(), menuHeading("Distribute"),
             menuAction("h", "Horizontal spacing"), menuHeading("Empty section")};
  return s;
}

MenuSpec tallMenu() {
  MenuSpec s;
  for (int i = 1; i <= 40; ++i) s.items.push_back(menuAction("row" + std::to_string(i), "Row " + std::to_string(i), i % 5 == 0 ? "Ctrl+" + std::to_string(i % 10) : std::string()));
  return s;
}

void tooltipPreview(UiContext& ui, WidgetId parent, const char* caption, TooltipInfo info) {
  DialogBox& c = cell(ui, parent, caption);
  OverlayHost& host = surface(ui, c.id(), OverlaySurface::Tooltip);
  ui.create<TooltipContent>(host.id(), std::move(info));
}

void toastPreview(UiContext& ui, WidgetId parent, const char* caption, const char* text, ToastTone tone) {
  DialogBox& c = cell(ui, parent, caption);
  ui.create<ToastWidget>(c.id(), text, tone, "", ToastControls::Auto, 0, ToastWidget::Callbacks{});
}

DialogButton& trigger(UiContext& ui, WidgetId parent, const char* label, std::function<void()> action) {
  DialogAction a;
  a.id = label;
  a.label = label;
  DialogButton& b = ui.create<DialogButton>(parent, a);
  b.setOnActivate(std::move(action));
  return b;
}

// A small dialog surface with a title, description and two action buttons (static).
void dialogPreview(UiContext& ui, WidgetId parent) {
  DialogBox& c = cell(ui, parent, "dialog surface");
  OverlayHost& host = surface(ui, c.id(), OverlaySurface::Dialog);
  host.style().width = layout::Length::px(360);
  DialogContent& content = ui.create<DialogContent>(host.id(), nullptr);
  DialogBox& header = ui.create<DialogBox>(content.id());
  header.style().direction = layout::FlexDirection::Column;
  header.style().gapRow = 4.0;
  header.style().padding[layout::kLeft] = header.style().padding[layout::kRight] = 16.0;
  header.style().padding[layout::kTop] = 16.0;
  header.style().padding[layout::kBottom] = 8.0;
  ui.create<DialogText>(header.id(), "Delete layer", "dialog.title");
  ui.create<DialogText>(header.id(), "This removes the layer and everything inside it. You can undo it with Ctrl+Z.", "dialog.description");
  DialogBox& footer = ui.create<DialogBox>(content.id());
  footer.style().direction = layout::FlexDirection::Row;
  footer.style().justifyContent = layout::Justify::End;
  footer.style().gapColumn = 8.0;
  footer.style().padding[layout::kLeft] = footer.style().padding[layout::kRight] = 16.0;
  footer.style().padding[layout::kTop] = 12.0;
  footer.style().padding[layout::kBottom] = 16.0;
  DialogAction cancel;
  cancel.id = "cancel";
  cancel.label = "Cancel";
  DialogAction ok;
  ok.id = "delete";
  ok.label = "Delete";
  ok.kind = DialogActionKind::Danger;
  ui.create<DialogButton>(footer.id(), cancel);
  ui.create<DialogButton>(footer.id(), ok);
}

}  // namespace

void buildGalleryOverlays(UiContext& ui, WidgetId parent) {
  GalleryRoot& root = ui.create<GalleryRoot>(parent, ui);
  const WidgetId rootId = root.id();

  // ---- menus ----
  DialogBox& menus = section(ui, rootId, "Menu");
  menuPreview(ui, menus.id(), "context menu", sampleContextMenu(), 1, 0.0, "overlay");
  menuPreview(ui, menus.id(), "menu bar menu", fileLikeMenu(), 0);
  menuPreview(ui, menus.id(), "headings, empty heading hidden", headingMenu(), 2);
  menuPreview(ui, menus.id(), "idle row", oneRow(menuAction("a", "Idle row", "Ctrl+I")));
  menuPreview(ui, menus.id(), "hover row", oneRow(menuAction("a", "Hover row", "Ctrl+H")), 0);
  {
    MenuItemSpec disabled = menuAction("a", "Disabled row", "Ctrl+D");
    disabled.enabled = false;
    menuPreview(ui, menus.id(), "disabled row", oneRow(disabled));
    MenuItemSpec component = menuAction("a", "Component tone", "Ctrl+K");
    component.tone = MenuTone::Component;
    menuPreview(ui, menus.id(), "component tone", oneRow(component));
    menuPreview(ui, menus.id(), "component hover", oneRow(component), 0);
  }
  menuPreview(ui, menus.id(), "submenu row open", oneRow(menuSubmenu("Submenu", {menuAction("x", "X")})), 0);
  {
    MenuItemSpec described = menuAction("a", "Number", {}, "hash");
    described.description = "Sizes, spacing, opacity";
    MenuSpec s = oneRow(described);
    s.minWidth = 192.0;
    menuPreview(ui, menus.id(), "two-line row", s);
  }
  menuPreview(ui, menus.id(), "scrolling (40 rows, 160 px)", tallMenu(), 3, 160.0);

  DialogBox& live = box(ui, menus.id(), layout::FlexDirection::Column, 8.0);
  label(ui, live.id(), "live", LabelRole::Caption);
  MenuBar& bar = ui.create<MenuBar>(live.id());
  bar.addMenu("File", fileLikeMenu());
  MenuSpec edit;
  edit.items = {menuAction("undo", "Undo", "Ctrl+Z"), menuAction("redo", "Redo", "Ctrl+Shift+Z"), menuSeparator(), menuAction("copy", "Copy", "Ctrl+C")};
  bar.addMenu("Edit", edit);
  bar.addMenu("View", sampleContextMenu());
  const WidgetId contextTrigger = trigger(ui, live.id(), "Open context menu", {}).id();
  if (auto* t = ui.objectAs<DialogButton>(contextTrigger)) {
    UiContext* u = &ui;
    t->setOnActivate([u, rootId, contextTrigger]() {
      GalleryRoot* g = u->objectAs<GalleryRoot>(rootId);
      const layout::Rect r = u->absRect(contextTrigger);
      if (g != nullptr) g->menus.openContextMenu(sampleContextMenu(), r.x, static_cast<double>(r.bottom()) + 4.0);
    });
  }

  // ---- tooltips ----
  DialogBox& tips = section(ui, rootId, "Tooltip");
  tooltipPreview(ui, tips.id(), "plain, 26 px", TooltipInfo{"Flip horizontal", "", ""});
  tooltipPreview(ui, tips.id(), "shortcut in the text", TooltipInfo{"Pen (P)", "", ""});
  tooltipPreview(ui, tips.id(), "title and shortcut", TooltipInfo{"Duplicate", "Ctrl+D", ""});
  tooltipPreview(ui, tips.id(), "rich: description", TooltipInfo{"Undo", "Ctrl+Z", "Reverts the last change to the document and keeps the redo history so it can be restored later."});
  {
    DialogBox& c = cell(ui, tips.id(), "live (hover 0.4 s)");
    DialogButton& target = trigger(ui, c.id(), "Hover me", {});
    root.tooltips->set(target.id(), TooltipInfo{"Rich tooltip", "Ctrl+T", "Appears after 410 ms, follows the pointer and fades in over 100 ms."});
  }

  // ---- popover ----
  DialogBox& popovers = section(ui, rootId, "Popover");
  {
    DialogBox& c = cell(ui, popovers.id(), "surface (radius 8, shadow xl)");
    OverlayHost& host = surface(ui, c.id(), OverlaySurface::Popover);
    host.style().width = layout::Length::px(200);
    for (double& p : host.style().padding) p = 13.0;
    label(ui, host.id(), "Popover content", LabelRole::Title);
    label(ui, host.id(), "Padding is up to the caller.", LabelRole::Muted);
  }
  {
    DialogBox& c = cell(ui, popovers.id(), "live");
    DialogButton& button = trigger(ui, c.id(), "Open popover", {});
    UiContext* u = &ui;
    const WidgetId buttonId = button.id();
    button.setOnActivate([u, buttonId]() {
      PopoverOptions o;
      o.anchorWidget = buttonId;
      o.placement = Placement::BelowStart;
      o.padding = 12.0;
      o.width = 220.0;
      const PopoverHandle h = openPopover(*u, o);
      u->create<Label>(h.host, "Click outside or press Escape", LabelRole::Muted);
    });
  }

  // ---- dialog ----
  DialogBox& dialogs = section(ui, rootId, "Dialog");
  dialogPreview(ui, dialogs.id());
  {
    DialogBox& c = cell(ui, dialogs.id(), "live (modal, 50% scrim)");
    UiContext* u = &ui;
    const WidgetId anchor = c.id();
    trigger(ui, c.id(), "Open dialog", [u, anchor]() {
      DialogSpec spec;
      spec.title = "Delete layer";
      spec.description = "This removes the layer and everything inside it. You can undo it with Ctrl+Z.";
      spec.actions = {{"cancel", "Cancel", DialogActionKind::Neutral, false, true, true}, {"delete", "Delete", DialogActionKind::Danger, true, false, true}};
      spec.owner = anchor;
      openDialog(*u, spec);
    });
  }

  // ---- toasts ----
  DialogBox& toasts = section(ui, rootId, "Toast");
  toastPreview(ui, toasts.id(), "default", "Copied as node ID", ToastTone::Default);
  toastPreview(ui, toasts.id(), "warning", "Autosave is turned off", ToastTone::Warning);
  toastPreview(ui, toasts.id(), "error (copy, close)", "Failed to open file: unexpected token", ToastTone::Error);
  {
    DialogBox& c = cell(ui, toasts.id(), "live");
    DialogBox& row = box(ui, c.id(), layout::FlexDirection::Row, 8.0);
    UiContext* u = &ui;
    const auto show = [u, rootId](const char* text, ToastTone tone) {
      if (GalleryRoot* g = u->objectAs<GalleryRoot>(rootId)) {
        ToastSpec spec;
        spec.text = text;
        spec.tone = tone;
        g->toasts.show(spec);
      }
    };
    trigger(ui, row.id(), "Default toast", [show]() { show("Copied as node ID", ToastTone::Default); });
    trigger(ui, row.id(), "Warning toast", [show]() { show("Autosave is turned off", ToastTone::Warning); });
    trigger(ui, row.id(), "Error toast", [show]() { show("Failed to open file: unexpected token", ToastTone::Error); });
  }
}

}  // namespace r1ui::widgets
