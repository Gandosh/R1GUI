// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the implementation of Descriptor.h: Accessor construction, enum lookup, validate() (the one
//   place a proposed value is checked and clamped), and the text form of values per descriptor.
// Why: see Descriptor.h. All functions are total; hostile input (NaN, huge numbers, long or malformed
//   strings) yields an EditCode, never an exception.
// Callers: PropertySet.cpp, PropertyContext*.cpp, UndoStack.cpp, tests.
#include "r1ui/props/Descriptor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace r1ui::props {

// ---- codes and accessor -------------------------------------------------------------------------------

const char* editCodeName(EditCode code) {
  switch (code) {
    case EditCode::Ok: return "ok";
    case EditCode::Clamped: return "clamped to the allowed range";
    case EditCode::Unchanged: return "unchanged";
    case EditCode::NoSelection: return "nothing is selected";
    case EditCode::NoSuchProperty: return "no such property";
    case EditCode::WrongType: return "wrong value type";
    case EditCode::NotFinite: return "not a finite number";
    case EditCode::InvalidEnum: return "not one of the allowed choices";
    case EditCode::InvalidUtf8: return "not valid UTF-8";
    case EditCode::InvalidText: return "contains a control character";
    case EditCode::TooLong: return "too long";
    case EditCode::Unparsable: return "not a value of this type";
    case EditCode::ReadOnly: return "read-only";
    case EditCode::Disabled: return "disabled by its condition";
    case EditCode::Rejected: return "rejected by the object";
    case EditCode::NoDefault: return "has no default value";
    case EditCode::SetterThrew: return "the object failed while applying it";
    case EditCode::InteractionOpen: return "an edit is already in progress";
  }
  return "?";
}

Accessor::Accessor(GetFn get, SetFn set, const void* state, size_t stateBytes) : get_(get), set_(set) {
  if (stateBytes > kStorage) {
    get_ = nullptr;  // PropertySet checks this at compile time; a runtime caller gets an invalid accessor
    set_ = nullptr;
    return;
  }
  if (stateBytes > 0) std::memcpy(state_, state, stateBytes);
}

const EnumEntry* PropertyDescriptor::findEnum(int64_t value) const {
  for (const EnumEntry& e : enumEntries) {
    if (e.value == value) return &e;
  }
  return nullptr;
}

const EnumEntry* PropertyDescriptor::findEnum(std::string_view entryName) const {
  const std::string folded = foldAscii(entryName);
  for (const EnumEntry& e : enumEntries) {
    if (foldAscii(e.name) == folded) return &e;
  }
  return nullptr;
}

// ---- validation ---------------------------------------------------------------------------------------

namespace {

// Clamps to the optional hard range; true when the value moved.
bool clampTo(double& v, const PropertyMeta& meta) {
  const double before = v;
  if (meta.min && v < *meta.min) v = *meta.min;
  if (meta.max && v > *meta.max) v = *meta.max;
  return v != before;
}

// The numeric value of an int64 or double proposal.
std::optional<double> numeric(const Value& v) {
  if (const auto* d = std::get_if<double>(&v)) return *d;
  if (const auto* i = std::get_if<int64_t>(&v)) return static_cast<double>(*i);
  return std::nullopt;
}

Validated fail(EditCode code) { return {Value(false), code}; }

// Rounds to the nearest integer and clamps into int64; sets `moved` when clamping was needed.
int64_t roundToInt64(double v, bool& moved) {
  const double rounded = std::round(v);
  constexpr double kLimit = 9.2233720368547748e18;  // 2^63, exactly representable
  if (rounded >= kLimit) {
    moved = true;
    return std::numeric_limits<int64_t>::max();
  }
  if (rounded <= -kLimit) {
    moved = true;
    return std::numeric_limits<int64_t>::min();
  }
  return static_cast<int64_t>(rounded);
}

Validated validateInt(const PropertyDescriptor& d, const Value& proposed) {
  bool moved = false;
  int64_t value = 0;
  if (const auto* i = std::get_if<int64_t>(&proposed)) {
    value = *i;
  } else if (const auto* x = std::get_if<double>(&proposed)) {
    if (!std::isfinite(*x)) return fail(EditCode::NotFinite);
    value = roundToInt64(*x, moved);
  } else {
    return fail(EditCode::WrongType);
  }
  // Hard range, compared in double: exact up to 2^53, which covers every practical range.
  if (d.meta.min && static_cast<double>(value) < *d.meta.min) {
    value = static_cast<int64_t>(std::ceil(*d.meta.min));
    moved = true;
  }
  if (d.meta.max && static_cast<double>(value) > *d.meta.max) {
    value = static_cast<int64_t>(std::floor(*d.meta.max));
    moved = true;
  }
  return {Value(value), moved ? EditCode::Clamped : EditCode::Ok};
}

Validated validateDouble(const PropertyDescriptor& d, const Value& proposed) {
  const auto n = numeric(proposed);
  if (!n) return fail(EditCode::WrongType);
  if (!std::isfinite(*n)) return fail(EditCode::NotFinite);
  double v = *n;
  const bool moved = clampTo(v, d.meta);
  return {Value(v), moved ? EditCode::Clamped : EditCode::Ok};
}

Validated validateEnum(const PropertyDescriptor& d, const Value& proposed) {
  const auto n = numeric(proposed);
  if (!n) return fail(EditCode::WrongType);
  if (!std::isfinite(*n) || *n != std::round(*n)) return fail(EditCode::InvalidEnum);
  bool moved = false;
  const int64_t v = roundToInt64(*n, moved);
  if (moved || d.findEnum(v) == nullptr) return fail(EditCode::InvalidEnum);
  return {Value(v), EditCode::Ok};
}

Validated validateString(const PropertyDescriptor& d, const Value& proposed) {
  const auto* s = std::get_if<std::string>(&proposed);
  if (s == nullptr) return fail(EditCode::WrongType);
  if (s->size() > d.meta.maxLength) return fail(EditCode::TooLong);
  if (!isValidUtf8(*s)) return fail(EditCode::InvalidUtf8);
  if (s->find('\0') != std::string::npos) return fail(EditCode::InvalidText);
  return {proposed, EditCode::Ok};
}

Validated validateColor(const Value& proposed) {
  const auto* c = std::get_if<Color>(&proposed);
  if (c == nullptr) return fail(EditCode::WrongType);
  if (!isFinite(proposed)) return fail(EditCode::NotFinite);
  Color out = *c;
  const auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
  out.r = clamp01(out.r);
  out.g = clamp01(out.g);
  out.b = clamp01(out.b);
  out.a = clamp01(out.a);
  return {Value(out), out == *c ? EditCode::Ok : EditCode::Clamped};
}

Validated validateVector(const PropertyDescriptor& d, const Value& proposed) {
  if (!matchesKind(proposed, d.kind)) return fail(EditCode::WrongType);
  if (!isFinite(proposed)) return fail(EditCode::NotFinite);
  Value out = proposed;
  bool moved = false;
  for (size_t i = 0; i < componentCount(d.kind); ++i) {
    double c = *componentOf(out, i);
    if (clampTo(c, d.meta)) {
      moved = true;
      out = withComponent(out, i, c);
    }
  }
  return {out, moved ? EditCode::Clamped : EditCode::Ok};
}

}  // namespace

Validated validate(const PropertyDescriptor& d, Value proposed) {
  switch (d.kind) {
    case ValueKind::Bool:
      if (!std::holds_alternative<bool>(proposed)) return fail(EditCode::WrongType);
      return {std::move(proposed), EditCode::Ok};
    case ValueKind::Int: return validateInt(d, proposed);
    case ValueKind::Double:
    case ValueKind::Range: return validateDouble(d, proposed);
    case ValueKind::Enum: return validateEnum(d, proposed);
    case ValueKind::String: return validateString(d, proposed);
    case ValueKind::Color: return validateColor(proposed);
    case ValueKind::Vec2:
    case ValueKind::Vec3: return validateVector(d, proposed);
  }
  return fail(EditCode::WrongType);
}

// ---- text ---------------------------------------------------------------------------------------------

std::string formatText(const PropertyDescriptor& d, const Value& value) {
  if (d.kind == ValueKind::Enum) {
    if (const auto* i = std::get_if<int64_t>(&value)) {
      if (const EnumEntry* e = d.findEnum(*i)) return e->name;
    }
  }
  return formatValue(value);
}

std::optional<Value> parseText(const PropertyDescriptor& d, std::string_view text) {
  if (d.kind == ValueKind::Enum) {
    if (const EnumEntry* e = d.findEnum(text)) return Value(e->value);
    return parseValue(text, ValueKind::Int);
  }
  if (d.kind != ValueKind::Int && d.kind != ValueKind::Double && d.kind != ValueKind::Range) return parseValue(text, d.kind);

  std::vector<UnitConversion> units = d.meta.units;
  if (!d.meta.unit.empty()) units.push_back({d.meta.unit, 1.0});
  const auto expression = parseExpression(text, {.units = units, .allowMixed = false});
  if (!expression) return std::nullopt;
  const double v = expression->evaluate();
  if (!std::isfinite(v)) return std::nullopt;
  if (d.kind != ValueKind::Int) return Value(v);
  bool moved = false;
  const int64_t rounded = roundToInt64(v, moved);
  if (moved) return std::nullopt;
  return Value(rounded);
}

std::string foldAscii(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

bool containsFolded(std::string_view haystack, std::string_view needleFolded) {
  if (needleFolded.empty()) return true;
  if (haystack.size() < needleFolded.size()) return false;
  for (size_t i = 0; i + needleFolded.size() <= haystack.size(); ++i) {
    size_t k = 0;
    for (; k < needleFolded.size(); ++k) {
      char c = haystack[i + k];
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
      if (c != needleFolded[k]) break;
    }
    if (k == needleFolded.size()) return true;
  }
  return false;
}

}  // namespace r1ui::props
