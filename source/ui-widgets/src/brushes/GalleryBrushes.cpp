// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery page of the brush library group (GalleryBrushes.h).
// Invariants: only layout boxes, Labels and BrushLibraryPopup widgets are created; the page's model lives in
//   a widget of the page (created first, so it is destroyed after the popups that read it); the popups have
//   no hooks, so a click on a tile changes only what the popup itself shows.
// Callers: the gallery preview, the group's gallery tests.
#include "r1ui/widgets/brushes/GalleryBrushes.h"

#include <cctype>
#include <memory>
#include <string>
#include <vector>

#include "r1ui/commands/brushes/BrushLibraryModel.h"
#include "r1ui/widgets/brushes/BrushLibraryPopup.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/section/Section.h"

namespace r1ui::widgets {

namespace {

namespace cb = commands::brushes;
namespace layout = core::layout;
using core::tree::WidgetId;

constexpr double kGalleryPopupWidth = 600.0;
constexpr double kGalleryPopupHeight = 420.0;

// Holds the gallery's model for the life of the page.
class ModelHolder final : public WidgetObject {
 public:
  const char* typeName() const override { return "GalleryBrushesModel"; }
  void onAttached() override {
    style().width = layout::Length::px(0.0);
    style().height = layout::Length::px(0.0);
    style().flexShrink = 0.0;
  }
  cb::BrushLibraryModel model;
};

std::vector<cb::BrushInfo> sampleBrushes() {
  struct Row {
    const char* name;
    const char* category;
    const char* icon;
  };
  const Row rows[] = {{"Standard", "Sculpt", "palette"},   {"Smooth", "Smooth", "droplets"},     {"Snake Hook", "Move", "move-3d"}, {"Slash", "Cut", "scissors"},
                      {"Clay Buildup", "Sculpt", "layers"}, {"Clay", "Sculpt", "layers"},          {"Inflate", "Sculpt", "circle"},    {"Pinch", "Sculpt", "pen-tool"},
                      {"Move", "Move", "move-3d"},          {"Blob", "Sculpt", "circle"},          {"Flatten", "Surface", "square"},   {"Polish", "Surface", "sparkles"},
                      {"Hpolish", "Surface", "sparkles"},   {"Crease", "Sculpt", "pen-tool"},      {"Dam Standard", "Sculpt", "pen-tool"}, {"Layer", "Surface", "layers"},
                      {"Trim", "Cut", "scissors"},          {"Mask Pen", "Mask", "pen-tool"},      {"Nudge", "Move", "hand"},          {"Orb Cracks", "Surface", "shapes"},
                      {"Pipette", "Mask", "pipette"},       {"Paint", "Surface", "paint-bucket"},  {"Grab", "Move", "hand"},           {"Elastic", "Move", "move-horizontal"}};
  std::vector<cb::BrushInfo> list;
  for (const Row& row : rows) {
    cb::BrushInfo info;
    info.name = row.name;
    for (const char* c = row.name; *c != 0; ++c) info.id += *c == ' ' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
    info.category = row.category;
    info.icon = row.icon;
    info.description = std::string("Sample brush ") + row.name;
    list.push_back(std::move(info));
  }
  return list;
}

SectionBox& box(UiContext& ui, WidgetId parent, bool row, double gap) {
  SectionBox& b = ui.create<SectionBox>(parent);
  b.style().direction = row ? layout::FlexDirection::Row : layout::FlexDirection::Column;
  b.style().gapRow = gap;
  b.style().gapColumn = gap;
  b.style().flexShrink = 0.0;
  if (row) b.style().wrap = layout::FlexWrap::Wrap;
  return b;
}

BrushLibraryPopup& card(UiContext& ui, WidgetId parent, cb::BrushLibraryModel& model, const char* caption, commands::brushes::QueryMode mode = commands::brushes::QueryMode::TypeToPick) {
  SectionBox& column = box(ui, parent, false, 6.0);
  ui.create<Label>(column.id(), caption, LabelRole::Caption);
  BrushPopupOptions options;
  options.width = kGalleryPopupWidth;
  options.height = kGalleryPopupHeight;
  options.mode = mode;
  return ui.create<BrushLibraryPopup>(column.id(), model, BrushPopupHooks{}, options);
}

}  // namespace

void buildGalleryBrushes(UiContext& ui, WidgetId parent) {
  ModelHolder& holder = ui.create<ModelHolder>(parent);
  cb::BrushLibraryModel& model = holder.model;
  model.setBrushes(sampleBrushes());
  for (const char* id : {"smooth", "clay", "pinch"}) model.setFavourite(id, true);
  for (const char* id : {"trim", "blob", "smooth"}) model.noteUsed(id);
  model.setActiveId("clay");
  model.setUserLetter("blob", "x");

  SectionBox& page = box(ui, parent, false, 20.0);
  ui.create<Label>(page.id(), "Brush library: press the key, type a letter, press the marked second letter", LabelRole::Heading);

  SectionBox& first = box(ui, page.id(), true, 20.0);
  card(ui, first.id(), model, "Opened: Recent first, favourites starred, the active brush outlined, the first letter of each name on its badge");
  BrushLibraryPopup& typing = card(ui, first.id(), model, "After typing S: four brushes remain, each shows the letter that picks it");
  typing.setText("s");

  SectionBox& second = box(ui, page.id(), true, 20.0);
  BrushLibraryPopup& exact = card(ui, second.id(), model, "After typing CLAY: the exact name first, the longer name needs one more letter");
  exact.setText("clay");
  BrushLibraryPopup& search = card(ui, second.id(), model, "Tab switches to search anywhere in the name", commands::brushes::QueryMode::SearchAnywhere);
  search.setText("ish");

  SectionBox& third = box(ui, page.id(), true, 20.0);
  BrushLibraryPopup& assign = card(ui, third.id(), model, "Assign letter: shows how many brushes share the letter");
  assign.openAssign(*model.indexOfId("trim"));
  assign.previewAssign(U's');
  BrushLibraryPopup& menu = card(ui, third.id(), model, "Right click on a tile");
  menu.setHighlight(2);
  menu.showMenu(*model.indexOfId("clay-buildup"));

  SectionBox& fourth = box(ui, page.id(), true, 20.0);
  BrushLibraryPopup& none = card(ui, fourth.id(), model, "No brush starts with the typed letters");
  none.setText("zq");
  BrushLibraryPopup& filtered = card(ui, fourth.id(), model, "A category chip narrows the list");
  filtered.setCategory("Surface");
}

}  // namespace r1ui::widgets
