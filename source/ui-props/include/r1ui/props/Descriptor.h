// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PropertyDescriptor (the declared facts about one property: name, label, category, group, order,
//   value kind, metadata, enum entries, edit conditions, and the type-erased accessor that reads and
//   writes it on a host object), the Accessor itself, EditCode (why an edit was refused or adjusted) and
//   validate(), the single place where a proposed value is checked and clamped against a descriptor.
// Why: a panel is generated from these descriptors, so everything the panel and the undo stack need to
//   know about a property lives here and nowhere in widget code.
// Callers: PropertySet (builds descriptors), PropertyContext (validates and applies edits), UndoStack
//   (writes back through accessors), the property panel widget (reads metadata), tests.
// Accessor erasure: a descriptor stores two plain function pointers and up to kStorage bytes of
//   trivially copyable callable state (a member pointer, or a getter and a setter). Reading and writing
//   cost one indirect call; there is no std::function, no heap allocation and no virtual dispatch on the
//   per-object path, which matters when a selection holds thousands of objects.
// Invariants: Accessor::set returns false (never throws by itself) when the host type rejects the
//   value; a host setter that throws is caught by PropertyContext and UndoStack, which roll back.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/props/Expression.h"
#include "r1ui/props/Value.h"

namespace r1ui::props {

// ---- edit outcomes ------------------------------------------------------------------------------------

enum class EditCode : uint8_t {
  Ok,
  Clamped,        // applied, but the value was moved into the hard range (a warning)
  Unchanged,      // every object already had the value; nothing happened
  NoSelection,
  NoSuchProperty,
  WrongType,      // the value's storage does not match the property kind
  NotFinite,      // NaN or infinity
  InvalidEnum,    // not one of the property's entries
  InvalidUtf8,
  InvalidText,    // contains a NUL
  TooLong,
  Unparsable,     // typed or pasted text is not a value of this kind
  ReadOnly,
  Disabled,       // the edit condition is false
  Rejected,       // the host object refused the value
  NoDefault,      // reset asked for a property that has no default value
  SetterThrew,    // the host setter threw; the edit was rolled back
  InteractionOpen // an operation that needs a closed interaction was called during one
};
const char* editCodeName(EditCode code);
inline bool isError(EditCode code) { return code != EditCode::Ok && code != EditCode::Clamped && code != EditCode::Unchanged; }

// ---- accessor -----------------------------------------------------------------------------------------

class Accessor {
 public:
  static constexpr size_t kStorage = 48;
  using GetFn = Value (*)(const void* object, const unsigned char* state);
  using SetFn = bool (*)(void* object, const unsigned char* state, const Value& value);

  Accessor() = default;
  Accessor(GetFn get, SetFn set, const void* state, size_t stateBytes);

  bool valid() const { return get_ != nullptr; }
  bool writable() const { return set_ != nullptr; }
  Value get(const void* object) const { return get_(object, state_); }
  // False when the property has no setter or the host rejected the value. May propagate host exceptions.
  bool set(void* object, const Value& value) const { return set_ != nullptr && set_(object, state_, value); }

 private:
  GetFn get_ = nullptr;
  SetFn set_ = nullptr;
  alignas(std::max_align_t) unsigned char state_[kStorage] = {};
};

// ---- metadata -----------------------------------------------------------------------------------------

enum class CompareOp : uint8_t { Truthy, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };

// A rule tying a property's state to another property of the same object (by name), or to a captureless
// predicate over the object. A rule is absent when both are empty.
struct Condition {
  std::string property;
  CompareOp op = CompareOp::Truthy;
  Value operand = true;
  bool (*predicate)(const void* object) = nullptr;
  bool present() const { return predicate != nullptr || !property.empty(); }
};

struct EnumEntry {
  std::string name;   // stored/text name, matched case-insensitively when pasted or typed
  int64_t value = 0;
  std::string label;  // shown to the user; the name when empty
};

struct PropertyMeta {
  std::optional<double> min, max;            // hard range: no value leaves it
  std::optional<double> softMin, softMax;    // what a slider or scrub covers
  double step = 0.0;                         // fixed increment (0 = automatic)
  std::string unit;                          // display suffix of the stored unit
  std::vector<UnitConversion> units;         // suffixes accepted when typing, with factors to the stored unit
  int precision = -1;                        // fraction digits shown (-1 = automatic)
  std::string tooltip;
  bool readOnly = false;
  bool hidden = false;                       // never listed in a panel
  bool advanced = false;                     // listed only when advanced properties are shown
  bool multiline = false;                    // string edited in a multi-line box
  bool password = false;                     // string that is never displayed, copied or logged
  bool asSwitch = false;                     // bool shown as a switch instead of a checkbox
  bool resettable = true;                    // false: no reset affordance
  size_t maxLength = size_t{1} << 20;        // strings: byte limit
  std::optional<Value> defaultValue;
  Condition enableWhen;                      // false: the row is disabled
  Condition visibleWhen;                     // false: the row is removed
};

struct PropertyDescriptor {
  std::string name;      // stable key: identifies the property across types and in pasted text
  std::string label;     // shown to the user and used in undo step names
  std::string category;
  std::string group;     // optional sub-group inside the category
  int32_t order = 0;     // within the group, then declaration order
  ValueKind kind = ValueKind::Double;
  PropertyMeta meta;
  std::vector<EnumEntry> enumEntries;
  Accessor accessor;

  bool writable() const { return accessor.writable() && !meta.readOnly; }
  bool hasDefault() const { return meta.defaultValue.has_value(); }
  const std::string& shownLabel() const { return label.empty() ? name : label; }
  const EnumEntry* findEnum(int64_t value) const;
  const EnumEntry* findEnum(std::string_view name) const;  // by name, case-insensitive
};

// ---- validation and text ------------------------------------------------------------------------------

struct Validated {
  Value value;
  EditCode code = EditCode::Ok;  // Ok, Clamped or an error (value is then meaningless)
};

// Checks `proposed` against the kind and metadata: type, finiteness, enum membership, text validity and
// length; clamps to the hard range (rounding to the nearest integer for Int). Int and Range accept the
// other numeric storage (a finite double is rounded for Int).
Validated validate(const PropertyDescriptor& descriptor, Value proposed);

// The value as text (enum values by entry name) and the reverse. parseText accepts a unit suffix from the
// descriptor's units and an arithmetic expression for numeric kinds; it does not clamp (validate does).
std::string formatText(const PropertyDescriptor& descriptor, const Value& value);
std::optional<Value> parseText(const PropertyDescriptor& descriptor, std::string_view text);

// Case-insensitive (ASCII) containment test used by search; bytes outside ASCII compare exactly.
bool containsFolded(std::string_view haystack, std::string_view needleFolded);
std::string foldAscii(std::string_view text);

}  // namespace r1ui::props
