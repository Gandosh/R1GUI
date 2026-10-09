// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GalleryApp.h.
// Invariants: the page list's selection and page_ agree; after selectPage the scroll area is back at
//   the top and holds exactly one page container.
// Callers: PreviewApp, tests/preview.
#include "GalleryApp.h"

#include <memory>
#include <vector>

#include "ComposedUtil.h"
#include "PreviewParts.h"
#include "r1ui/widgets/button/GalleryButtons.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/menu/GalleryOverlays.h"
#include "r1ui/widgets/scroll/ScrollArea.h"
#include "r1ui/widgets/section/GalleryContainers.h"
#include "r1ui/widgets/textinput/GalleryFields.h"
#include "r1ui/widgets/thumbnailgrid/GalleryEditors.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace preview {

namespace layout = r1ui::core::layout;
using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;

namespace {

constexpr const char* kPageNames[GalleryApp::kPageCount] = {"Buttons", "Fields", "Containers", "Overlays", "Editors"};
constexpr const char* kPageIcons[GalleryApp::kPageCount] = {"square", "text-cursor-input", "layout-panel-top", "layers", "palette"};
constexpr double kListWidth = 200.0;

// The editors page has no page container of its own: a padded column, children kept at their size.
void buildEditorsPage(UiContext& ui, WidgetId parent) {
  SectionBox& page = build::flex(ui, parent, false, 16);
  build::pad(page.style(), 16, 16, 16, 16);
  page.style().alignItems = layout::Align::Start;
  buildGalleryEditors(ui, page.id());
}

void buildPage(UiContext& ui, WidgetId parent, size_t index) {
  switch (index) {
    case 0: buildGalleryButtons(ui, parent); break;
    case 1: buildGalleryFields(ui, parent); break;
    case 2: buildGalleryContainers(ui, parent); break;
    case 3: buildGalleryOverlays(ui, parent); break;
    default: buildEditorsPage(ui, parent); break;
  }
}

}  // namespace

const char* GalleryApp::pageName(size_t index) { return index < kPageCount ? kPageNames[index] : ""; }

GalleryApp::GalleryApp(UiContext& ui, WidgetId parent, size_t first) : ui_(ui) {
  Surface& side = ui_.create<Surface>(parent, "panel", Surface::kRight);
  side.style().direction = layout::FlexDirection::Column;
  build::fixed(side.style(), kListWidth, 0);
  side.style().height = layout::Length::autoValue();
  build::pad(side.style(), 8, 12, 8, 8);
  side.style().gapRow = 8;
  ui_.create<Label>(side.id(), "WIDGET GALLERY", LabelRole::Caption);

  auto model = std::make_shared<SimpleTreeModel>();
  for (size_t i = 0; i < kPageCount; ++i) model->add(kTreeRoot, i + 1, kPageNames[i], kPageIcons[i]);
  TreeView& list = ui_.create<TreeView>(side.id());
  list.setAppearance(TreeAppearance::List);
  list.setSelectionMode(TreeSelectionMode::Single);
  list.setModel(model);
  build::grow(list.style());
  list_ = list.id();
  list.setOnSelectionChanged([this](TreeView& tree) {
    const std::vector<NodeId> selection = tree.selection();
    if (!selection.empty()) selectPage(static_cast<size_t>(selection.front() - 1));
  });

  Surface& backdrop = ui_.create<Surface>(parent, "panel");
  backdrop.style().direction = layout::FlexDirection::Column;
  build::grow(backdrop.style());
  ScrollArea& scroll = ui_.create<ScrollArea>(backdrop.id());
  build::grow(scroll.style());
  scroll_ = scroll.id();
  selectPage(first < kPageCount ? first : 0);
}

bool GalleryApp::selectPage(size_t index) {
  if (index >= kPageCount) return false;
  ScrollArea* scroll = ui_.objectAs<ScrollArea>(scroll_);
  if (scroll == nullptr) return false;
  if (index == page_ && ui_.tree().firstChild(scroll->content()).valid()) return true;
  ui_.overlays().closeAll();
  std::vector<WidgetId> old;
  for (WidgetId c = ui_.tree().firstChild(scroll->content()); c.valid(); c = ui_.tree().nextSibling(c)) old.push_back(c);
  for (const WidgetId id : old) ui_.destroy(id);
  page_ = index;
  scroll->scrollTo(0.0, 0.0);
  buildPage(ui_, scroll->content(), index);
  if (TreeView* list = ui_.objectAs<TreeView>(list_)) list->select(index + 1);
  return true;
}

}  // namespace preview
