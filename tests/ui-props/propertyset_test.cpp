// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of property declaration (PropertySet<T>, PropertyRef<T>): member pointers, getter/setter
//   pairs, computed read-only properties, kinds deduced from member types, metadata, enum entries,
//   defaults, display order, and the rejection of bad declarations (collisions, bad ranges, bad names)
//   without exceptions.
// Callers: CTest (fast tier, no GPU).
#include <cstdint>
#include <string>

#include "Samples.h"
#include "TestSupport.h"

namespace {

using namespace r1ui::props;
using namespace samples;

struct Narrow {
  uint8_t byte = 0;
  float f = 0.0f;
  int64_t big = 0;
  unsigned long long huge = 0;
};

bool isLarge(const Narrow& n) { return n.byte > 100; }

void declarationBasics() {
  const PropertySet<Transform>& t = transformSet();
  R1_EXPECT(t.errors().empty());
  R1_EXPECT(t.size() == 6);
  const auto position = t.find("position");
  R1_EXPECT(position.has_value() && t.at(*position).kind == ValueKind::Vec3);
  R1_EXPECT(t.at(*position).meta.unit == "m" && t.at(*position).meta.units.size() == 2);
  R1_EXPECT(t.at(*position).meta.min == -1000.0 && t.at(*position).meta.tooltip == "World position");
  R1_EXPECT(t.at(*t.find("name")).kind == ValueKind::String && t.at(*t.find("visible")).kind == ValueKind::Bool);
  R1_EXPECT(t.at(*t.find("layer")).kind == ValueKind::Int && t.at(*t.find("layer")).meta.advanced);
  R1_EXPECT(!t.find("nonsense").has_value());

  // Defaults came from a default-constructed object.
  R1_EXPECT(*t.at(*t.find("scale")).meta.defaultValue == Value(Vec3{1.0, 1.0, 1.0}));

  // The explicit category order (-10) puts "Transform" first even though "Object" was declared after it too.
  const auto order = t.displayOrder();
  R1_EXPECT(order.size() == 6 && t.at(order[0]).category == "Transform" && t.at(order[3]).category == "Object");

  // Get and set through the erased accessor.
  Transform tr;
  const PropertyDescriptor& pos = t.at(*position);
  R1_EXPECT(pos.accessor.get(&tr) == Value(Vec3{}));
  R1_EXPECT(pos.accessor.set(&tr, Value(Vec3{1.0, 2.0, 3.0})) && tr.position == (Vec3{1.0, 2.0, 3.0}));
  R1_EXPECT(!pos.accessor.set(&tr, Value(1.0)));  // wrong storage: refused, not converted
  R1_EXPECT(tr.position == (Vec3{1.0, 2.0, 3.0}));
}

void pairsAndEnums() {
  const PropertySet<Light>& s = lightSet();
  R1_EXPECT(s.errors().empty());
  Light light;
  const PropertyDescriptor& intensity = s.at(*s.find("intensity"));
  R1_EXPECT(intensity.kind == ValueKind::Double && intensity.writable());
  R1_EXPECT(intensity.accessor.set(&light, Value(250.0)) && light.intensity() == 250.0 && light.writes == 1);
  R1_EXPECT(intensity.meta.softMax == 1000.0 && intensity.meta.step == 10.0);

  const PropertyDescriptor& unit = s.at(*s.find("unit"));
  R1_EXPECT(unit.kind == ValueKind::Enum && unit.enumEntries.size() == 3);
  R1_EXPECT(unit.accessor.set(&light, Value(int64_t{2})) && light.unit() == LightUnit::Lux);
  R1_EXPECT(unit.findEnum("LUX") != nullptr && unit.findEnum(int64_t{1})->label == "Candela");

  // float members: the value narrows; a value beyond float range is refused.
  const PropertyDescriptor& radius = s.at(*s.find("radius"));
  R1_EXPECT(radius.accessor.set(&light, Value(2.5)) && light.radius() == 2.5f);
  R1_EXPECT(!radius.accessor.set(&light, Value(1e300)) && light.radius() == 2.5f);

  // A computed read-only property.
  PropertySet<Light> computed("Light2");
  computed.category("X");
  computed.addReadOnly("lumens", "Lumens", [](const Light& l) { return l.intensity() * 2.0; });
  const PropertyDescriptor& lumens = computed.at(0);
  R1_EXPECT(!lumens.writable() && lumens.accessor.get(&light) == Value(500.0));
  R1_EXPECT(!lumens.accessor.set(&light, Value(1.0)));
}

void integerNarrowing() {
  PropertySet<Narrow> s("Narrow");
  s.add("byte", "Byte", &Narrow::byte);
  s.add("f", "F", &Narrow::f);
  s.add("big", "Big", &Narrow::big);
  s.add("huge", "Huge", &Narrow::huge);
  R1_EXPECT(s.errors().empty());
  Narrow n;
  R1_EXPECT(s.at(0).accessor.set(&n, Value(int64_t{200})) && n.byte == 200);
  R1_EXPECT(!s.at(0).accessor.set(&n, Value(int64_t{300})) && n.byte == 200);  // out of range for the member: refused
  R1_EXPECT(!s.at(0).accessor.set(&n, Value(int64_t{-1})));
  n.huge = ~0ull;
  R1_EXPECT(s.at(3).accessor.get(&n) == Value(INT64_MAX));  // an unsigned member beyond int64 saturates on read
  R1_EXPECT(s.at(1).kind == ValueKind::Double);
}

void badDeclarations() {
  PropertySet<Transform> s("Bad");
  s.category("A");
  R1_EXPECT(s.add("x", "X", &Transform::layer).valid());
  R1_EXPECT(!s.add("x", "Duplicate", &Transform::visible).valid());  // name collision: first declaration is kept
  R1_EXPECT(s.size() == 1 && s.at(0).kind == ValueKind::Int && s.errors().size() == 1);
  R1_EXPECT(!s.add("", "Empty", &Transform::layer).valid());
  R1_EXPECT(!s.add("tab\tname", "Tab", &Transform::layer).valid());
  R1_EXPECT(!s.add("new\nline", "Newline", &Transform::layer).valid());
  R1_EXPECT(!s.add("bad\xC3", "Bad utf8", &Transform::layer).valid());
  R1_EXPECT(!s.add("label", "Bad \xFF label", &Transform::layer).valid());
  R1_EXPECT(!s.add(std::string(2000, 'n'), "Long", &Transform::layer).valid());
  R1_EXPECT(s.size() == 1);

  // Settings that do not fit are ignored and listed; the property stays declared.
  s.add("y", "Y", &Transform::layer).range(5.0, 1.0).softRange(std::nan(""), 1.0).step(-1.0).slider(0.0, 1.0);
  const PropertyDescriptor& y = s.at(*s.find("y"));
  R1_EXPECT(!y.meta.min && !y.meta.softMin && y.meta.step == 0.0 && y.kind == ValueKind::Int);
  s.add("z", "Z", &Transform::visible).range(0.0, 1.0).value("a", 1).defaultValue(5);
  const PropertyDescriptor& z = s.at(*s.find("z"));
  R1_EXPECT(!z.meta.min && z.enumEntries.empty() && !z.meta.defaultValue);
  s.add("n", "N", &Transform::layer).asEnum().value("one", 1).value("one", 2).value("uno", 1).value("two", 2);
  R1_EXPECT(s.at(*s.find("n")).enumEntries.size() == 2);  // duplicate name and duplicate value both rejected
  R1_EXPECT(s.errors().size() >= 10);

  // Errors are bounded no matter how many bad declarations arrive.
  for (int i = 0; i < 1000; ++i) s.add("x", "again", &Transform::layer);
  R1_EXPECT(s.errors().size() <= 64);

  // Conditions and predicates.
  PropertySet<Narrow> p("P");
  p.add("byte", "Byte", &Narrow::byte).enableIf<&isLarge>();
  Narrow big;
  big.byte = 200;
  Narrow small;
  R1_EXPECT(p.at(0).meta.enableWhen.predicate(&big) && !p.at(0).meta.enableWhen.predicate(&small));
}

void orderingWithGroups() {
  PropertySet<Transform> s("Order");
  s.category("B");
  s.add("b1", "B1", &Transform::layer);
  s.category("A");
  s.group("G2");
  s.add("a2", "A2", &Transform::layer);
  s.group("G1");
  s.add("a1", "A1", &Transform::visible);
  s.group("");
  s.add("a0", "A0", &Transform::name);
  s.add("a3", "A3", &Transform::position).order(-5).group("G2");
  const auto order = s.displayOrder();
  // Categories keep first appearance (B then A); inside A: ungrouped first, then groups by first
  // appearance (G2 then G1), inside a group by order then declaration.
  R1_EXPECT(order.size() == 5);
  R1_EXPECT(s.at(order[0]).name == "b1");
  R1_EXPECT(s.at(order[1]).name == "a0");
  R1_EXPECT(s.at(order[2]).name == "a3" && s.at(order[3]).name == "a2");
  R1_EXPECT(s.at(order[4]).name == "a1");
}

void manyProperties() {
  PropertySet<Transform> s("Many");
  s.category("Bulk");
  for (int i = 0; i < 10000; ++i) s.add("p" + std::to_string(i), "Property " + std::to_string(i), &Transform::layer);
  R1_EXPECT(s.size() == 10000 && s.errors().empty());
  R1_EXPECT(s.find("p9999").has_value() && s.displayOrder().size() == 10000);
}

}  // namespace

int main() {
  declarationBasics();
  pairsAndEnums();
  integerNarrowing();
  badDeclarations();
  orderingWithGroups();
  manyProperties();
  return r1test::finish();
}
