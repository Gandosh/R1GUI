// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for PropertyPanel: the generated sections and rows for one and two objects, the
//   reset-category action, collapse state kept in the PanelState (open by default), the live search
//   filter and the restore of the collapse state when it is cleared, the advanced toggle, edit-condition
//   rows appearing and disappearing, live refresh by change notices coalesced into one flush, rebuild on
//   selection change, undo/redo refreshing the panel, deferral of a rebuild during a scrub, the empty
//   and no-match notices, and teardown with timers pending.
// Callers: CTest (props fast, no GPU; paint is checked on a recording Painter).
#include <cmath>
#include <string>
#include <vector>

#include "../../ui-props/support/Samples.h"
#include "../textinput/FieldRig.h"
#include "r1ui/widgets/iconbutton/IconButton.h"
#include "r1ui/widgets/numberfield/NumberField.h"
#include "r1ui/widgets/props/PropControls.h"
#include "r1ui/widgets/props/PropertyPanel.h"
#include "r1ui/widgets/scroll/ScrollArea.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1ui::widgets;
using namespace samples;
namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;
using events::Key;
using r1ui::core::tree::WidgetId;

struct Rig {
  r1test::FieldRig ui{300, 700};
  PropertyContext context;
  PanelState state;
  PropertyPanel* panel = nullptr;
  size_t row(const char* name) const { return context.findRow(name).value_or(static_cast<size_t>(-1)); }
  void mount() {
    ui.ui.rootStyle().alignItems = layout::Align::Stretch;
    ui.ui.rootStyle().padding[0] = ui.ui.rootStyle().padding[1] = 0.0;
    panel = &ui.ui.create<PropertyPanel>(ui.ui.root(), context, state);
    panel->style().width = layout::Length::px(258);
    ui.layout();
  }
  // Lets the zero-delay refresh timer fire, as the host loop does.
  void tick() {
    ui.ui.setTime(ui.ui.now() + 20);
    ui.ui.tick();
    ui.layout();
  }
};

std::vector<Target> targetsOf(std::vector<Material>& m) {
  std::vector<Target> t;
  for (Material& x : m) t.push_back(targetOf(x, materialSet()));
  return t;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void clickWidget(Rig& rig, WidgetId id) {
  const layout::Rect r = rig.ui.ui.absRect(id);
  rig.ui.click(r.x + r.w * 0.5, r.y + r.h * 0.5);
}

// ---- structure ---------------------------------------------------------------------------------------------

void generatedStructure() {
  Rig rig;
  std::vector<Material> mats(2);
  mats[0].roughness = 0.2;
  mats[1].roughness = 0.8;
  mats[1].useTexture = true;
  rig.context.setSelection(targetsOf(mats));
  rig.mount();
  PropertyPanel& panel = *rig.panel;

  R1_EXPECT(panel.sectionCount() == 3);
  R1_EXPECT(panel.section(0)->title() == "Appearance" && panel.section(1)->title() == "Texture" && panel.section(2)->title() == "Object");
  R1_EXPECT(panel.section(0) == panel.sectionNamed("Appearance") && panel.sectionNamed("Nope") == nullptr);
  for (size_t i = 0; i < 3; ++i) R1_EXPECT(!panel.section(i)->collapsed());  // open by default (D23)
  R1_EXPECT(panel.searchVisible() && panel.searchBox() != nullptr && panel.searchBox()->attached());

  // Rows: the mixed roughness shows "Mixed" on its number field; both texture rows are present (one object uses it).
  const PropertyRowView* rough = panel.rowView(rig.row("roughness"));
  R1_EXPECT(rough != nullptr && rough->number(0)->hasState(StateFlag::kMixed) && rough->slider()->hasState(StateFlag::kMixed));
  R1_EXPECT(panel.rowView(rig.row("textureScale")) != nullptr && panel.rowView(rig.row("texture")) != nullptr);
  R1_EXPECT(panel.rowView(rig.row("secret")) == nullptr);  // not a Material property of this panel's model: advanced, hidden
  R1_EXPECT(panel.rowView(9999) == nullptr);

  // Widths: the section content is 234 px wide in a 258 px panel (docs/spec/widgets.md 3).
  const layout::Rect rowRect = rig.ui.ui.absRect(rough->id());
  R1_EXPECT(std::fabs(rowRect.w - 234.0) < 0.5);
  R1_EXPECT(rig.ui.paint() > 0);
  R1_EXPECT(!panel.dirty());
}

void vectorFieldWidths() {
  Rig rig;
  Transform a;
  const Target t[] = {targetOf(a, transformSet())};
  rig.context.setSelection(t);
  rig.mount();
  const PropertyRowView* pos = rig.panel->rowView(rig.row("position"));
  const layout::Rect r0 = rig.ui.ui.absRect(pos->number(0)->id());
  const layout::Rect r1 = rig.ui.ui.absRect(pos->number(1)->id());
  const layout::Rect r2 = rig.ui.ui.absRect(pos->number(2)->id());
  // Three fields of one row: (234 - 2 x 6) / 3 = 74 px each.
  R1_EXPECT(std::fabs(r0.w - 74.0) < 0.6 && std::fabs(r1.w - 74.0) < 0.6 && std::fabs(r2.w - 74.0) < 0.6);
  R1_EXPECT(std::fabs((r1.x - (r0.x + r0.w)) - 6.0) < 0.6);
}

// ---- collapse, search, advanced ---------------------------------------------------------------------------------

void collapseAndSearch() {
  Rig rig;
  std::vector<Material> mats(1);
  rig.context.setSelection(targetsOf(mats));
  rig.mount();
  PropertyPanel& panel = *rig.panel;

  // Clicking a header collapses the category; the PanelState remembers it.
  WidgetId header = panel.section(0)->header();
  clickWidget(rig, header);
  R1_EXPECT(panel.section(0)->collapsed() && rig.state.collapsed("Appearance"));
  clickWidget(rig, panel.section(1)->header());
  R1_EXPECT(rig.state.collapsed("Texture"));
  R1_EXPECT(!panel.dirty());  // a user toggle does not rebuild anything

  // Searching: case-insensitive, live (no tick needed), forced open and filtered.
  panel.setSearchText("ROUGH");
  R1_EXPECT(panel.sectionCount() == 1 && panel.section(0)->title() == "Appearance" && !panel.section(0)->collapsed());
  R1_EXPECT(panel.rowView(rig.row("roughness")) != nullptr && panel.rowView(rig.row("color")) == nullptr);
  R1_EXPECT(panel.searchBox()->text() == "ROUGH");
  panel.setSearchText("texture");
  R1_EXPECT(panel.sectionCount() == 1 && panel.section(0)->title() == "Texture");
  R1_EXPECT(!panel.section(0)->collapsed());
  panel.setSearchText("nothing matches this");
  R1_EXPECT(panel.sectionCount() == 0 && contains(panel.describe(), "notice \"No matching properties\""));

  // Typing in the box filters on every keystroke.
  panel.setSearchText("");
  clickWidget(rig, panel.searchBox()->id());
  rig.ui.type("rough");
  R1_EXPECT(panel.sectionCount() == 1 && rig.state.search() == "rough");
  rig.ui.ctrl('A');
  rig.ui.key(Key::Backspace);
  R1_EXPECT(panel.sectionCount() == 3 - 0 && rig.state.search().empty());
  // Clearing restored the collapse state from before the search (rule 59).
  R1_EXPECT(panel.sectionNamed("Appearance")->collapsed() && panel.sectionNamed("Texture")->collapsed() && !panel.sectionNamed("Object")->collapsed());
}

void advancedToggle() {
  Rig rig;
  Transform a;
  const Target t[] = {targetOf(a, transformSet())};
  rig.context.setSelection(t);
  rig.mount();
  PropertyPanel& panel = *rig.panel;
  R1_EXPECT(panel.rowView(rig.row("layer")) == nullptr);
  clickWidget(rig, panel.advancedButton()->id());
  R1_EXPECT(rig.state.showAdvanced() && panel.advancedButton()->active() && panel.rowView(rig.row("layer")) != nullptr);
  clickWidget(rig, panel.advancedButton()->id());
  R1_EXPECT(!rig.state.showAdvanced() && panel.rowView(rig.row("layer")) == nullptr);
}

// ---- edit conditions -----------------------------------------------------------------------------------------------

void conditionalRows() {
  Rig rig;
  Material m;
  const Target t[] = {targetOf(m, materialSet())};
  rig.context.setSelection(t);
  rig.mount();
  PropertyPanel& panel = *rig.panel;
  R1_EXPECT(panel.rowView(rig.row("textureScale")) == nullptr);                  // hidden while the condition is false
  R1_EXPECT(!panel.rowView(rig.row("texture"))->text()->enabled());               // disabled, name still visible
  // Toggling the controlling switch shows the hidden row and enables the other (rule 67).
  rig.context.setValue(rig.row("useTexture"), Value(true));
  R1_EXPECT(panel.dirty());
  rig.tick();
  R1_EXPECT(panel.rowView(rig.row("textureScale")) != nullptr && panel.rowView(rig.row("texture"))->text()->enabled());
  rig.context.undo().undo();
  rig.tick();
  R1_EXPECT(panel.rowView(rig.row("textureScale")) == nullptr && !panel.rowView(rig.row("texture"))->text()->enabled());
}

// ---- live refresh ------------------------------------------------------------------------------------------------

void liveRefreshIsCoalesced() {
  Rig rig;
  std::vector<Material> mats(2);
  rig.context.setSelection(targetsOf(mats));
  rig.mount();
  PropertyPanel& panel = *rig.panel;
  const PropertyRowView* rough = panel.rowView(rig.row("roughness"));
  R1_EXPECT(rough->number(0)->value() == 0.5 && !panel.dirty());
  R1_EXPECT(!rig.ui.ui.msUntilTick().has_value());  // idle: no timer armed, no frames wanted

  // A script changes the value; the host reports it. Many notices before the next tick cost one flush.
  mats[0].roughness = 0.9;
  mats[1].roughness = 0.1;
  for (int i = 0; i < 50; ++i) rig.context.notifyExternalChange();
  R1_EXPECT(panel.dirty() && rig.ui.ui.msUntilTick().has_value() && *rig.ui.ui.msUntilTick() == 0);
  R1_EXPECT(rough->number(0)->value() == 0.5);  // not applied yet
  rig.tick();
  R1_EXPECT(!panel.dirty() && rough->number(0)->hasState(StateFlag::kMixed) && rough->slider()->hasState(StateFlag::kMixed));
  R1_EXPECT(rough->number(0)->value() == 0.9);
  R1_EXPECT(!rig.ui.ui.msUntilTick().has_value());  // nothing stays armed once settled

  // Edits through the context refresh on the next tick; undo and redo do as well (rule 7).
  rig.context.setValue(rig.row("roughness"), Value(0.3));
  rig.tick();
  R1_EXPECT(!rough->number(0)->hasState(StateFlag::kMixed) && rough->number(0)->value() == 0.3 && rough->resetButton()->shown());
  rig.context.undo().undo();
  rig.tick();
  R1_EXPECT(rough->number(0)->hasState(StateFlag::kMixed) && rough->number(0)->value() == 0.9);
  rig.context.undo().redo();
  rig.tick();
  R1_EXPECT(rough->number(0)->value() == 0.3);
}

void selectionChangeRebuilds() {
  Rig rig;
  Material m;
  Transform tr;
  const Target first[] = {targetOf(m, materialSet())};
  rig.context.setSelection(first);
  rig.mount();
  PropertyPanel& panel = *rig.panel;
  const WidgetId oldRow = panel.rowView(rig.row("roughness"))->id();
  R1_EXPECT(panel.sectionCount() == 3);
  const Target second[] = {targetOf(tr, transformSet())};
  rig.context.setSelection(second);
  R1_EXPECT(panel.dirty());
  rig.tick();
  R1_EXPECT(panel.sectionCount() == 2 && panel.section(0)->title() == "Transform");
  R1_EXPECT(!rig.ui.ui.alive(oldRow));
  R1_EXPECT(panel.rowView(rig.row("position")) != nullptr);

  // Nothing selected: a notice, and the search box is hidden (rules 3 and 9).
  rig.context.setSelection({});
  rig.tick();
  R1_EXPECT(panel.sectionCount() == 0 && !panel.searchVisible() && contains(panel.describe(), "notice \"Nothing selected\""));
  R1_EXPECT(rig.ui.paint() > 0);
  rig.context.setSelection(first);
  rig.tick();
  R1_EXPECT(panel.sectionCount() == 3 && panel.searchVisible());
}

// ---- editing through the panel -----------------------------------------------------------------------------------

void resetCategoryAction() {
  Rig rig;
  std::vector<Material> mats(2);
  mats[0].roughness = 0.9;
  mats[1].shading = Shading::Toon;
  rig.context.setSelection(targetsOf(mats));
  rig.mount();
  PropertyPanel& panel = *rig.panel;
  const std::string text = panel.describe();
  R1_EXPECT(contains(text, "section \"Appearance\" open reset=enabled") && contains(text, "section \"Texture\" open reset=disabled"));
  const layout::Rect before = rig.ui.ui.absRect(panel.section(0)->content());
  // The action is the last child of the header.
  WidgetId action;
  rig.ui.ui.tree().forEachDescendant(panel.section(0)->header(), [&](WidgetId id) {
    if (dynamic_cast<ActionButton*>(rig.ui.ui.object(id)) != nullptr) action = id;
  });
  R1_EXPECT(action.valid() && rig.ui.ui.object(action)->tooltipText() == "Reset category to defaults");
  clickWidget(rig, action);
  R1_EXPECT(mats[0].roughness == 0.5 && mats[1].shading == Shading::Smooth);
  R1_EXPECT(rig.context.undo().undoCount() == 1 && rig.context.undo().undoLabel() == "Reset Appearance");
  rig.tick();
  R1_EXPECT(contains(panel.describe(), "section \"Appearance\" open reset=disabled"));
  R1_EXPECT(std::fabs(rig.ui.ui.absRect(panel.section(0)->content()).h - before.h) < 0.5);  // no layout change
  rig.context.undo().undo();
  rig.tick();
  R1_EXPECT(mats[0].roughness == 0.9 && contains(panel.describe(), "section \"Appearance\" open reset=enabled"));
}

void scrubThroughPanel() {
  Rig rig;
  Transform a;
  a.position = {10.0, 0.0, 0.0};
  const Target t[] = {targetOf(a, transformSet())};
  rig.context.setSelection(t);
  rig.mount();
  PropertyPanel& panel = *rig.panel;
  NumberField* x = panel.rowView(rig.row("position"))->number(0);
  const layout::Rect r = rig.ui.ui.absRect(x->id());
  rig.ui.ui.pointerMove(r.x + 30, r.y + 13);
  rig.ui.ui.pointerDown(r.x + 30, r.y + 13);
  for (int i = 1; i <= 20; ++i) rig.ui.ui.pointerMove(r.x + 30 + 2 * i, r.y + 13);
  R1_EXPECT(x->scrubbing() && a.position.x > 10.0);
  rig.tick();  // refreshes arrive mid-scrub; the field under the pointer must survive
  R1_EXPECT(rig.ui.ui.alive(x->id()) && x->scrubbing());
  rig.ui.ui.pointerUp(r.x + 70, r.y + 13);
  R1_EXPECT(rig.context.undo().undoCount() == 1);
  rig.tick();
  R1_EXPECT(panel.rowView(rig.row("position"))->resetButton()->shown());
}

void structuralChangeWaitsForGesture() {
  // A numeric property that controls the visibility of another: scrubbing it must not rebuild the rows
  // (and destroy the scrubbing field) until the gesture ends.
  struct Gate {
    double level = 0.0;
    double extra = 1.0;
  };
  PropertySet<Gate> set("Gate");
  set.category("G");
  set.add("level", "Level", &Gate::level).range(0.0, 1000.0);
  set.add("extra", "Extra", &Gate::extra).visibleWhen("level", CompareOp::Greater, 50);
  Gate g;
  Rig rig;
  const Target t[] = {targetOf(g, set)};
  rig.context.setSelection(t);
  rig.mount();
  PropertyPanel& panel = *rig.panel;
  R1_EXPECT(panel.rowView(rig.row("extra")) == nullptr);
  NumberField* level = panel.rowView(rig.row("level"))->number(0);
  const WidgetId levelId = level->id();
  const layout::Rect r = rig.ui.ui.absRect(levelId);
  rig.ui.ui.pointerMove(r.x + 30, r.y + 13);
  rig.ui.ui.pointerDown(r.x + 30, r.y + 13);
  for (int i = 1; i <= 30; ++i) rig.ui.ui.pointerMove(r.x + 30 + 4 * i, r.y + 13);
  R1_EXPECT(g.level > 50.0 && level->scrubbing());
  rig.tick();
  R1_EXPECT(rig.ui.ui.alive(levelId) && level->scrubbing() && panel.rowView(rig.row("extra")) == nullptr);  // deferred
  rig.ui.ui.pointerUp(r.x + 150, r.y + 13);
  rig.tick();
  R1_EXPECT(panel.rowView(rig.row("extra")) != nullptr);  // appears once the gesture ended
}

// ---- teardown ---------------------------------------------------------------------------------------------------

void teardown() {
  Material m;
  const Target t[] = {targetOf(m, materialSet())};
  Rig rig;
  rig.context.setSelection(t);
  rig.mount();
  rig.context.notifyExternalChange();  // a timer is pending
  R1_EXPECT(rig.context.notifier().listenerCount() >= 1);
  const size_t listeners = rig.context.notifier().listenerCount();
  rig.ui.ui.destroy(rig.panel->id());
  R1_EXPECT(rig.context.notifier().listenerCount() == listeners - 1);
  R1_EXPECT(!rig.ui.ui.msUntilTick().has_value());  // the pending timer was cancelled with the panel
  rig.context.notifyExternalChange();               // notices after the panel is gone are harmless
  rig.tick();
}

}  // namespace

int main() try {
  generatedStructure();
  vectorFieldWidths();
  collapseAndSearch();
  advancedToggle();
  conditionalRows();
  liveRefreshIsCoalesced();
  selectionChangeRebuilds();
  resetCategoryAction();
  scrubThroughPanel();
  structuralChangeWaitsForGesture();
  teardown();
  return r1test::finish();
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}
