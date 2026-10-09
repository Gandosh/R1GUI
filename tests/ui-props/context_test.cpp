// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of PropertyContext: the rows shared by selections of one, two and 1000 objects of one or
//   two types, mixed-value detection (whole and per axis), edits applied to all objects in one undo
//   step, typed values with units, "Mixed + 5" expressions, clamping, edit conditions, reset per row
//   and per category, copy and paste of values and groups, value binding (D16) and change notices.
// Callers: CTest (fast tier, no GPU).
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "Samples.h"
#include "TestSupport.h"
#include "r1ui/props/PropertyContext.h"

namespace {

using namespace r1ui::props;
using namespace samples;

std::vector<Target> targetsOf(std::vector<Material>& mats) {
  std::vector<Target> out;
  for (Material& m : mats) out.push_back(targetOf(m, materialSet()));
  return out;
}

size_t row(const PropertyContext& c, const char* name) { return c.findRow(name).value_or(static_cast<size_t>(-1)); }

// A host binding provider with a mutable variable table.
class Variables final : public BindingProvider {
 public:
  std::map<std::string, Value> table;
  std::optional<Value> resolve(std::string_view source) const override {
    const auto it = table.find(std::string(source));
    if (it == table.end()) return std::nullopt;
    return it->second;
  }
  std::vector<VariableInfo> variables() const override {
    std::vector<VariableInfo> out;
    for (const auto& [name, value] : table) out.push_back({name, ValueKind::Double, {}});
    return out;
  }
};

// ---- rows and mixed values ------------------------------------------------------------------------------

void rowsOfOneAndTwoTypes() {
  Transform tr;
  Material mat;
  Light light;
  PropertyContext c;
  const Target one[] = {targetOf(tr, transformSet())};
  R1_EXPECT(c.setSelection(one) == 1);
  R1_EXPECT(c.rowCount() == 6 && c.descriptor(0).name == "position");  // display order: Transform category first

  // Transform + Light share only "name" (string); Material + Light share "color" and "name".
  const Target tl[] = {targetOf(tr, transformSet()), targetOf(light, lightSet())};
  c.setSelection(tl);
  R1_EXPECT(c.rowCount() == 1 && c.descriptor(0).name == "name");
  const Target ml[] = {targetOf(mat, materialSet()), targetOf(light, lightSet())};
  c.setSelection(ml);
  R1_EXPECT(c.rowCount() == 2 && c.findRow("color").has_value() && c.findRow("name").has_value());
  const Target all[] = {targetOf(tr, transformSet()), targetOf(mat, materialSet()), targetOf(light, lightSet())};
  c.setSelection(all);
  R1_EXPECT(c.rowCount() == 1);

  // Edits reach objects of different types through their own descriptors.
  R1_EXPECT(c.setValue(0, Value(std::string("Shared"))).ok());
  R1_EXPECT(tr.name == "Shared" && mat.name == "Shared" && light.name == "Shared");
  R1_EXPECT(c.undo().undoCount() == 1);
  R1_EXPECT(c.undo().undo().done && tr.name == "Object" && mat.name == "Material" && light.name == "Light");

  c.setSelection({});
  R1_EXPECT(c.rowCount() == 0 && c.targetCount() == 0);
  R1_EXPECT(c.setValue(0, Value(1.0)).code == EditCode::NoSuchProperty);
}

void sameNameDifferentKind() {
  // "level" is a string in one type and an int in the other: not common.
  struct A { std::string level; };
  struct B { int level = 0; };
  PropertySet<A> sa("A");
  sa.add("level", "Level", &A::level);
  PropertySet<B> sb("B");
  sb.add("level", "Level", &B::level);
  A a;
  B b;
  PropertyContext c;
  const Target t[] = {targetOf(a, sa), targetOf(b, sb)};
  c.setSelection(t);
  R1_EXPECT(c.rowCount() == 0);
}

void mixedValues() {
  std::vector<Material> mats(3);
  mats[0].roughness = 0.1;
  mats[1].roughness = 0.2;
  mats[2].roughness = 0.3;
  PropertyContext c;
  c.setSelection(targetsOf(mats));
  const size_t rough = row(c, "roughness");
  PropertyState s = c.state(rough);
  R1_EXPECT(s.mixed && s.value == Value(0.1));
  R1_EXPECT(!c.state(row(c, "shading")).mixed);  // all Smooth: shows the common value

  // Plain number into a mixed field: every object gets it, in one step.
  R1_EXPECT(c.setText(rough, "0.9").ok());
  R1_EXPECT(mats[0].roughness == 0.9 && mats[1].roughness == 0.9 && mats[2].roughness == 0.9);
  R1_EXPECT(!c.state(rough).mixed && c.undo().undoCount() == 1);
  R1_EXPECT(c.undo().undoLabel() == "Set Roughness");
  c.undo().undo();
  R1_EXPECT(mats[0].roughness == 0.1 && mats[2].roughness == 0.3);

  // Expression over the placeholder: every object keeps its offset (spec 09 scenario 6).
  R1_EXPECT(c.setText(rough, "Mixed + 0.5").ok());
  R1_EXPECT_NEAR(mats[0].roughness, 0.6, 1e-12);
  R1_EXPECT_NEAR(mats[1].roughness, 0.7, 1e-12);
  R1_EXPECT_NEAR(mats[2].roughness, 0.8, 1e-12);
  R1_EXPECT(c.undo().undoCount() == 1);
  c.undo().undo();
  R1_EXPECT(mats[1].roughness == 0.2);

  // Enum and bool mixed.
  mats[1].shading = Shading::Toon;
  mats[2].useTexture = true;
  R1_EXPECT(c.state(row(c, "shading")).mixed && c.state(row(c, "useTexture")).mixed);
  R1_EXPECT(c.setValue(row(c, "useTexture"), Value(true)).ok() && mats[0].useTexture && mats[1].useTexture);  // click on the indeterminate box checks all
  R1_EXPECT(c.setText(row(c, "shading"), "flat").ok() && mats[1].shading == Shading::Flat);
}

void perAxisMixed() {
  std::vector<Transform> trs(3);
  trs[0].position = {1.0, 2.0, 3.0};
  trs[1].position = {1.0, 5.0, 3.0};
  trs[2].position = {1.0, 2.0, 9.0};
  PropertyContext c;
  std::vector<Target> targets;
  for (Transform& t : trs) targets.push_back(targetOf(t, transformSet()));
  c.setSelection(targets);
  const size_t pos = row(c, "position");
  const PropertyState s = c.state(pos);
  R1_EXPECT(s.mixed && !s.componentMixed[0] && s.componentMixed[1] && s.componentMixed[2]);

  // Editing X keeps each object's Y and Z.
  R1_EXPECT(c.setComponent(pos, 0, 7.0).ok());
  R1_EXPECT(trs[0].position == (Vec3{7.0, 2.0, 3.0}) && trs[1].position == (Vec3{7.0, 5.0, 3.0}) && trs[2].position == (Vec3{7.0, 2.0, 9.0}));
  // Relative edit on one axis.
  R1_EXPECT(c.setText(pos, "Mixed * 2", 1).ok());
  R1_EXPECT(trs[0].position.y == 4.0 && trs[1].position.y == 10.0 && trs[2].position.y == 4.0);
  // Units: typed centimetres are converted to the stored metres.
  R1_EXPECT(c.setText(pos, "250cm", 2).ok());
  R1_EXPECT_NEAR(trs[1].position.z, 2.5, 1e-12);
  R1_EXPECT(c.setComponent(pos, 3, 1.0).code == EditCode::WrongType);  // no fourth axis
}

void thousandObjects() {
  std::vector<Material> mats(1000);
  for (size_t i = 0; i < mats.size(); ++i) mats[i].roughness = static_cast<double>(i % 10) / 10.0;
  std::vector<Light> lights(1000);
  PropertyContext c;
  std::vector<Target> targets = targetsOf(mats);
  for (Light& l : lights) targets.push_back(targetOf(l, lightSet()));
  R1_EXPECT(c.setSelection(targets) == 2000 && c.distinctSetCount() == 2);
  R1_EXPECT(c.rowCount() == 2);  // color, name
  R1_EXPECT(c.setValue(row(c, "color"), Value(Color{1, 0, 0, 1})).changed == 2000);
  R1_EXPECT(mats[999].color.r == 1.0f && lights[0].color.g == 0.0f);
  R1_EXPECT(c.undo().undoCount() == 1);
  c.undo().undo();
  R1_EXPECT(mats[0].color.r == 0.8f && lights[999].color.r == 1.0f);
}

// ---- editing rules ---------------------------------------------------------------------------------------

void clampingAndNoOps() {
  Transform tr;
  PropertyContext c;
  const Target one[] = {targetOf(tr, transformSet())};
  c.setSelection(one);
  const size_t pos = row(c, "position");
  EditReport r = c.setComponent(pos, 0, 5000.0);
  R1_EXPECT(r.ok() && r.code == EditCode::Clamped && r.clamped == 1 && tr.position.x == 1000.0);
  r = c.setComponent(pos, 0, 5000.0);  // already at the limit: nothing changes
  R1_EXPECT(r.code == EditCode::Unchanged && c.undo().undoCount() == 1);
  r = c.setText(pos, "1000", 0);
  R1_EXPECT(r.code == EditCode::Unchanged && c.undo().undoCount() == 1);  // rule 16: same value, no step
  R1_EXPECT(c.setText(row(c, "layer"), "99").code == EditCode::Clamped && tr.layer == 31);
  R1_EXPECT(c.setText(row(c, "layer"), "7.6").ok() && tr.layer == 8);  // integers round (rule 18)

  // Unparsable and hostile text change nothing.
  R1_EXPECT(c.setText(pos, "abc", 0).code == EditCode::Unparsable);
  R1_EXPECT(c.setText(pos, "1/0", 0).code == EditCode::Unparsable || c.setText(pos, "1/0", 0).code == EditCode::NotFinite);
  R1_EXPECT(c.setText(pos, "nan", 0).code == EditCode::Unparsable);
  R1_EXPECT(c.setComponent(pos, 0, std::nan("")).code == EditCode::NotFinite);
  R1_EXPECT(c.setComponent(pos, 0, INFINITY).code == EditCode::NotFinite);
  R1_EXPECT(tr.position.x == 1000.0);
  R1_EXPECT(c.setValue(pos, Value(1.0)).code == EditCode::WrongType);
  R1_EXPECT(c.setValue(row(c, "name"), Value(std::string(100, 'x'))).code == EditCode::TooLong);  // maxLength 64
  R1_EXPECT(c.setValue(row(c, "name"), Value(std::string("bad\xFF"))).code == EditCode::InvalidUtf8);
  R1_EXPECT(tr.name == "Object");
  const size_t steps = c.undo().undoCount();
  R1_EXPECT(steps == 3);  // x clamp, layer 31, layer 8
}

void conditions() {
  std::vector<Material> mats(2);
  PropertyContext c;
  c.setSelection(targetsOf(mats));
  const size_t texture = row(c, "texture");
  const size_t scale = row(c, "textureScale");
  R1_EXPECT(!c.state(texture).enabled && !c.state(scale).visible);
  R1_EXPECT(c.setValue(texture, Value(std::string("wood.png"))).code == EditCode::Disabled);
  R1_EXPECT(mats[0].texture.empty() && c.undo().undoCount() == 0);

  // One object satisfying the condition is not enough to enable (every object must), but is enough to show.
  mats[0].useTexture = true;
  R1_EXPECT(!c.state(texture).enabled && c.state(scale).visible);
  mats[1].useTexture = true;
  R1_EXPECT(c.state(texture).enabled);
  R1_EXPECT(c.setValue(texture, Value(std::string("wood.png"))).ok());
  R1_EXPECT(mats[0].texture == "wood.png" && mats[1].texture == "wood.png");

  // Editing the controlling value enables/hides on the next read (rule 67); it is its own step.
  const size_t steps = c.undo().undoCount();
  R1_EXPECT(c.setValue(row(c, "useTexture"), Value(false)).ok() && c.undo().undoCount() == steps + 1);
  R1_EXPECT(!c.state(texture).enabled && !c.state(scale).visible);

  // Read-only rows refuse edits.
  PropertySet<Light> ro("RO");
  ro.category("C");
  ro.addReadOnly("lumens", "Lumens", [](const Light& l) { return l.intensity() * 2.0; });
  Light light;
  PropertyContext c2;
  const Target t[] = {targetOf(light, ro)};
  c2.setSelection(t);
  R1_EXPECT(c2.state(0).readOnly && c2.setValue(0, Value(1.0)).code == EditCode::ReadOnly);
}

void conditionOperators() {
  struct Probe {
    int mode = 0;
    double amount = 0.0;
  };
  PropertySet<Probe> s("Probe");
  s.category("P");
  s.add("mode", "Mode", &Probe::mode);
  s.add("amount", "Amount", &Probe::amount).enableWhen("mode", CompareOp::GreaterEqual, 2).visibleWhen("mode", CompareOp::NotEqual, 9);
  Probe p;
  PropertyContext c;
  const Target t[] = {targetOf(p, s)};
  c.setSelection(t);
  const size_t amount = row(c, "amount");
  R1_EXPECT(!c.state(amount).enabled);
  p.mode = 2;
  R1_EXPECT(c.state(amount).enabled && c.state(amount).visible);
  p.mode = 9;
  R1_EXPECT(!c.state(amount).visible);

  // A condition naming a property the object lacks fails open: a typo never hides a property.
  PropertySet<Probe> typo("Typo");
  typo.category("P");
  typo.add("amount", "Amount", &Probe::amount).enableWhenTrue("nope");
  const Target t2[] = {targetOf(p, typo)};
  c.setSelection(t2);
  R1_EXPECT(c.state(0).enabled);
}

void resets() {
  std::vector<Material> mats(2);
  mats[0].roughness = 0.9;
  mats[1].color = Color{0, 0, 1, 1};
  mats[1].name = "Other";
  PropertyContext c;
  c.setSelection(targetsOf(mats));
  const size_t rough = row(c, "roughness");
  R1_EXPECT(c.state(rough).canReset && c.state(rough).hasDefault);
  R1_EXPECT(!c.state(row(c, "shading")).canReset);
  R1_EXPECT(c.reset(rough).changed == 1);  // only the object that differed changed
  R1_EXPECT(mats[0].roughness == 0.5 && !c.state(rough).canReset);
  R1_EXPECT(c.undo().undoLabel() == "Reset Roughness");
  c.undo().undo();
  R1_EXPECT(mats[0].roughness == 0.9 && c.state(rough).canReset);  // undo shows the icon again (scenario 7)

  // Category reset: one step for every differing row of the category.
  mats[0].shading = Shading::Toon;
  const size_t before = c.undo().undoCount();
  EditReport r = c.resetCategory("Appearance");
  R1_EXPECT(r.ok() && r.changed == 3);  // roughness (obj 0), color (obj 1), shading (obj 0)
  R1_EXPECT(c.undo().undoCount() == before + 1 && c.undo().undoLabel() == "Reset Appearance");
  R1_EXPECT(mats[0].roughness == 0.5 && mats[1].color == (Color{0.8f, 0.8f, 0.8f, 1.0f}) && mats[0].shading == Shading::Smooth);
  R1_EXPECT(mats[1].name == "Other");  // other categories untouched
  R1_EXPECT(c.resetCategory("Appearance").code == EditCode::Unchanged);
  c.undo().undo();
  R1_EXPECT(mats[0].roughness == 0.9 && mats[1].color == (Color{0, 0, 1, 1}) && mats[0].shading == Shading::Toon);

  // No default or not resettable: refused.
  PropertySet<Light> bare("Bare");
  bare.category("B");
  bare.add("intensity", "Intensity", &Light::intensity, &Light::setIntensity);
  bare.add("radius", "Radius", &Light::radius, &Light::setRadius).defaultValue(1.0).notResettable();
  Light light;
  PropertyContext c2;
  const Target t[] = {targetOf(light, bare)};
  c2.setSelection(t);
  R1_EXPECT(!c2.state(0).hasDefault && c2.reset(0).code == EditCode::NoDefault);
  R1_EXPECT(c2.reset(1).code == EditCode::NoDefault && !c2.state(1).canReset);
}

void copyAndPaste() {
  std::vector<Material> mats(2);
  mats[0].roughness = 0.25;
  mats[0].color = Color{0.5f, 0.25f, 0.125f, 1.0f};
  mats[0].shading = Shading::Toon;
  mats[0].name = "Tab\there\nnew \\ line";
  mats[0].secret = "hunter2";
  PropertyContext c;
  const Target first[] = {targetOf(mats[0], materialSet())};
  c.setSelection(first);

  R1_EXPECT(c.copyValue(row(c, "roughness")) == std::optional<std::string>("0.25"));
  R1_EXPECT(c.copyValue(row(c, "shading")) == std::optional<std::string>("toon"));
  R1_EXPECT(!c.copyValue(row(c, "secret")).has_value());  // passwords are never copied
  const std::string appearance = c.copyGroup("Appearance");
  const std::string object = c.copyGroup("Object");
  R1_EXPECT(appearance.rfind("r1props 1\n", 0) == 0 && appearance.find("roughness\t0.25") != std::string::npos);
  R1_EXPECT(object.find("secret") == std::string::npos && object.find("Tab\\there\\nnew \\\\ line") != std::string::npos);

  const Target second[] = {targetOf(mats[1], materialSet())};
  c.setSelection(second);
  R1_EXPECT(c.pasteValue(row(c, "roughness"), "0.25").ok() && mats[1].roughness == 0.25);
  R1_EXPECT(c.undo().undoLabel() == "Paste Roughness");
  R1_EXPECT(c.pasteValue(row(c, "roughness"), "not a number").code == EditCode::Unparsable && mats[1].roughness == 0.25);
  R1_EXPECT(c.pasteValue(row(c, "roughness"), "Mixed + 1").code == EditCode::Unparsable);  // a pasted value is a value, not a relative edit
  R1_EXPECT(c.pasteValue(row(c, "shading"), "plastic").code == EditCode::Unparsable);

  const size_t steps = c.undo().undoCount();
  EditReport r = c.pasteGroup("Appearance", std::nullopt, appearance);
  R1_EXPECT(r.ok() && r.changed == 2);  // color and shading; roughness already matches
  R1_EXPECT(mats[1].color == mats[0].color && mats[1].shading == Shading::Toon);
  R1_EXPECT(c.undo().undoCount() == steps + 1 && c.undo().undoLabel() == "Paste Appearance");
  c.undo().undo();
  R1_EXPECT(mats[1].color == (Color{0.8f, 0.8f, 0.8f, 1.0f}) && mats[1].shading == Shading::Smooth);

  // Strings with tabs, newlines and backslashes survive the round trip.
  R1_EXPECT(c.pasteGroup("Object", std::nullopt, object).ok() && mats[1].name == mats[0].name);
  R1_EXPECT(mats[1].secret.empty());

  // A group paste ignores lines outside its scope and reports unknown names; hostile payloads are refused.
  r = c.pasteGroup("Appearance", std::nullopt, "r1props 1\nnope\t1\nname\tShould not apply\nroughness\t0.75\n");
  R1_EXPECT(r.changed == 1 && mats[1].roughness == 0.75 && mats[1].name == mats[0].name);
  R1_EXPECT(r.issueCount == 1 && r.issues[0].code == EditCode::NoSuchProperty);
  R1_EXPECT(c.pasteGroup(std::nullopt, std::nullopt, "garbage").code == EditCode::Unparsable);
  R1_EXPECT(c.pasteGroup(std::nullopt, std::nullopt, "").code == EditCode::Unparsable);
  R1_EXPECT(c.pasteGroup(std::nullopt, std::nullopt, std::string(kMaxPasteBytes + 1, 'x')).code == EditCode::TooLong);
  std::string flood = "r1props 1\n";
  for (size_t i = 0; i < kMaxPasteLines + 5; ++i) flood += "roughness\t1\n";
  R1_EXPECT(c.pasteGroup(std::nullopt, std::nullopt, flood).code == EditCode::TooLong);
  R1_EXPECT(c.pasteGroup(std::nullopt, std::nullopt, "r1props 1\nroughness\t1\\q\n").issueCount == 1);  // bad escape: that line only
  R1_EXPECT(mats[1].roughness == 0.75);
}

// ---- interactions -----------------------------------------------------------------------------------------

void interactions() {
  Light light;
  PropertyContext c;
  const Target t[] = {targetOf(light, lightSet())};
  c.setSelection(t);
  const size_t intensity = row(c, "intensity");

  // A scrub is one step however many values were applied (rule 75).
  R1_EXPECT(c.beginInteraction(intensity) && c.interacting());
  for (int i = 1; i <= 50; ++i) c.setValue(intensity, Value(100.0 + i));
  R1_EXPECT(light.intensity() == 150.0 && c.undo().undoCount() == 0);  // nothing committed yet
  R1_EXPECT(c.endInteraction() && !c.interacting());
  R1_EXPECT(c.undo().undoCount() == 1 && c.undo().undoLabel() == "Set Intensity");
  c.undo().undo();
  R1_EXPECT(light.intensity() == 100.0);

  // A scrub that returns to its start leaves no step (rule 31).
  c.beginInteraction(intensity);
  c.setValue(intensity, Value(300.0));
  c.setValue(intensity, Value(100.0));
  c.endInteraction();
  R1_EXPECT(c.undo().undoCount() == 0 && c.undo().redoCount() == 1);  // the earlier undo is still redoable: a no-op does not truncate redo

  // Escape restores the values from before the gesture.
  c.beginInteraction(intensity);
  c.setValue(intensity, Value(777.0));
  R1_EXPECT(light.intensity() == 777.0);
  R1_EXPECT(c.cancelInteraction() && light.intensity() == 100.0 && !c.interacting());
  R1_EXPECT(c.undo().undoCount() == 0);

  // Several properties changed inside one gesture share a step (rule 76).
  c.beginInteraction(intensity);
  c.setValue(intensity, Value(5.0));
  c.setValue(row(c, "radius"), Value(3.0));
  c.setValue(row(c, "castShadows"), Value(false));
  c.endInteraction();
  R1_EXPECT(c.undo().undoCount() == 1);
  c.undo().undo();
  R1_EXPECT(light.intensity() == 100.0 && light.radius() == 1.0f && light.castShadows);

  // An edit during an open interaction joins it (rule 77); end/cancel without begin are refused.
  R1_EXPECT(!c.endInteraction() && !c.cancelInteraction());
  c.beginInteraction(intensity);
  c.setText(row(c, "name"), "Joined");
  c.endInteraction();
  R1_EXPECT(c.undo().undoCount() == 1);
}

// ---- binding ----------------------------------------------------------------------------------------------

void bindings() {
  Variables vars;
  vars.table["base"] = 0.8;
  ContextOptions options;
  options.provider = &vars;
  PropertyContext c(options);
  std::vector<Material> mats(2);
  c.setSelection(targetsOf(mats));
  const size_t rough = row(c, "roughness");

  R1_EXPECT(c.state(rough).binding == BindingState::Unbound);
  EditReport r = c.bind(rough, "base");
  R1_EXPECT(r.ok() && mats[0].roughness == 0.8 && mats[1].roughness == 0.8);
  PropertyState s = c.state(rough);
  R1_EXPECT(s.binding == BindingState::Bound && s.bindingSource == "base");
  R1_EXPECT(c.undo().undoCount() == 1 && c.undo().undoLabel() == "Bind Roughness");

  // The variable changes: refresh derives new values without a history step.
  vars.table["base"] = 0.3;
  R1_EXPECT(c.refreshBindings() == 2 && mats[0].roughness == 0.3);
  R1_EXPECT(c.undo().undoCount() == 1);

  // The variable disappears: broken, the stored value stays.
  vars.table.erase("base");
  R1_EXPECT(c.state(rough).binding == BindingState::Broken && c.state(rough).bindingSource == "base");
  R1_EXPECT(c.refreshBindings() == 0 && mats[0].roughness == 0.3);

  // A wrong-typed value also breaks it.
  vars.table["base"] = std::string("oops");
  R1_EXPECT(c.state(rough).binding == BindingState::Broken);
  vars.table["base"] = 0.6;
  R1_EXPECT(c.state(rough).binding == BindingState::Bound);

  // Rebind to another variable.
  vars.table["other"] = 0.1;
  R1_EXPECT(c.bind(rough, "other").ok() && mats[1].roughness == 0.1);
  R1_EXPECT(c.undo().undoLabel() == "Rebind Roughness");
  R1_EXPECT(c.bind(rough, "other").code == EditCode::Unchanged);

  // Undo restores both the binding and the value.
  c.undo().undo();
  R1_EXPECT(c.state(rough).bindingSource == "base");
  c.undo().undo();
  R1_EXPECT(c.state(rough).binding == BindingState::Unbound && mats[0].roughness == 0.5);
  c.undo().redo();
  R1_EXPECT(c.state(rough).binding != BindingState::Unbound && mats[0].roughness == 0.8);

  // Unbind keeps the value; it is a step of its own.
  R1_EXPECT(c.unbind(rough).changed == 2);
  R1_EXPECT(c.state(rough).binding == BindingState::Unbound && mats[0].roughness == 0.8);
  R1_EXPECT(c.undo().undoLabel() == "Unbind Roughness");
  R1_EXPECT(c.unbind(rough).code == EditCode::Unchanged);
  c.undo().undo();
  R1_EXPECT(c.state(rough).binding != BindingState::Unbound);

  // Typing over a bound value detaches the binding in the same step.
  R1_EXPECT(c.setText(rough, "0.25").ok());
  R1_EXPECT(c.state(rough).binding == BindingState::Unbound && mats[0].roughness == 0.25);
  c.undo().undo();
  R1_EXPECT(c.state(rough).binding != BindingState::Unbound && mats[0].roughness == 0.8);

  // Mixed bindings: only one object bound.
  c.undo().undo();  // back to unbound for all
  const Target only[] = {targetOf(mats[0], materialSet())};
  c.setSelection(only);
  c.bind(row(c, "roughness"), "base");
  c.setSelection(targetsOf(mats));
  R1_EXPECT(c.state(rough).binding == BindingState::Mixed);

  // A binding without a provider is broken; bad sources are refused.
  PropertyContext bare;
  bare.setSelection(targetsOf(mats));
  R1_EXPECT(bare.bind(rough, "x").ok() && bare.state(rough).binding == BindingState::Broken);
  R1_EXPECT(bare.bind(rough, "").code == EditCode::Unparsable);
  R1_EXPECT(bare.bind(rough, std::string(kMaxBindingSourceBytes + 1, 'v')).code == EditCode::Unparsable);
  R1_EXPECT(bare.bind(rough, "\xFF").code == EditCode::Unparsable);
  R1_EXPECT(bare.bind(99, "x").code == EditCode::NoSuchProperty);
}

// ---- colours and category scopes ------------------------------------------------------------------------------

void colourChannels() {
  std::vector<Light> lights(2);
  lights[1].color = Color{0.0f, 1.0f, 1.0f, 0.5f};  // against white: r and a differ, g and b agree
  PropertyContext c;
  std::vector<Target> targets;
  for (Light& l : lights) targets.push_back(targetOf(l, lightSet()));
  c.setSelection(targets);
  const size_t color = row(c, "color");
  const PropertyState s = c.state(color);
  R1_EXPECT(s.mixed && s.componentMixed[0] && !s.componentMixed[1] && !s.componentMixed[2] && s.componentMixed[3]);
  R1_EXPECT(c.setComponent(color, 3, 0.25).ok());
  R1_EXPECT(lights[0].color.a == 0.25f && lights[1].color.a == 0.25f && lights[1].color.r == 0.0f && lights[0].color.r == 1.0f);
  R1_EXPECT(c.setComponent(color, 1, -3.0).code == EditCode::Clamped && lights[0].color.g == 0.0f);  // channels clamp to [0, 1]
  R1_EXPECT(c.setComponent(color, 4, 0.0).code == EditCode::WrongType);
  R1_EXPECT(c.setText(color, "Mixed * 0.5", 3).ok() && lights[0].color.a == 0.125f);
  R1_EXPECT(c.setText(color, "#336699").ok() && lights[0].color.b > 0.59f && lights[0].color.b < 0.61f && lights[1].color.r < 0.21f);
}

void categoryScopes() {
  // Properties declared without a category belong to "" (an exact scope); nullopt means every row.
  struct Loose {
    double a = 1.0;
    double b = 1.0;
  };
  PropertySet<Loose> s("Loose");
  s.add("a", "A", &Loose::a).defaultValue(0.0);
  s.category("Other");
  s.add("b", "B", &Loose::b).defaultValue(0.0);
  Loose o;
  PropertyContext c;
  const Target t[] = {targetOf(o, s)};
  c.setSelection(t);
  R1_EXPECT(c.descriptor(0).category.empty());
  R1_EXPECT(c.resetCategory(std::string_view("")).changed == 1 && o.a == 0.0 && o.b == 1.0);
  R1_EXPECT(c.resetCategory(std::nullopt).changed == 1 && o.b == 0.0);
  R1_EXPECT(c.undo().undoLabel() == "Reset properties");  // no scope name to show
  o.a = o.b = 5.0;
  R1_EXPECT(c.copyGroup(std::string_view("")).find("a	5") != std::string::npos && c.copyGroup(std::string_view("")).find("b	") == std::string::npos);
  R1_EXPECT(c.copyGroup(std::nullopt).find("b	5") != std::string::npos);
  // A sub-group scope.
  PropertySet<Loose> g("Grouped");
  g.category("C");
  g.group("One");
  g.add("a", "A", &Loose::a).defaultValue(0.0);
  g.group("Two");
  g.add("b", "B", &Loose::b).defaultValue(0.0);
  const Target gt[] = {targetOf(o, g)};
  c.setSelection(gt);
  R1_EXPECT(c.resetCategory(std::string_view("C"), std::string_view("Two")).changed == 1 && o.a == 5.0 && o.b == 0.0);
}

// ---- notifications ------------------------------------------------------------------------------------------

void notifications() {
  std::vector<Material> mats(2);
  PropertyContext c;
  int selection = 0, values = 0, bindings = 0;
  int64_t lastRow = -2;
  const auto token = c.notifier().subscribe([&](const ChangeEvent& e) {
    selection += e.kind == ChangeKind::Selection;
    values += e.kind == ChangeKind::Values;
    bindings += e.kind == ChangeKind::Bindings;
    if (e.kind == ChangeKind::Values) lastRow = e.row;
  });
  c.setSelection(targetsOf(mats));
  R1_EXPECT(selection == 1);
  const size_t rough = row(c, "roughness");
  c.setValue(rough, Value(0.9));
  R1_EXPECT(values == 1 && lastRow == static_cast<int64_t>(rough));
  c.setValue(rough, Value(0.9));  // no change: no notice
  R1_EXPECT(values == 1);
  c.undo().undo();  // undo reports a change too (spec 09 rule 7)
  R1_EXPECT(values == 2);
  c.notifyExternalChange();
  R1_EXPECT(values == 3 && lastRow == -1);
  c.notifier().unsubscribe(token);
  c.setValue(rough, Value(0.1));
  R1_EXPECT(values == 3);
  R1_EXPECT(bindings == 0);

  // Undo and redo tell the views too (rule 7): a Values and a Structure notice each.
  const int valuesBefore = values;
  const auto again = c.notifier().subscribe([&](const ChangeEvent& e) { values += e.kind == ChangeKind::Values; });
  c.undo().undo();
  c.undo().redo();
  R1_EXPECT(values == valuesBefore + 2);
  c.notifier().unsubscribe(again);

  // forgetObject drops a destroyed object from the selection and the history.
  c.setSelection(targetsOf(mats));
  c.setValue(rough, Value(0.7));
  c.forgetObject(&mats[1]);
  R1_EXPECT(c.targetCount() == 1 && c.undo().undoCount() >= 1);
  R1_EXPECT(c.undo().undo().done && mats[0].roughness != 0.7);
}

}  // namespace

int main() {
  rowsOfOneAndTwoTypes();
  sameNameDifferentKind();
  mixedValues();
  perAxisMixed();
  thousandObjects();
  clampingAndNoOps();
  conditions();
  conditionOperators();
  resets();
  copyAndPaste();
  interactions();
  colourChannels();
  categoryScopes();
  bindings();
  notifications();
  return r1test::finish();
}
