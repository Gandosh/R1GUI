// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of PanelState and buildPanelModel: categories open by default (D23), case-insensitive
//   multi-word search over label, name, category, group and tooltip, forced expansion while searching,
//   restoring the collapse state when the search ends, the advanced toggle, the modified-only filter,
//   edit-condition visibility, the empty-selection model, and hostile search text.
// Callers: CTest (fast tier, no GPU).
#include <string>
#include <vector>

#include "Samples.h"
#include "TestSupport.h"
#include "r1ui/props/PanelState.h"

namespace {

using namespace r1ui::props;
using namespace samples;

const PanelCategory* find(const PanelModel& m, const std::string& name) {
  for (const PanelCategory& c : m.categories) {
    if (c.name == name) return &c;
  }
  return nullptr;
}

bool hasRow(const PropertyContext& ctx, const PanelCategory& c, const char* name) {
  for (const PanelRow& r : c.rows) {
    if (ctx.descriptor(r.row).name == name) return true;
  }
  return false;
}

void defaultsAndCollapse() {
  Material m;
  PropertyContext c;
  const Target t[] = {targetOf(m, materialSet())};
  c.setSelection(t);
  PanelState state;
  PanelModel model = buildPanelModel(c, state);
  R1_EXPECT(model.categories.size() == 3);  // Appearance, Texture, Object
  for (const PanelCategory& cat : model.categories) R1_EXPECT(!cat.collapsed);  // open by default (D23)
  R1_EXPECT(find(model, "Appearance") && find(model, "Appearance")->rows.size() == 3);
  // Texture: useTexture and the disabled-but-visible texture row; textureScale is hidden by its condition.
  const PanelCategory* texture = find(model, "Texture");
  R1_EXPECT(texture && hasRow(c, *texture, "useTexture") && hasRow(c, *texture, "texture") && !hasRow(c, *texture, "textureScale"));
  m.useTexture = true;
  model = buildPanelModel(c, state);
  R1_EXPECT(hasRow(c, *find(model, "Texture"), "textureScale"));

  R1_EXPECT(state.setCategoryCollapsed("Appearance", true));
  model = buildPanelModel(c, state);
  R1_EXPECT(find(model, "Appearance")->collapsed && !find(model, "Texture")->collapsed);
  R1_EXPECT(model.searchVisible);
}

void searching() {
  Material m;
  PropertyContext c;
  const Target t[] = {targetOf(m, materialSet())};
  c.setSelection(t);
  PanelState state;
  state.setCategoryCollapsed("Texture", true);
  state.setCategoryCollapsed("Object", true);

  state.setSearch("ROUGH");  // case-insensitive
  PanelModel model = buildPanelModel(c, state);
  R1_EXPECT(model.categories.size() == 1 && hasRow(c, model.categories[0], "roughness"));
  R1_EXPECT(!model.categories[0].collapsed);

  // Several words: each must match somewhere in the row's label, name, category, group or tooltip.
  state.setSearch("surface rough");  // "surface" is in the tooltip
  model = buildPanelModel(c, state);
  R1_EXPECT(model.totalRows == 1);
  state.setSearch("surface nonsense");
  model = buildPanelModel(c, state);
  R1_EXPECT(model.categories.empty() && model.totalRows == 0 && model.searchVisible);  // no match: an empty tree, not an error

  // A category name matches all of its rows; collapsed categories are forced open and advanced rows show.
  state.setSearch("object");
  model = buildPanelModel(c, state);
  R1_EXPECT(model.categories.size() == 1 && model.categories[0].name == "Object" && !model.categories[0].collapsed);
  R1_EXPECT(hasRow(c, model.categories[0], "secret"));  // advanced, forced visible while searching
  R1_EXPECT(!state.setCategoryCollapsed("Appearance", true));  // categories cannot be collapsed during a search

  // The stored name matches too (spec 09 rule 56).
  state.setSearch("usetexture");
  model = buildPanelModel(c, state);
  R1_EXPECT(model.totalRows == 1);

  // Clearing restores the collapse state from before the search (rule 59).
  state.setSearch("");
  model = buildPanelModel(c, state);
  R1_EXPECT(!state.searching() && find(model, "Texture")->collapsed && find(model, "Object")->collapsed && !find(model, "Appearance")->collapsed);

  // Hostile text: huge, invalid UTF-8, many words, only blanks.
  state.setSearch(std::string(100000, 'x'));
  R1_EXPECT(state.search().size() == kMaxSearchBytes);
  state.setSearch("\xFF\xFE\xC3");
  R1_EXPECT(buildPanelModel(c, state).categories.empty());
  state.setSearch("a b c d e f g h i j k l m n o p");
  R1_EXPECT(state.searchWords().size() == kMaxSearchWords);
  state.setSearch("   \t  ");
  R1_EXPECT(!state.searching());
}

void advancedAndModified() {
  Transform tr;
  PropertyContext c;
  const Target t[] = {targetOf(tr, transformSet())};
  c.setSelection(t);
  PanelState state;
  PanelModel model = buildPanelModel(c, state);
  const PanelCategory* object = find(model, "Object");
  R1_EXPECT(object && !hasRow(c, *object, "layer") && object->hiddenAdvanced == 1);
  state.setShowAdvanced(true);
  model = buildPanelModel(c, state);
  R1_EXPECT(hasRow(c, *find(model, "Object"), "layer") && find(model, "Object")->rows.back().advanced);

  // Modified only: just the rows whose reset affordance shows.
  state.setOnlyModified(true);
  R1_EXPECT(buildPanelModel(c, state).categories.empty());
  tr.scale = {2.0, 1.0, 1.0};
  model = buildPanelModel(c, state);
  R1_EXPECT(model.categories.size() == 1 && model.categories[0].anyModified && model.totalRows == 1);
  R1_EXPECT(find(model, "Transform")->anyModified);
}

void emptySelection() {
  PropertyContext c;
  PanelState state;
  PanelModel model = buildPanelModel(c, state);
  R1_EXPECT(model.categories.empty() && !model.searchVisible);  // rule 3/9: nothing selected, no search box
  state.setSearch("x");
  R1_EXPECT(buildPanelModel(c, state).searchVisible);  // text in the box keeps it visible
}

void modelEquality() {
  Light l;
  PropertyContext c;
  const Target t[] = {targetOf(l, lightSet())};
  c.setSelection(t);
  PanelState state;
  const PanelModel a = buildPanelModel(c, state);
  R1_EXPECT(a == buildPanelModel(c, state));
  state.setCategoryCollapsed("Light", true);
  R1_EXPECT(!(a == buildPanelModel(c, state)));
}

}  // namespace

int main() {
  defaultsAndCollapse();
  searching();
  advancedAndModified();
  emptySelection();
  modelEquality();
  return r1test::finish();
}
