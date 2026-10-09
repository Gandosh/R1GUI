// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for PropertyRowView, the widget generated for one property: the control chosen for
//   each kind, mixed display, the reset affordance (space reserved, shown only when the value differs),
//   edit conditions (disabled rows), read-only and password rows, typed edits with units, the
//   "Mixed + 5" relative edit, scrub = one undo step and Escape restoring it, the slider, colour
//   chip/hex/alpha, enum and bool controls, binding display (bound, broken) and the variable menu, and
//   hostile cases (stale row after the selection was cleared, edits with no selection).
// Callers: CTest (props fast, no GPU; paint is checked on a recording Painter).
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "../../ui-props/support/Samples.h"
#include "../textinput/FieldRig.h"
#include "r1ui/widgets/checkbox/Checkbox.h"
#include "r1ui/widgets/numberfield/NumberField.h"
#include "r1ui/widgets/props/PropControls.h"
#include "r1ui/widgets/props/PropertyPanel.h"
#include "r1ui/widgets/props/PropertyRow.h"
#include "r1ui/widgets/select/Select.h"
#include "r1ui/widgets/switch/Switch.h"
#include "r1ui/widgets/textinput/TextInput.h"
#include "r1ui/widgets/toolbar/FlyoutList.h"

namespace {

using namespace r1ui::widgets;
using namespace samples;
namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;
using events::Key;
using r1ui::core::tree::WidgetId;

size_t rowOf(const PropertyContext& c, const char* name) { return c.findRow(name).value_or(static_cast<size_t>(-1)); }

PropertyRowView& makeRow(r1test::FieldRig& rig, PropertyContext& ctx, const char* name, PropertyRowOptions options = {}) {
  PropertyRowView& row = rig.ui.create<PropertyRowView>(rig.ui.root(), ctx, rowOf(ctx, name), options);
  row.style().width = layout::Length::px(234);
  rig.layout();
  return row;
}

layout::Rect rectOf(r1test::FieldRig& rig, WidgetId id) { return rig.ui.absRect(id); }

void clickWidget(r1test::FieldRig& rig, WidgetId id) {
  const layout::Rect r = rectOf(rig, id);
  rig.click(r.x + r.w * 0.5, r.y + r.h * 0.5);
}

class Variables final : public BindingProvider {
 public:
  std::map<std::string, Value> table;
  std::optional<Value> resolve(std::string_view s) const override {
    const auto it = table.find(std::string(s));
    return it == table.end() ? std::nullopt : std::optional<Value>(it->second);
  }
  std::vector<VariableInfo> variables() const override {
    std::vector<VariableInfo> out;
    for (const auto& [name, value] : table) out.push_back({name, ValueKind::Double, {}});
    return out;
  }
};

// ---- vectors, mixed values, typed edits and scrubs ---------------------------------------------------------

void vectorRow() {
  r1test::FieldRig rig;
  Transform a, b;
  a.position = {1.0, 2.0, 3.0};
  b.position = {1.0, 7.0, 3.0};
  PropertyContext ctx;
  const Target t[] = {targetOf(a, transformSet()), targetOf(b, transformSet())};
  ctx.setSelection(t);
  PropertyRowView& row = makeRow(rig, ctx, "position");
  R1_EXPECT(row.kind() == ValueKind::Vec3 && row.numberCount() == 3);
  NumberField* x = row.number(0);
  NumberField* y = row.number(1);
  R1_EXPECT(x != nullptr && y != nullptr && row.number(2) != nullptr && row.number(3) == nullptr);
  R1_EXPECT(!x->hasState(StateFlag::kMixed) && y->hasState(StateFlag::kMixed) && !row.number(2)->hasState(StateFlag::kMixed));
  R1_EXPECT(x->value() == 1.0 && x->minimum() == -1000.0 && x->maximum() == 1000.0);
  // Three fields share the row: equal widths, same line.
  const layout::Rect rx = rectOf(rig, x->id()), ry = rectOf(rig, y->id());
  R1_EXPECT(std::fabs(rx.w - ry.w) < 1.0 && std::fabs(rx.y - ry.y) < 1.0 && ry.x > rx.x + rx.w);

  // Typed centimetres reach the stored metres; the object that disagreed on Y keeps its own value.
  clickWidget(rig, x->id());
  R1_EXPECT(x->editing());
  rig.ctrl('A');
  rig.type("250cm");
  rig.key(Key::Enter);
  R1_EXPECT(std::fabs(a.position.x - 2.5) < 1e-12 && std::fabs(b.position.x - 2.5) < 1e-12 && a.position.y == 2.0 && b.position.y == 7.0);
  R1_EXPECT(ctx.undo().undoCount() == 1 && ctx.undo().undoLabel() == "Set Position");

  // The relative edit on the mixed axis: every object keeps its offset, one step (spec 09 scenario 6).
  clickWidget(rig, y->id());
  R1_EXPECT(y->editing());
  rig.ctrl('A');
  rig.type("Mixed+10");
  rig.key(Key::Enter);
  R1_EXPECT(a.position.y == 12.0 && b.position.y == 17.0 && ctx.undo().undoCount() == 2);
  R1_EXPECT(y->hasState(StateFlag::kMixed));  // still different afterwards
  ctx.undo().undo();
  R1_EXPECT(a.position.y == 2.0 && b.position.y == 7.0);
  row.refresh();
  R1_EXPECT(y->hasState(StateFlag::kMixed) && rig.paint() > 0);
}

void scrubIsOneStep() {
  r1test::FieldRig rig;
  Transform a;
  a.position = {10.0, 0.0, 0.0};
  PropertyContext ctx;
  const Target t[] = {targetOf(a, transformSet())};
  ctx.setSelection(t);
  PropertyRowView& row = makeRow(rig, ctx, "position");
  NumberField* x = row.number(0);
  const layout::Rect r = rectOf(rig, x->id());
  const double midY = r.y + r.h * 0.5;
  rig.ui.pointerMove(r.x + 30, midY);
  rig.ui.pointerDown(r.x + 30, midY);
  for (int i = 1; i <= 10; ++i) rig.ui.pointerMove(r.x + 30 + 3 * i, midY);
  R1_EXPECT(x->scrubbing() && a.position.x > 10.0);
  R1_EXPECT(ctx.undo().undoCount() == 0 && ctx.interacting());  // nothing committed while the pointer is down
  rig.ui.pointerUp(r.x + 60, midY);
  R1_EXPECT(!ctx.interacting() && ctx.undo().undoCount() == 1 && a.position.x > 10.0);
  const double scrubbed = a.position.x;
  ctx.undo().undo();
  R1_EXPECT(a.position.x == 10.0);
  ctx.undo().redo();
  R1_EXPECT(a.position.x == scrubbed);
  row.refresh();
  R1_EXPECT(x->value() == scrubbed);

  // Escape restores the value and leaves no step (spec 09 scenario 10).
  const size_t steps = ctx.undo().undoCount();
  rig.ui.pointerMove(r.x + 30, midY);
  rig.ui.pointerDown(r.x + 30, midY);
  rig.ui.pointerMove(r.x + 70, midY);
  R1_EXPECT(a.position.x != scrubbed);
  rig.key(Key::Escape);
  R1_EXPECT(a.position.x == scrubbed && ctx.undo().undoCount() == steps && !ctx.interacting());
  rig.ui.pointerUp(r.x + 70, midY);

  // Scrubbing is off on a mixed field (rule 39): the release is a click that starts editing.
  Transform c;
  c.position = {99.0, 0.0, 0.0};
  const Target two[] = {targetOf(a, transformSet()), targetOf(c, transformSet())};
  ctx.setSelection(two);
  PropertyRowView& mixed = makeRow(rig, ctx, "position");
  NumberField* mx = mixed.number(0);
  R1_EXPECT(mx->hasState(StateFlag::kMixed));
  const layout::Rect mr = rectOf(rig, mx->id());
  rig.ui.pointerMove(mr.x + 30, mr.y + 13);
  rig.ui.pointerDown(mr.x + 30, mr.y + 13);
  rig.ui.pointerMove(mr.x + 70, mr.y + 13);
  rig.ui.pointerUp(mr.x + 70, mr.y + 13);
  R1_EXPECT(!mx->scrubbing() && a.position.x == scrubbed && c.position.x == 99.0);
}

// ---- reset ----------------------------------------------------------------------------------------------------

void resetAffordance() {
  r1test::FieldRig rig;
  Material m;
  PropertyContext ctx;
  const Target t[] = {targetOf(m, materialSet())};
  ctx.setSelection(t);
  PropertyRowView& row = makeRow(rig, ctx, "roughness");
  PropResetButton* reset = row.resetButton();
  R1_EXPECT(reset != nullptr && !reset->shown());
  const layout::Rect hiddenRect = rectOf(rig, reset->id());
  R1_EXPECT(hiddenRect.w == PropResetButton::kSize && hiddenRect.h == PropResetButton::kSize);  // the space is reserved

  ctx.setValue(rowOf(ctx, "roughness"), Value(0.9));
  row.refresh();
  R1_EXPECT(reset->shown() && reset->tooltipText() == "Reset to default");
  R1_EXPECT(row.slider() != nullptr && row.slider()->value() == 0.9 && row.number(0)->value() == 0.9);
  R1_EXPECT(rectOf(rig, reset->id()).x == hiddenRect.x && rectOf(rig, reset->id()).y == hiddenRect.y);  // it did not move
  clickWidget(rig, reset->id());
  R1_EXPECT(m.roughness == 0.5 && !reset->shown() && ctx.undo().undoLabel() == "Reset Roughness");
  R1_EXPECT(reset->tooltipText().empty());
  ctx.undo().undo();
  row.refresh();
  R1_EXPECT(m.roughness == 0.9 && reset->shown());  // undo shows the icon again

  // A hidden button takes no pointer input: a click on its place does nothing.
  ctx.undo().redo();
  row.refresh();
  R1_EXPECT(!reset->shown());
  const size_t steps = ctx.undo().undoCount();
  clickWidget(rig, reset->id());
  R1_EXPECT(ctx.undo().undoCount() == steps && m.roughness == 0.5);
}

// ---- other kinds ----------------------------------------------------------------------------------------------

void boolAndEnum() {
  r1test::FieldRig rig;
  std::vector<Material> mats(2);
  mats[1].useTexture = true;
  mats[1].shading = Shading::Toon;
  PropertyContext ctx;
  std::vector<Target> targets;
  for (Material& m : mats) targets.push_back(targetOf(m, materialSet()));
  ctx.setSelection(targets);

  PropertyRowView& sw = makeRow(rig, ctx, "useTexture");
  R1_EXPECT(sw.toggle() != nullptr && sw.checkbox() == nullptr && sw.toggle()->mixed());
  clickWidget(rig, sw.toggle()->id());  // the indeterminate switch turns everything on
  R1_EXPECT(mats[0].useTexture && mats[1].useTexture && !sw.toggle()->mixed() && sw.toggle()->checked());
  R1_EXPECT(ctx.undo().undoLabel() == "Set Use texture");

  PropertyRowView& en = makeRow(rig, ctx, "shading");
  Select* select = en.select();
  R1_EXPECT(select != nullptr && select->entries().size() == 3);
  R1_EXPECT(!select->selectedIndex().has_value() && select->selectedValue().empty());  // mixed: no entry, the placeholder shows
  select->choose(0);
  R1_EXPECT(mats[0].shading == Shading::Flat && mats[1].shading == Shading::Flat);
  R1_EXPECT(select->selectedLabel() == "Flat" && ctx.undo().undoLabel() == "Set Shading");
  ctx.undo().undo();
  en.refresh();
  R1_EXPECT(!select->selectedIndex().has_value());

  Transform tr;
  PropertyContext c2;
  const Target one[] = {targetOf(tr, transformSet())};
  c2.setSelection(one);
  PropertyRowView& vis = makeRow(rig, c2, "visible");
  R1_EXPECT(vis.checkbox() != nullptr && vis.checkbox()->checked() && vis.checkbox()->label() == "Visible");
  clickWidget(rig, vis.checkbox()->id());
  R1_EXPECT(!tr.visible && !vis.checkbox()->checked());
}

void stringRows() {
  r1test::FieldRig rig;
  Material m;
  m.secret = "hunter2";
  PropertyContext ctx;
  const Target t[] = {targetOf(m, materialSet())};
  ctx.setSelection(t);

  PropertyRowView& name = makeRow(rig, ctx, "name");
  TextInput* input = name.text();
  R1_EXPECT(input != nullptr && input->text() == "Material");
  clickWidget(rig, input->id());
  rig.ctrl('A');
  rig.type("Brass");
  rig.key(Key::Enter);
  R1_EXPECT(m.name == "Brass" && ctx.undo().undoLabel() == "Set Name");
  R1_EXPECT(ctx.undo().undoCount() == 1);

  // A password row never shows its value.
  PropertyRowView& secret = makeRow(rig, ctx, "secret");
  R1_EXPECT(secret.text()->text().empty() && secret.text()->placeholder() == "Hidden");

  // The texture row is disabled until its condition holds (rule 63) and keeps its label (the name stays visible).
  PropertyRowView& texture = makeRow(rig, ctx, "texture");
  R1_EXPECT(!texture.text()->enabled());
  m.useTexture = true;
  texture.refresh();
  R1_EXPECT(texture.text()->enabled());
  m.useTexture = false;
  texture.refresh();
  R1_EXPECT(!texture.text()->enabled());

  // Read-only: the field is shown but cannot be typed into.
  PropertySet<Light> ro("RO");
  ro.category("C");
  ro.addReadOnly("label", "Label", [](const Light& l) { return l.name; });
  Light light;
  PropertyContext c2;
  const Target lt[] = {targetOf(light, ro)};
  c2.setSelection(lt);
  PropertyRowView& readonly = makeRow(rig, c2, "label");
  R1_EXPECT(readonly.text()->readOnly() && readonly.text()->text() == "Light");

  // Mixed strings show the "Mixed" hint with no text.
  Material other;
  other.name = "Other";
  const Target two[] = {targetOf(m, materialSet()), targetOf(other, materialSet())};
  ctx.setSelection(two);
  PropertyRowView& mixed = makeRow(rig, ctx, "name");
  R1_EXPECT(mixed.text()->hasState(StateFlag::kMixed) && mixed.text()->text().empty());
}

void colorRow() {
  r1test::FieldRig rig;
  std::vector<Light> lights(2);
  lights[1].color = Color{1.0f, 0.0f, 0.0f, 0.5f};
  PropertyContext ctx;
  std::vector<Target> targets;
  for (Light& l : lights) targets.push_back(targetOf(l, lightSet()));
  ctx.setSelection(targets);
  PropertyRowView& row = makeRow(rig, ctx, "color");
  R1_EXPECT(row.chip() != nullptr && row.text() != nullptr && row.alpha() != nullptr);
  R1_EXPECT(row.chip()->hasState(StateFlag::kMixed) && row.text()->hasState(StateFlag::kMixed) && row.text()->text().empty());
  R1_EXPECT(row.alpha()->hasState(StateFlag::kMixed));  // the alphas differ too
  R1_EXPECT(rig.paint() > 0);

  // Typing a hex (with or without the #) sets the colour on both, keeping each alpha.
  clickWidget(rig, row.text()->id());
  rig.type("00ff00");
  rig.key(Key::Enter);
  R1_EXPECT(lights[0].color.g == 1.0f && lights[0].color.r == 0.0f && lights[1].color.g == 1.0f && lights[1].color.r == 0.0f);
  R1_EXPECT(lights[1].color.a == 0.5f && lights[0].color.a == 1.0f);  // a hex without alpha digits keeps each object's alpha
  R1_EXPECT(ctx.undo().undoCount() == 1);                              // three channel edits, one step
  R1_EXPECT(row.chip()->hasState(StateFlag::kMixed) && row.alpha()->hasState(StateFlag::kMixed));  // the alphas still differ

  // An 8-digit hex sets the alpha too.
  clickWidget(rig, row.text()->id());
  rig.ctrl('A');
  rig.type("#0000ff80");
  rig.key(Key::Enter);
  R1_EXPECT(lights[0].color.b == 1.0f && std::fabs(lights[0].color.a - 128.0f / 255.0f) < 1e-6f && std::fabs(lights[1].color.a - 128.0f / 255.0f) < 1e-6f);
  R1_EXPECT(!row.chip()->hasState(StateFlag::kMixed) && row.text()->text() == "0000FF");

  // The alpha field edits only the alpha channel.
  clickWidget(rig, row.alpha()->id());
  rig.ctrl('A');
  rig.type("25");
  rig.key(Key::Enter);
  R1_EXPECT(lights[0].color.a == 0.25f && lights[1].color.a == 0.25f && lights[0].color.b == 1.0f);

  // The chip opens the picker popover; a second activation closes it.
  R1_EXPECT(!row.colorPopupOpen());
  clickWidget(rig, row.chip()->id());
  R1_EXPECT(row.colorPopupOpen() && rig.ui.overlays().stack().size() == 1);
  clickWidget(rig, row.chip()->id());
  R1_EXPECT(!row.colorPopupOpen());
}

void sliderRow() {
  r1test::FieldRig rig;
  Material m;
  PropertyContext ctx;
  const Target t[] = {targetOf(m, materialSet())};
  ctx.setSelection(t);
  PropertyRowView& row = makeRow(rig, ctx, "roughness");
  PropSlider* slider = row.slider();
  R1_EXPECT(slider != nullptr && slider->minimum() == 0.0 && slider->maximum() == 1.0 && slider->value() == 0.5);
  const layout::Rect r = rectOf(rig, slider->id());
  // Dragging from the middle to the right end: one step, value 1 (hard range).
  rig.ui.pointerMove(r.x + r.w * 0.5, r.y + 13);
  rig.ui.pointerDown(r.x + r.w * 0.5, r.y + 13);
  rig.ui.pointerMove(r.x + r.w * 0.75, r.y + 13);
  R1_EXPECT(slider->dragging() && m.roughness > 0.6 && ctx.undo().undoCount() == 0);
  rig.ui.pointerMove(r.x + r.w + 40, r.y + 13);
  rig.ui.pointerUp(r.x + r.w + 40, r.y + 13);
  R1_EXPECT(m.roughness == 1.0 && ctx.undo().undoCount() == 1 && row.number(0)->value() == 1.0);
  // Escape while dragging restores the start.
  rig.ui.pointerMove(r.x + r.w * 0.5, r.y + 13);
  rig.ui.pointerDown(r.x + r.w * 0.5, r.y + 13);
  R1_EXPECT(m.roughness < 1.0);
  rig.key(Key::Escape);
  R1_EXPECT(m.roughness == 1.0 && ctx.undo().undoCount() == 1 && !slider->dragging());
  rig.ui.pointerUp(r.x + r.w * 0.5, r.y + 13);
  // Keys step by a hundredth of the range, one step each.
  ctx.undo().clear();
  rig.ui.focusWidget(slider->id(), events::FocusReason::Keyboard);
  rig.key(Key::Left);
  R1_EXPECT(std::fabs(m.roughness - 0.99) < 1e-12 && ctx.undo().undoCount() == 1);
  rig.key(Key::Home);
  R1_EXPECT(m.roughness == 0.0);

  // Mixed sliders rest their thumb in the middle and accept a drag that sets everything.
  Material other;
  other.roughness = 0.1;
  const Target two[] = {targetOf(m, materialSet()), targetOf(other, materialSet())};
  ctx.setSelection(two);
  PropertyRowView& mixed = makeRow(rig, ctx, "roughness");
  R1_EXPECT(mixed.slider()->hasState(StateFlag::kMixed) && mixed.number(0)->hasState(StateFlag::kMixed));
  R1_EXPECT(rig.paint() > 0);
}

// ---- binding (D16) -----------------------------------------------------------------------------------------

void bindingDisplay() {
  r1test::FieldRig rig;
  Variables vars;
  vars.table["base"] = 0.8;
  ContextOptions options;
  options.provider = &vars;
  PropertyContext ctx(options);
  Light light;
  const Target t[] = {targetOf(light, lightSet())};
  ctx.setSelection(t);
  vars.table["key"] = 250.0;
  PropertyRowView& row = makeRow(rig, ctx, "intensity");
  NumberField* f = row.number(0);
  R1_EXPECT(f->boundVariable().empty() && !f->hasState(StateFlag::kInvalid));

  ctx.bind(rowOf(ctx, "intensity"), "key");
  row.refresh();
  R1_EXPECT(f->boundVariable() == "key" && !f->hasState(StateFlag::kInvalid) && f->value() == 250.0);
  R1_EXPECT(row.resetButton()->shown());  // a binding can be cleared with the reset arrow
  R1_EXPECT(rig.paint() > 0);

  vars.table.erase("key");
  row.refresh();
  R1_EXPECT(f->boundVariable() == "key" && f->hasState(StateFlag::kInvalid) && f->tooltipText() == "Missing variable: key");
  R1_EXPECT(rig.paint() > 0);

  // The variable button opens the list of variables; the menu is an overlay.
  vars.table["base"] = 0.8;
  R1_EXPECT(rig.ui.overlays().stack().empty());
  row.openBindingMenu();
  R1_EXPECT(rig.ui.overlays().stack().size() == 1);
  rig.ui.overlays().closeAll();

  clickWidget(rig, row.resetButton()->id());
  R1_EXPECT(f->boundVariable().empty() && !f->hasState(StateFlag::kInvalid) && light.intensity() == 100.0);
  R1_EXPECT(ctx.undo().undoLabel() == "Reset Intensity");

  // A context with no provider shows no variable button affordance for the menu (it does nothing).
  PropertyContext bare;
  bare.setSelection(t);
  PropertyRowView& plain = makeRow(rig, bare, "intensity");
  plain.openBindingMenu();
  R1_EXPECT(rig.ui.overlays().stack().empty());
}

// ---- row menu --------------------------------------------------------------------------------------------------

FlyoutList* flyout(r1test::FieldRig& rig) {
  FlyoutList* found = nullptr;
  rig.ui.tree().forEachDescendant(rig.ui.root(), [&](WidgetId id) {
    if (found == nullptr) found = dynamic_cast<FlyoutList*>(rig.ui.object(id));
  });
  return found;
}

void rightClick(r1test::FieldRig& rig, double x, double y) {
  rig.ui.setTime(rig.ui.now() + 1000);
  rig.ui.pointerMove(x, y);
  rig.ui.pointerDown(x, y, events::Button::Right);
  rig.ui.pointerUp(x, y, events::Button::Right);
}

void rowMenu() {
  r1test::FieldRig rig;
  Material m;
  PropertyContext ctx;
  const Target t[] = {targetOf(m, materialSet())};
  ctx.setSelection(t);
  PropertyRowView& row = makeRow(rig, ctx, "roughness");
  const layout::Rect r = rectOf(rig, row.id());
  const auto open = [&] {
    rightClick(rig, r.x + 20, r.y + 6);  // on the caption: the controls do not use right clicks
    return flyout(rig);
  };

  FlyoutList* menu = open();
  R1_EXPECT(menu != nullptr && menu->itemCount() == 5 && rig.ui.overlays().stack().size() == 1);
  R1_EXPECT(!menu->pick(2));  // nothing to reset: that entry is disabled (rule 50)
  R1_EXPECT(menu->pick(0) && rig.clipboard == "0.5" && rig.ui.overlays().stack().empty());

  rig.clipboard = "0.9";
  menu = open();
  R1_EXPECT(menu->pick(1) && m.roughness == 0.9 && ctx.undo().undoLabel() == "Paste Roughness");
  menu = open();
  R1_EXPECT(menu->pick(2) && m.roughness == 0.5 && ctx.undo().undoLabel() == "Reset Roughness");
  menu = open();
  R1_EXPECT(menu->pick(4) && rig.clipboard == "roughness");  // the stored name, not the label

  // A clipboard that throws or holds junk changes nothing and does not crash.
  rig.clipboardThrows = true;
  menu = open();
  R1_EXPECT(menu->pick(1) && m.roughness == 0.5);
  rig.clipboardThrows = false;
  rig.clipboard = "not a number";
  menu = open();
  const size_t steps = ctx.undo().undoCount();
  R1_EXPECT(menu->pick(1) && m.roughness == 0.5 && ctx.undo().undoCount() == steps);
  R1_EXPECT(row.lastReport().code == EditCode::Unparsable);

  // Read-only rows cannot be pasted into.
  PropertySet<Light> ro("RO");
  ro.category("C");
  ro.addReadOnly("lumens", "Lumens", [](const Light& l) { return l.intensity() * 2.0; });
  Light light;
  PropertyContext c2;
  const Target lt[] = {targetOf(light, ro)};
  c2.setSelection(lt);
  PropertyRowView& locked = makeRow(rig, c2, "lumens");
  const layout::Rect lr = rectOf(rig, locked.id());
  rightClick(rig, lr.x + 20, lr.y + 6);
  menu = flyout(rig);
  R1_EXPECT(menu != nullptr && !menu->pick(1) && menu->pick(0));
}

// ---- history grouping of step edits ------------------------------------------------------------------------------

void stepGrouping() {
  // With mergeSteps the arrow-key notches of one field inside the window are one undo step; without it each is its own.
  for (const bool merge : {true, false}) {
    r1test::FieldRig rig;
    UiClock clock(rig.ui);
    rig.ui.setTime(1234);
    R1_EXPECT(clock.nowMs() == 1234);
    ContextOptions options;
    options.ownedUndo.clock = &clock;
    PropertyContext ctx(options);
    Light light;
    const Target t[] = {targetOf(light, lightSet())};
    ctx.setSelection(t);
    PropertyRowOptions rowOptions;
    rowOptions.mergeSteps = merge;
    PropertyRowView& row = makeRow(rig, ctx, "radius", rowOptions);
    NumberField* f = row.number(0);
    rig.ui.router().focus(f->id(), events::FocusReason::Pointer);  // focused, not editing: arrows step
    rig.key(Key::Up);
    rig.ui.setTime(rig.ui.now() + 100);
    rig.key(Key::Up);
    rig.ui.setTime(rig.ui.now() + 100);
    rig.key(Key::Up);
    R1_EXPECT(light.radius() == 4.0f);
    R1_EXPECT(ctx.undo().undoCount() == (merge ? 1u : 3u));
    rig.ui.setTime(rig.ui.now() + 1000);  // past the 400 ms window
    rig.key(Key::Up);
    R1_EXPECT(ctx.undo().undoCount() == (merge ? 2u : 4u));
    ctx.undo().undo();
    R1_EXPECT(light.radius() == 4.0f);
    if (merge) {
      ctx.undo().undo();
      R1_EXPECT(light.radius() == 1.0f);  // the three notches undo together
    }
  }
}

// ---- hostile ----------------------------------------------------------------------------------------------------

void staleRows() {
  r1test::FieldRig rig;
  Transform a;
  PropertyContext ctx;
  const Target t[] = {targetOf(a, transformSet())};
  ctx.setSelection(t);
  PropertyRowView& row = makeRow(rig, ctx, "position");
  NumberField* x = row.number(0);
  ctx.setSelection({});
  row.refresh();  // no rows any more: nothing to read, nothing to crash
  R1_EXPECT(row.describe() == "(stale row)");
  clickWidget(rig, x->id());
  rig.ctrl('A');
  rig.type("5");
  rig.key(Key::Enter);  // the callbacks find no row: no edit, no crash
  R1_EXPECT(a.position.x == 0.0 && ctx.undo().undoCount() == 0);
  row.openColorPopup();
  row.openBindingMenu();
  R1_EXPECT(rig.paint() > 0);

  // A row built for a bad index is empty and harmless.
  PropertyRowView& bad = rig.ui.create<PropertyRowView>(rig.ui.root(), ctx, size_t{999});
  rig.layout();
  bad.refresh();
  R1_EXPECT(bad.numberCount() == 0 && bad.select() == nullptr);
}

void captionlessRows() {
  r1test::FieldRig rig;
  Transform a;
  PropertyContext ctx;
  const Target t[] = {targetOf(a, transformSet())};
  ctx.setSelection(t);
  PropertyRowOptions options;
  options.caption = false;
  PropertyRowView& row = makeRow(rig, ctx, "position", options);
  R1_EXPECT(row.resetButton() == nullptr && row.numberCount() == 3);
  const layout::Rect r = rectOf(rig, row.number(0)->id());
  R1_EXPECT(std::fabs(r.y - 10.0) < 0.5 && r.h == 26.0);  // the field is the first thing in the row: 26 px tall at the row's top
  PropertyRowView& visible = makeRow(rig, ctx, "visible", options);
  R1_EXPECT(visible.checkbox() != nullptr && visible.checkbox()->label() == "Visible");
}

}  // namespace

int main() try {
  vectorRow();
  scrubIsOneStep();
  resetAffordance();
  boolAndEnum();
  stringRows();
  colorRow();
  sliderRow();
  bindingDisplay();
  rowMenu();
  stepGrouping();
  staleRows();
  captionlessRows();
  return r1test::finish();
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}
