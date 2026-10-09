// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the value vocabulary of ui-props: the closed set of storage types a property can hold (bool,
//   int64, double, string, RGBA colour, vec2, vec3), the ValueKind a descriptor presents them as (enum
//   and range reuse int64 and double storage), equality that is total over NaN, UTF-8 validation and the
//   text form of a value (copy and paste, binding sources).
// Why: a property panel edits many unrelated host types through one interface; one small variant keeps
//   the descriptor, undo and panel code free of templates and lets values be stored in undo steps.
// Callers: Descriptor, PropertySet, PropertyContext, UndoStack, the property panel widget, tests.
// Invariants: every function is total (no throw on any input); text produced by formatValue parses back
//   to an equal value with parseValue for the same kind; non-finite numbers never format.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace r1ui::props {

// ---- storage types --------------------------------------------------------------------------------

struct Color {
  float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
  friend bool operator==(const Color&, const Color&) = default;
};
struct Vec2 {
  double x = 0.0, y = 0.0;
  friend bool operator==(const Vec2&, const Vec2&) = default;
};
struct Vec3 {
  double x = 0.0, y = 0.0, z = 0.0;
  friend bool operator==(const Vec3&, const Vec3&) = default;
};

// The alternatives are indexed by `Storage`; enum values travel as int64 and range values as double.
using Value = std::variant<bool, int64_t, double, std::string, Color, Vec2, Vec3>;
enum class Storage : uint8_t { Bool, Int, Double, String, Color, Vec2, Vec3 };

// What a property is presented as. Enum is int64 storage with named entries; Range is double storage
// shown as a slider.
enum class ValueKind : uint8_t { Bool, Int, Double, String, Enum, Color, Vec2, Vec3, Range };

constexpr Storage storageOf(ValueKind kind) {
  switch (kind) {
    case ValueKind::Bool: return Storage::Bool;
    case ValueKind::Int:
    case ValueKind::Enum: return Storage::Int;
    case ValueKind::Double:
    case ValueKind::Range: return Storage::Double;
    case ValueKind::String: return Storage::String;
    case ValueKind::Color: return Storage::Color;
    case ValueKind::Vec2: return Storage::Vec2;
    case ValueKind::Vec3: return Storage::Vec3;
  }
  return Storage::Bool;
}
// Axes of a vector (x, y, z) or channels of a colour (r, g, b, a); 1 for every other kind.
constexpr size_t componentCount(ValueKind kind) { return kind == ValueKind::Vec2 ? 2 : kind == ValueKind::Vec3 ? 3 : kind == ValueKind::Color ? 4 : 1; }
const char* kindName(ValueKind kind);

// ---- queries ----------------------------------------------------------------------------------------

inline Storage storageOf(const Value& v) { return static_cast<Storage>(v.index()); }
bool matchesKind(const Value& v, ValueKind kind);
// The zero value of the kind's storage (false, 0, empty string, opaque black, origin).
Value zeroValue(ValueKind kind);
// Equality that treats two NaN doubles as equal, so a host returning NaN cannot make a selection
// appear mixed with itself.
bool valuesEqual(const Value& a, const Value& b);
// False when any number in the value is NaN or infinite.
bool isFinite(const Value& v);
// The `index`-th numeric component (vec2/vec3 axes, colour channels; 0 for scalars); nullopt for other
// storage or an index past the end.
std::optional<double> componentOf(const Value& v, size_t index);
// A copy of `v` with component `index` replaced; unchanged when `v` has no such component.
Value withComponent(const Value& v, size_t index, double component);
// Approximate heap and inline footprint, for the undo budget.
size_t valueBytes(const Value& v);

// ---- text -------------------------------------------------------------------------------------------

// Strict UTF-8 (no overlongs, surrogates or code points above U+10FFFF).
bool isValidUtf8(std::string_view text);

// Text form used for copy/paste. Bool: true/false. Int: decimal. Double: shortest round-trip. String:
// as is. Color: rgba(r,g,b,a). Vec: (x,y[,z]). Non-finite numbers give an empty string.
std::string formatValue(const Value& v);
// Parses text for a kind other than Enum (which needs names, see Descriptor.h). Accepts surrounding
// blanks; bool accepts true/false/on/off/yes/no/1/0; color accepts #rgb, #rgba, #rrggbb, #rrggbbaa and
// rgba(...); vectors accept (x,y,z), "x,y,z" and "x y z". Empty on any malformed or non-finite input.
std::optional<Value> parseValue(std::string_view text, ValueKind kind);

}  // namespace r1ui::props
