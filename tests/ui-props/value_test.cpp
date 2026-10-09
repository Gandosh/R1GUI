// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the value layer of ui-props: Value text round trips and parsing, UTF-8 validation,
//   NaN-total equality, the expression language (precedence, units, the Mixed token, limits, hostile
//   text) and validate() (type checks, clamping, rounding, enum membership, strings).
// Callers: CTest (fast tier, no GPU).
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "Samples.h"
#include "TestSupport.h"
#include "r1ui/props/Expression.h"

namespace {

using namespace r1ui::props;

PropertyDescriptor numberDescriptor(ValueKind kind) {
  PropertyDescriptor d;
  d.name = "n";
  d.kind = kind;
  return d;
}

void valueText() {
  R1_EXPECT(formatValue(Value(true)) == "true");
  R1_EXPECT(formatValue(Value(int64_t{-42})) == "-42");
  R1_EXPECT(formatValue(Value(0.1)) == "0.1");
  R1_EXPECT(std::get<double>(*parseValue(formatValue(Value(0.1 + 0.2)), ValueKind::Double)) == 0.1 + 0.2);  // round trips exactly
  R1_EXPECT(std::get<Vec3>(*parseValue("(1, 2.5, -3)", ValueKind::Vec3)) == (Vec3{1.0, 2.5, -3.0}));
  R1_EXPECT(std::get<Vec3>(*parseValue("1 2 3", ValueKind::Vec3)) == (Vec3{1.0, 2.0, 3.0}));
  R1_EXPECT(!parseValue("(1,2)", ValueKind::Vec3));
  R1_EXPECT(!parseValue("(1,2,3,4)", ValueKind::Vec3));
  R1_EXPECT(std::get<Vec2>(*parseValue(formatValue(Value(Vec2{0.25, -8.0})), ValueKind::Vec2)) == (Vec2{0.25, -8.0}));
  const Color c{0.25f, 0.5f, 0.75f, 1.0f};
  R1_EXPECT(std::get<Color>(*parseValue(formatValue(Value(c)), ValueKind::Color)) == c);
  R1_EXPECT(std::get<Color>(*parseValue("#ff8000", ValueKind::Color)).g > 0.49f);
  R1_EXPECT(std::get<Color>(*parseValue("#f00", ValueKind::Color)) == (Color{1.0f, 0.0f, 0.0f, 1.0f}));
  R1_EXPECT(std::get<Color>(*parseValue("#ff000080", ValueKind::Color)).a > 0.49f);
  R1_EXPECT(!parseValue("#ggg", ValueKind::Color));
  R1_EXPECT(!parseValue("#12345", ValueKind::Color));
  R1_EXPECT(std::get<bool>(*parseValue(" On ", ValueKind::Bool)));
  R1_EXPECT(!std::get<bool>(*parseValue("0", ValueKind::Bool)));
  R1_EXPECT(!parseValue("maybe", ValueKind::Bool));
  R1_EXPECT(!parseValue("nan", ValueKind::Double));
  R1_EXPECT(!parseValue("inf", ValueKind::Double));
  R1_EXPECT(!parseValue("1e999", ValueKind::Double));
  R1_EXPECT(!parseValue("0x10", ValueKind::Double));
  R1_EXPECT(!parseValue("", ValueKind::Double));
  R1_EXPECT(!parseValue(std::string(100, '1'), ValueKind::Double));  // token longer than any double needs
  R1_EXPECT(formatValue(Value(std::numeric_limits<double>::quiet_NaN())).empty());
  R1_EXPECT(formatValue(Value(Vec3{1.0, std::numeric_limits<double>::infinity(), 0.0})).empty());
}

void utf8AndEquality() {
  R1_EXPECT(isValidUtf8("plain"));
  R1_EXPECT(isValidUtf8("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80"));
  R1_EXPECT(!isValidUtf8("\xC3"));          // truncated
  R1_EXPECT(!isValidUtf8("\xC0\x80"));      // overlong NUL
  R1_EXPECT(!isValidUtf8("\xED\xA0\x80"));  // surrogate
  R1_EXPECT(!isValidUtf8("\xF4\x90\x80\x80"));  // above U+10FFFF
  R1_EXPECT(!isValidUtf8("\xFF"));
  const double nan = std::numeric_limits<double>::quiet_NaN();
  R1_EXPECT(valuesEqual(Value(nan), Value(nan)));  // total: a NaN host value does not look mixed with itself
  R1_EXPECT(!valuesEqual(Value(1.0), Value(int64_t{1})));
  R1_EXPECT(!valuesEqual(Value(Vec2{1.0, 2.0}), Value(Vec2{1.0, 3.0})));
  R1_EXPECT(!isFinite(Value(Color{0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f})));
  R1_EXPECT(componentOf(Value(Vec3{1, 2, 3}), 2) == 3.0);
  R1_EXPECT(!componentOf(Value(Vec2{1, 2}), 2).has_value());
  R1_EXPECT(std::get<Vec3>(withComponent(Value(Vec3{1, 2, 3}), 1, 9.0)) == (Vec3{1, 9, 3}));
}

void expressions() {
  const auto eval = [](const char* text, double mixed = 0.0, std::span<const UnitConversion> units = {}) {
    const auto e = parseExpression(text, {.units = units, .allowMixed = true});
    return e ? e->evaluate(mixed) : std::numeric_limits<double>::quiet_NaN();
  };
  R1_EXPECT(eval("1+2*3") == 7.0);
  R1_EXPECT(eval("(1+2)*3") == 9.0);
  R1_EXPECT(eval("-2 * -3") == 6.0);
  R1_EXPECT(eval("10/4") == 2.5);
  R1_EXPECT(eval("1e2 + .5") == 100.5);
  R1_EXPECT(eval("Mixed + 5", 10.0) == 15.0);
  R1_EXPECT(eval("mixed*2", 4.0) == 8.0);
  R1_EXPECT(eval("2 \xE2\x88\x92 1") == 1.0);  // U+2212
  R1_EXPECT(std::isnan(eval("1/0")));
  R1_EXPECT(!parseExpression("1/0").has_value());  // a constant that is not finite is refused
  R1_EXPECT(std::isnan(eval("Mixed/0", 1.0)));
  R1_EXPECT(!parseExpression("").has_value());
  R1_EXPECT(!parseExpression("1 +").has_value());
  R1_EXPECT(!parseExpression("(1").has_value());
  R1_EXPECT(!parseExpression("1 2").has_value());
  R1_EXPECT(!parseExpression("nan").has_value());
  R1_EXPECT(!parseExpression("inf").has_value());
  R1_EXPECT(!parseExpression("0x1F").has_value());
  R1_EXPECT(!parseExpression("1e999").has_value());
  R1_EXPECT(!parseExpression("Mixed", {.units = {}, .allowMixed = false}).has_value());
  R1_EXPECT(!parseExpression(std::string(kMaxExpressionBytes + 1, '1')).has_value());
  R1_EXPECT(!parseExpression(std::string(200, '(') + "1" + std::string(200, ')')).has_value());  // nesting bound
  R1_EXPECT(!parseExpression(std::string(100, '-') + "1").has_value());  // a deep unary chain is bounded, not recursed
  std::string many = "1";
  for (int i = 0; i < 200; ++i) many += "+1";
  R1_EXPECT(!parseExpression(many).has_value());  // node bound

  const std::vector<UnitConversion> units = {{"cm", 0.01}, {"mm", 0.001}, {"m", 1.0}, {"km", 1000.0}};
  R1_EXPECT_NEAR(eval("50cm", 0.0, units), 0.5, 1e-12);
  R1_EXPECT_NEAR(eval("2 m + 30 cm", 0.0, units), 2.3, 1e-12);
  R1_EXPECT_NEAR(eval("1.5km", 0.0, units), 1500.0, 1e-9);
  R1_EXPECT_NEAR(eval("5 MM", 0.0, units), 0.005, 1e-12);  // suffixes match ignoring case
  R1_EXPECT(!parseExpression("5 parsecs", {.units = units, .allowMixed = true}).has_value());
  R1_EXPECT_NEAR(eval("Mixed + 10cm", 1.0, units), 1.1, 1e-12);
}

void validation() {
  PropertyDescriptor d = numberDescriptor(ValueKind::Double);
  d.meta.min = 0.0;
  d.meta.max = 10.0;
  Validated v = validate(d, Value(15.0));
  R1_EXPECT(v.code == EditCode::Clamped && std::get<double>(v.value) == 10.0);
  v = validate(d, Value(int64_t{3}));
  R1_EXPECT(v.code == EditCode::Ok && std::get<double>(v.value) == 3.0);  // int64 accepted for a double kind
  R1_EXPECT(validate(d, Value(std::numeric_limits<double>::quiet_NaN())).code == EditCode::NotFinite);
  R1_EXPECT(validate(d, Value(std::numeric_limits<double>::infinity())).code == EditCode::NotFinite);
  R1_EXPECT(validate(d, Value(std::string("1"))).code == EditCode::WrongType);

  PropertyDescriptor i = numberDescriptor(ValueKind::Int);
  i.meta.min = 0.0;
  i.meta.max = 100.0;
  R1_EXPECT(std::get<int64_t>(validate(i, Value(2.5)).value) == 3);  // rounds to nearest
  R1_EXPECT(validate(i, Value(1e30)).code == EditCode::Clamped);
  R1_EXPECT(std::get<int64_t>(validate(i, Value(int64_t{500})).value) == 100);
  R1_EXPECT(validate(i, Value(std::numeric_limits<double>::quiet_NaN())).code == EditCode::NotFinite);
  PropertyDescriptor unbounded = numberDescriptor(ValueKind::Int);
  R1_EXPECT(std::get<int64_t>(validate(unbounded, Value(1e30)).value) == std::numeric_limits<int64_t>::max());

  PropertyDescriptor e = numberDescriptor(ValueKind::Enum);
  e.enumEntries = {{"a", 1, ""}, {"b", 7, "B"}};
  R1_EXPECT(validate(e, Value(int64_t{7})).code == EditCode::Ok);
  R1_EXPECT(validate(e, Value(int64_t{2})).code == EditCode::InvalidEnum);
  R1_EXPECT(validate(e, Value(1.5)).code == EditCode::InvalidEnum);
  R1_EXPECT(formatText(e, Value(int64_t{7})) == "b");
  R1_EXPECT(std::get<int64_t>(*parseText(e, "B")) == 7);  // names match ignoring case
  R1_EXPECT(std::get<int64_t>(*parseText(e, "1")) == 1);

  PropertyDescriptor s = numberDescriptor(ValueKind::String);
  s.meta.maxLength = 8;
  R1_EXPECT(validate(s, Value(std::string("short"))).code == EditCode::Ok);
  R1_EXPECT(validate(s, Value(std::string("far too long"))).code == EditCode::TooLong);
  R1_EXPECT(validate(s, Value(std::string("\xC3"))).code == EditCode::InvalidUtf8);
  R1_EXPECT(validate(s, Value(std::string("a\0b", 3))).code == EditCode::InvalidText);

  PropertyDescriptor c = numberDescriptor(ValueKind::Color);
  R1_EXPECT(validate(c, Value(Color{2.0f, -1.0f, 0.5f, 1.0f})).code == EditCode::Clamped);
  R1_EXPECT(validate(c, Value(Color{0, 0, 0, std::numeric_limits<float>::quiet_NaN()})).code == EditCode::NotFinite);

  PropertyDescriptor vec = numberDescriptor(ValueKind::Vec3);
  vec.meta.min = -1.0;
  vec.meta.max = 1.0;
  const Validated clamped = validate(vec, Value(Vec3{5.0, 0.5, -5.0}));
  R1_EXPECT(clamped.code == EditCode::Clamped && std::get<Vec3>(clamped.value) == (Vec3{1.0, 0.5, -1.0}));
  R1_EXPECT(validate(vec, Value(Vec2{})).code == EditCode::WrongType);

  // Typed text for numbers: units, expressions, integer rounding.
  PropertyDescriptor length = numberDescriptor(ValueKind::Double);
  length.meta.unit = "m";
  length.meta.units = {{"cm", 0.01}};
  R1_EXPECT_NEAR(std::get<double>(*parseText(length, "150cm")), 1.5, 1e-12);
  R1_EXPECT_NEAR(std::get<double>(*parseText(length, "2m + 50cm")), 2.5, 1e-12);
  R1_EXPECT(!parseText(length, "abc").has_value());
  R1_EXPECT(std::get<int64_t>(*parseText(i, "7/2")) == 4);
  R1_EXPECT(!parseText(i, "1e30").has_value());  // beyond int64: refused, not wrapped
}

}  // namespace

int main() {
  valueText();
  utf8AndEquality();
  expressions();
  validation();
  return r1test::finish();
}
