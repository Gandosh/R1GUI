// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GalleryApp, the Gallery mode: a page list at the left (a list-style TreeView) and the
//   selected page of the widget library's galleries (Buttons, Fields, Containers, Overlays,
//   Editors, Docking, Commands, Properties, Customize, Actions, Hotkeys, CustomMenus, Creator, Brushes) mounted inside a ScrollArea on the right. Each page shows every widget of its groups in
//   every state with captions, built by the widget library's own gallery functions.
// Why: slice 4.17, a state matrix of the whole toolkit that is live (hover, click, type, open the
//   popups) in the real window, next to the composed screen of the Widgets mode.
// Callers: PreviewApp, tests/preview. Calls: buildGalleryButtons, buildGalleryFields,
//   buildGalleryContainers, buildGalleryOverlays, buildGalleryEditors, buildGalleryDock,
//   buildGalleryCommands, buildGalleryProps, buildGalleryCustomize, buildGalleryActions, buildGalleryHotkeys, buildGalleryCustomMenus, buildGalleryCreator, buildGalleryBrushes.
// Lifetime: only the selected page exists; switching destroys the old page (its overlays are closed
//   first) and builds the new one, so a page always starts in its initial state.
#pragma once

#include <cstddef>

#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace preview {

class GalleryApp {
 public:
  static constexpr size_t kPageCount = 14;
  static const char* pageName(size_t index);

  // Builds the page list and page `first` as a child of `parent` (a flex container filling the window
  // below the title bar).
  GalleryApp(r1ui::widgets::UiContext& ui, r1ui::core::tree::WidgetId parent, size_t first = 0);
  GalleryApp(const GalleryApp&) = delete;
  GalleryApp& operator=(const GalleryApp&) = delete;

  // Replaces the page; false for an index out of range.
  bool selectPage(size_t index);
  size_t page() const { return page_; }
  r1ui::core::tree::WidgetId scrollArea() const { return scroll_; }
  r1ui::core::tree::WidgetId list() const { return list_; }

 private:
  r1ui::widgets::UiContext& ui_;
  r1ui::core::tree::WidgetId list_;
  r1ui::core::tree::WidgetId scroll_;
  size_t page_ = 0;
};

}  // namespace preview
