// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: hostile-input and scale tests of ui-props: 100k objects selected, 10k properties in one set,
//   NaN/infinite edits, huge and malformed strings, null and duplicate targets, stale row indices,
//   binding floods, and operations on empty selections. Each must either apply cleanly or fail with an
//   EditCode and leave every object and the history untouched.
// Callers: CTest (fast tier, no GPU).
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "Samples.h"
#include "TestSupport.h"
#include "r1ui/props/PanelState.h"

namespace {

using namespace r1ui::props;
using namespace samples;

size_t row(const PropertyContext& c, const char* name) { return c.findRow(name).value_or(static_cast<size_t>(-1)); }

struct Tiny {
  double v = 1.0;
  std::string text;
};

const PropertySet<Tiny>& tinySet() {
  static const PropertySet<Tiny> set = [] {
    PropertySet<Tiny> s("Tiny");
    s.category("T");
    s.add("v", "V", &Tiny::v).range(-1e6, 1e6).defaultValue(1.0);
    s.add("text", "Text", &Tiny::text);
    return s;
  }();
  return set;
}

void hundredThousandObjects() {
  std::vector<Tiny> objects(100000);
  for (size_t i = 0; i < objects.size(); ++i) objects[i].v = static_cast<double>(i % 7);
  std::vector<Target> targets;
  targets.reserve(objects.size());
  for (Tiny& t : objects) targets.push_back(targetOf(t, tinySet()));
  PropertyContext c;
  const auto start = std::chrono::steady_clock::now();
  R1_EXPECT(c.setSelection(targets) == 100000);
  R1_EXPECT(c.rowCount() == 2 && c.state(0).mixed);
  EditReport r = c.setText(0, "Mixed + 1");
  R1_EXPECT(r.ok() && r.changed == 100000);
  R1_EXPECT(objects[0].v == 1.0 && objects[99999].v == static_cast<double>(99999 % 7) + 1.0);
  R1_EXPECT(c.undo().undoCount() == 1);
  R1_EXPECT(c.undo().undo().done && objects[5].v == 5.0);
  r = c.setValue(0, Value(std::numeric_limits<double>::quiet_NaN()));
  R1_EXPECT(r.code == EditCode::NotFinite && r.changed == 0 && objects[5].v == 5.0);
  r = c.setValue(0, Value(1e300));  // clamped to the hard range on every object
  R1_EXPECT(r.code == EditCode::Clamped && r.clamped == 100000 && objects[77].v == 1e6);
  r = c.reset(0);
  R1_EXPECT(r.changed == 100000 && objects[77].v == 1.0);
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("100k objects: select, 5 edits, undo in %.2f s\n", seconds);
  R1_EXPECT(seconds < 20.0);

  // Duplicates, nulls and the same object twice are dropped, not counted.
  std::vector<Target> messy = {targetOf(objects[0], tinySet()), targetOf(objects[0], tinySet()), Target{}, Target{&objects[1], nullptr}, Target{nullptr, &tinySet()}};
  R1_EXPECT(c.setSelection(messy) == 1);
}

void tenThousandProperties() {
  PropertySet<Tiny> s("Big");
  s.category("Bulk");
  for (int i = 0; i < 10000; ++i) s.add("p" + std::to_string(i), "Property " + std::to_string(i), &Tiny::v);
  Tiny a, b;
  PropertyContext c;
  const Target t[] = {targetOf(a, s), targetOf(b, s)};
  c.setSelection(t);
  R1_EXPECT(c.rowCount() == 10000);
  PanelState state;
  const PanelModel model = buildPanelModel(c, state);
  R1_EXPECT(model.totalRows == 10000);
  state.setSearch("property 9999");
  R1_EXPECT(buildPanelModel(c, state).totalRows == 1);
  // All 10000 rows alias one member; editing one is still a single step.
  R1_EXPECT(c.setValue(5000, Value(3.0)).ok() && a.v == 3.0 && c.undo().undoCount() == 1);
  R1_EXPECT(c.copyGroup("Bulk").size() > 10000 * 6);
}

void hostileValues() {
  Tiny a;
  Material m;
  PropertyContext c;
  const Target t[] = {targetOf(a, tinySet())};
  c.setSelection(t);
  const size_t text = row(c, "text");
  const size_t v = row(c, "v");

  R1_EXPECT(c.setValue(text, Value(std::string(10u << 20, 'x'))).code == EditCode::TooLong);  // 10 MiB
  R1_EXPECT(c.setText(text, std::string(kMaxPasteBytes + 1, 'x')).code == EditCode::TooLong);
  R1_EXPECT(c.setValue(text, Value(std::string("\xF0\x28\x8C\x28"))).code == EditCode::InvalidUtf8);
  R1_EXPECT(c.setValue(text, Value(std::string("embedded\0nul", 12))).code == EditCode::InvalidText);
  R1_EXPECT(a.text.empty() && c.undo().undoCount() == 0);

  for (const double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
    R1_EXPECT(c.setValue(v, Value(bad)).code == EditCode::NotFinite);
  }
  R1_EXPECT(c.setText(v, "1e999").code == EditCode::Unparsable);
  R1_EXPECT(c.setText(v, "0x10").code == EditCode::Unparsable);
  R1_EXPECT(c.setText(v, std::string(5000, '9')).code == EditCode::Unparsable);
  R1_EXPECT(c.setText(v, "").code == EditCode::Unparsable);
  R1_EXPECT(c.setText(v, "((((((((((((((((((((((((((((((((((((((((1))))))))))))))))))))))))))))))))))))))))").code == EditCode::Unparsable);
  R1_EXPECT(c.setValue(v, Value(std::string("1"))).code == EditCode::WrongType);
  R1_EXPECT(a.v == 1.0 && c.undo().undoCount() == 0);

  // Stale and out-of-range row indices are refused everywhere.
  const size_t bad = 12345;
  R1_EXPECT(c.setValue(bad, Value(1.0)).code == EditCode::NoSuchProperty);
  R1_EXPECT(c.setText(bad, "1").code == EditCode::NoSuchProperty);
  R1_EXPECT(c.reset(bad).code == EditCode::NoSuchProperty);
  R1_EXPECT(c.bind(bad, "x").code == EditCode::NoSuchProperty);
  R1_EXPECT(c.unbind(bad).code == EditCode::NoSuchProperty);
  R1_EXPECT(!c.copyValue(bad).has_value());
  R1_EXPECT(c.pasteValue(bad, "1").code == EditCode::NoSuchProperty);
  R1_EXPECT(!c.beginInteraction(bad));
  R1_EXPECT(c.state(bad).value == Value(false));

  // An empty selection refuses everything and copies an empty group.
  c.setSelection({});
  R1_EXPECT(c.setValue(0, Value(1.0)).code == EditCode::NoSuchProperty);
  R1_EXPECT(c.resetCategory("T").code == EditCode::NoSelection);
  R1_EXPECT(c.pasteGroup(std::nullopt, std::nullopt, "r1props 1\n").code == EditCode::NoSelection);
  R1_EXPECT(c.copyGroup(std::nullopt) == "r1props 1\n");
  R1_EXPECT(c.refreshBindings() == 0);
}

void bindingFlood() {
  struct Provider final : BindingProvider {
    std::optional<Value> resolve(std::string_view s) const override { return s == "one" ? std::optional<Value>(1.0) : std::nullopt; }
    std::vector<VariableInfo> variables() const override { return {}; }
  } provider;
  ContextOptions options;
  options.provider = &provider;
  PropertyContext c(options);
  std::vector<Tiny> objects(5000);
  std::vector<Target> targets;
  for (Tiny& t : objects) targets.push_back(targetOf(t, tinySet()));
  c.setSelection(targets);
  R1_EXPECT(c.bind(0, "one").changed == 5000 && c.bindings().size() == 5000);
  R1_EXPECT(c.state(0).binding == BindingState::Bound);
  R1_EXPECT(c.unbind(0).changed == 5000 && c.bindings().size() == 0);
  c.undo().undo();
  R1_EXPECT(c.bindings().size() == 5000);
  c.forgetObject(&objects[10]);
  R1_EXPECT(c.bindings().size() == 4999 && c.targetCount() == 4999);

  // The store itself refuses bad sources and keeps the previous binding.
  BindingStore store;
  R1_EXPECT(store.set(&objects[0], "v", BindingRecord{"a"}));
  R1_EXPECT(!store.set(&objects[0], "v", BindingRecord{""}) && store.find(&objects[0], "v")->source == "a");
  R1_EXPECT(!store.set(&objects[0], "v", BindingRecord{"\xFF"}));
  R1_EXPECT(store.set(&objects[0], "v", std::nullopt) && store.find(&objects[0], "v") == nullptr);
}

void collisionsAcrossTypes() {
  // Two types with a property of the same name and kind but different ranges/enums validate per object.
  struct A { double x = 0.0; };
  struct B { double x = 0.0; };
  PropertySet<A> sa("A");
  sa.add("x", "X", &A::x).range(0.0, 1.0);
  PropertySet<B> sb("B");
  sb.add("x", "X", &B::x).range(0.0, 100.0);
  A a;
  B b;
  PropertyContext c;
  const Target t[] = {targetOf(a, sa), targetOf(b, sb)};
  c.setSelection(t);
  R1_EXPECT(c.rowCount() == 1);
  const EditReport r = c.setValue(0, Value(50.0));
  R1_EXPECT(r.code == EditCode::Clamped && r.clamped == 1 && a.x == 1.0 && b.x == 50.0);
}

}  // namespace

int main() {
  hundredThousandObjects();
  tenThousandProperties();
  hostileValues();
  bindingFlood();
  collisionsAcrossTypes();
  return r1test::finish();
}
