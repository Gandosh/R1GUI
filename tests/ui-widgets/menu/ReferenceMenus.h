// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the menu contents of the reference captures (File, Edit and View menus, the canvas context
//   menu with its Copy/Paste as submenu, the one-item page menu, the toolbar flyout and the "add
//   variable" menu with two-line items), written as MenuSpec so tests render them at the positions of
//   the reference screens.
// Why: the visual tests of menus, and the gallery of this group, need the same realistic content;
//   one header keeps the item lists, icons and shortcut texts next to each other and in one place.
// Callers: tests/ui-widgets/menu/*_test.cpp. Texts and shortcuts are those visible in the captured
//   screens (docs/spec/widgets.md 2.9).
#pragma once

#include "r1ui/widgets/menu/MenuModel.h"

namespace r1test::menus {

using namespace r1ui::widgets;

inline MenuItemSpec disabled(MenuItemSpec spec) {
  spec.enabled = false;
  return spec;
}

inline MenuItemSpec component(MenuItemSpec spec) {
  spec.tone = MenuTone::Component;
  return spec;
}

inline MenuSpec contextMenu() {
  MenuSpec m;
  m.minWidth = 224.0;
  m.items = {
      menuAction("copy", "Copy", "Ctrl+C"),
      menuAction("cut", "Cut", "Ctrl+X"),
      menuAction("paste-here", "Paste here", "Ctrl+V"),
      menuAction("paste-replace", "Paste to replace"),
      menuAction("duplicate", "Duplicate", "Ctrl+D"),
      menuAction("delete", "Delete", "\xE2\x8C\xAB"),
      menuSeparator(),
      menuAction("forward", "Bring forward", "Alt+]"),
      menuAction("backward", "Send backward", "Alt+["),
      menuAction("front", "Bring to front", "Alt+Shift+]"),
      menuAction("back", "Send to back", "Alt+Shift+["),
      menuSeparator(),
      disabled(menuAction("group", "Group selection", "Ctrl+G")),
      menuAction("frame", "Frame selection", "Ctrl+Alt+G"),
      menuAction("autolayout", "Add auto layout", "Shift+A"),
      menuAction("mask", "Use as mask", "Ctrl+Alt+M"),
      menuAction("flatten", "Flatten", "Alt+Shift+F", "list-collapse"),
      disabled(menuAction("outline-text", "Outline text", "", "type-outline")),
      disabled(menuAction("outline-stroke", "Outline stroke", "", "spline")),
      menuSeparator(),
      component(menuAction("component", "Create component", "Ctrl+Alt+K")),
      menuSeparator(),
      menuAction("show", "Show/Hide", "Ctrl+Shift+H"),
      menuAction("lock", "Lock/Unlock", "Ctrl+Shift+L"),
      menuSeparator(),
      menuAction("flip-h", "Flip horizontal", "Shift+H"),
      menuAction("flip-v", "Flip vertical", "Shift+V"),
      menuSeparator(),
      menuSubmenu("Copy/Paste as",
                  {menuAction("copy-text", "Copy as text"), menuAction("copy-svg", "Copy as SVG"),
                   menuAction("copy-png", "Copy as PNG", "Ctrl+Shift+C"), menuAction("copy-jsx", "Copy as JSX"),
                   menuAction("copy-id", "Copy node ID"), menuAction("copy-xpath", "Copy XPath")}),
  };
  return m;
}

inline MenuSpec fileMenu() {
  MenuSpec m;
  m.items = {
      menuAction("new", "New", "Ctrl+N"),
      menuAction("open", "Open...", "Ctrl+O"),
      menuSeparator(),
      menuAction("save", "Save", "Ctrl+S"),
      menuAction("save-as", "Save as...", "Ctrl+Shift+S"),
      menuSeparator(),
      menuSubmenu("Export selection...", {menuAction("png", "PNG"), menuAction("svg", "SVG"), menuAction("fig", ".fig")}),
      menuSeparator(),
      menuAction("autosave", "Auto-save to local file"),
      menuAction("close", "Close tab", "Ctrl+W"),
  };
  return m;
}

inline MenuSpec editMenu() {
  MenuSpec m;
  m.items = {
      menuAction("undo", "Undo", "Ctrl+Z"),
      disabled(menuAction("redo", "Redo", "Ctrl+Shift+Z")),
      menuSeparator(),
      menuAction("copy", "Copy", "Ctrl+C"),
      menuAction("cut", "Cut", "Ctrl+X"),
      menuAction("paste", "Paste", "Ctrl+V"),
      menuAction("paste-replace", "Paste to replace"),
      menuAction("duplicate", "Duplicate", "Ctrl+D"),
      menuAction("delete", "Delete", "\xE2\x8C\xAB"),
      menuSeparator(),
      menuAction("select-all", "Select all", "Ctrl+A"),
  };
  return m;
}

inline MenuSpec viewMenu() {
  MenuSpec m;
  m.items = {
      menuAction("zoom-100", "Zoom to 100%"),
      menuAction("zoom-fit", "Zoom to fit"),
      menuAction("zoom-selection", "Zoom to selection"),
      menuAction("zoom-in", "Zoom in", "Ctrl+="),
      menuAction("zoom-out", "Zoom out", "Ctrl+-"),
      menuSeparator(),
      menuSubmenu("Theme", {menuRadio("light", "Light", false), menuRadio("dark", "Dark", true)}),
      menuSubmenu("Language", {menuRadio("en", "English", true)}),
      menuSeparator(),
      menuAction("toggle-ui", "Toggle UI", "Ctrl+\\"),
      menuCheck("profiler", "Performance profiler", false),
  };
  return m;
}

// The page row menu with only one page: Delete is disabled.
inline MenuSpec pageMenu() {
  MenuSpec m;
  m.items = {disabled(menuAction("delete-page", "Delete", "", "trash-2"))};
  return m;
}

inline MenuSpec flyoutMenu() {
  MenuSpec m;
  m.look.iconSize = 16.0;  // measured: 16 px icons whose label starts 20 px after them
  m.look.iconGap = 20.0;
  m.items = {menuAction("frame", "Frame", "F", "frame"), menuAction("section", "Section", "S", "layout-grid")};
  return m;
}

inline MenuItemSpec described(std::string id, std::string title, std::string description, std::string icon) {
  MenuItemSpec spec = menuAction(std::move(id), std::move(title), {}, std::move(icon));
  spec.description = std::move(description);
  return spec;
}

inline MenuSpec addVariableMenu() {
  MenuSpec m;
  m.items = {described("color", "Color", "Paint values", "palette"), described("number", "Number", "Sizes, spacing, opacity", "hash"),
             described("text", "Text", "Copy and labels", "type"), described("boolean", "Boolean", "True or false", "toggle-left")};
  return m;
}

}  // namespace r1test::menus
