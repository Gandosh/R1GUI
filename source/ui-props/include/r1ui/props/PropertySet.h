// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the declaration API of properties for a plain C++ type without reflection: PropertySet<T> (a
//   list of PropertyDescriptors for T) and the fluent PropertyRef<T> returned by each add(), plus Target
//   (an object paired with its set, the element of a selection).
// Why: hosts describe their types once, next to the type, with member pointers or getter/setter pairs;
//   the type-safe templates here convert between the host's member types and the closed Value variant
//   so everything downstream (context, undo, panel) is non-template code.
// Callers: host code and tests declare sets; PropertyContext reads them through PropertySetBase.
// Member types understood: bool; any integer type (checked against its range on write); float and
//   double (a value that overflows float is refused); std::string; enum and enum class (int64 storage,
//   entries named with value()); props::Color, props::Vec2, props::Vec3. Anything else fails to compile.
// Getter/setter pairs: a getter is any callable taking `const T&` (a member function pointer, a function
//   pointer or a captureless lambda), a setter any callable taking `T&` and the value (it may return
//   bool to refuse a value). Both must be trivially copyable and fit Accessor::kStorage together; this
//   is checked at compile time. Capturing callables are not supported by design (see Descriptor.h).
// Declaration problems (empty or duplicate names, bad ranges, a default of the wrong type, invalid
//   UTF-8) never throw: the property is skipped or the setting ignored and the problem is listed in
//   errors(), so a broken declaration cannot take a panel down.
// Lifetime: a PropertySet must outlive every Target, UndoStack step and context that refers to it.
// Threading: declare sets before use; afterwards they are read-only except displayOrder()'s cache.
#pragma once

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "r1ui/props/Descriptor.h"

namespace r1ui::props {

inline constexpr size_t kMaxProperties = 100000;
inline constexpr size_t kMaxNameBytes = 1024;

// ---- the type-independent part ------------------------------------------------------------------------

class PropertySetBase {
 public:
  PropertySetBase(std::string typeName, std::type_index type) : typeName_(std::move(typeName)), type_(type) {}
  virtual ~PropertySetBase() = default;

  const std::string& typeName() const { return typeName_; }
  std::type_index type() const { return type_; }
  size_t size() const { return properties_.size(); }
  const PropertyDescriptor& at(size_t index) const { return properties_[index]; }
  std::span<const PropertyDescriptor> properties() const { return properties_; }
  std::optional<size_t> find(std::string_view name) const;
  const std::vector<std::string>& errors() const { return errors_; }

  // Indices of properties() in display order: categories by explicit order then first appearance, inside
  // a category ungrouped properties first, then groups by first appearance, then `order`, then declaration.
  std::span<const size_t> displayOrder() const;

 protected:
  template <class T>
  friend class PropertyRef;
  // Adds a descriptor; nullopt (and an entry in errors()) when it is rejected.
  std::optional<size_t> addDescriptor(PropertyDescriptor descriptor);
  PropertyDescriptor& mutableAt(size_t index) {
    order_.clear();
    return properties_[index];
  }
  void noteError(std::string message);
  void setCategoryOrder(const std::string& category, int order);

  std::string currentCategory_;
  std::string currentGroup_;

 private:
  std::string typeName_;
  std::type_index type_;
  std::vector<PropertyDescriptor> properties_;
  std::unordered_map<std::string, size_t> byName_;
  std::unordered_map<std::string, int> categoryOrder_;
  std::vector<std::string> errors_;
  mutable std::vector<size_t> order_;  // cache of displayOrder(), cleared on every change
};

// An object paired with the set that describes it: one element of a selection. Both must outlive the
// context that holds them; a null member makes the target invalid and it is ignored.
struct Target {
  void* object = nullptr;
  const PropertySetBase* set = nullptr;
  bool valid() const { return object != nullptr && set != nullptr; }
};

// ---- member type conversions --------------------------------------------------------------------------

namespace detail {

template <class M>
inline constexpr bool kIsEnum = std::is_enum_v<M>;
template <class M>
inline constexpr bool kIsInteger = std::is_integral_v<M> && !std::is_same_v<M, bool>;

// The integer type behind an integer or enum member.
template <class M, bool = std::is_enum_v<M>>
struct RawOf {
  using type = M;
};
template <class M>
struct RawOf<M, true> {
  using type = std::underlying_type_t<M>;
};

template <class M>
constexpr ValueKind kindFor() {
  if constexpr (std::is_same_v<M, bool>) return ValueKind::Bool;
  else if constexpr (kIsInteger<M>) return ValueKind::Int;
  else if constexpr (std::is_floating_point_v<M>) return ValueKind::Double;
  else if constexpr (kIsEnum<M>) return ValueKind::Enum;
  else if constexpr (std::is_same_v<M, std::string>) return ValueKind::String;
  else if constexpr (std::is_same_v<M, Color>) return ValueKind::Color;
  else if constexpr (std::is_same_v<M, Vec2>) return ValueKind::Vec2;
  else if constexpr (std::is_same_v<M, Vec3>) return ValueKind::Vec3;
  else static_assert(sizeof(M) == 0, "unsupported property member type; see PropertySet.h");
}

template <class M>
Value toValue(const M& v) {
  if constexpr (std::is_same_v<M, bool>) {
    return v;
  } else if constexpr (kIsInteger<M>) {
    if constexpr (std::is_unsigned_v<M> && sizeof(M) >= sizeof(int64_t)) {
      return v > static_cast<M>(std::numeric_limits<int64_t>::max()) ? std::numeric_limits<int64_t>::max() : static_cast<int64_t>(v);
    } else {
      return static_cast<int64_t>(v);
    }
  } else if constexpr (std::is_floating_point_v<M>) {
    return static_cast<double>(v);
  } else if constexpr (kIsEnum<M>) {
    return static_cast<int64_t>(static_cast<std::underlying_type_t<M>>(v));
  } else {
    return v;  // string, Color, Vec2, Vec3
  }
}

// Converts a validated Value back to the member type; nullopt when it does not fit.
template <class M>
std::optional<M> fromValue(const Value& value) {
  if constexpr (std::is_same_v<M, bool>) {
    if (const auto* b = std::get_if<bool>(&value)) return *b;
  } else if constexpr (kIsInteger<M> || kIsEnum<M>) {
    using Raw = typename RawOf<M>::type;
    if (const auto* i = std::get_if<int64_t>(&value)) {
      if (std::in_range<Raw>(*i)) return static_cast<M>(static_cast<Raw>(*i));
    }
  } else if constexpr (std::is_floating_point_v<M>) {
    if (const auto* d = std::get_if<double>(&value)) {
      const M narrowed = static_cast<M>(*d);
      if (std::isfinite(narrowed)) return narrowed;
    }
  } else {
    if (const auto* p = std::get_if<M>(&value)) return *p;
  }
  return std::nullopt;
}

// ---- accessor state and trampolines ---------------------------------------------------------------------

template <class T, class C, class M>
struct MemberState {
  M C::* member;
};

template <class T, class C, class M>
Value getMember(const void* object, const unsigned char* state) {
  MemberState<T, C, M> s;
  std::memcpy(&s, state, sizeof s);
  return toValue<M>(static_cast<const T*>(object)->*(s.member));
}

template <class T, class C, class M>
bool setMember(void* object, const unsigned char* state, const Value& value) {
  MemberState<T, C, M> s;
  std::memcpy(&s, state, sizeof s);
  auto converted = fromValue<M>(value);
  if (!converted) return false;
  static_cast<T*>(object)->*(s.member) = std::move(*converted);
  return true;
}

template <class G, class S>
struct PairState {
  G getter;
  S setter;
};

template <class T, class G>
using GetterValue = std::remove_cvref_t<std::invoke_result_t<G, const T&>>;

template <class T, class G, class S>
Value getPair(const void* object, const unsigned char* state) {
  PairState<G, S> s;
  std::memcpy(&s, state, sizeof s);
  return toValue<GetterValue<T, G>>(std::invoke(s.getter, *static_cast<const T*>(object)));
}

template <class T, class G, class S>
bool setPair(void* object, const unsigned char* state, const Value& value) {
  PairState<G, S> s;
  std::memcpy(&s, state, sizeof s);
  auto converted = fromValue<GetterValue<T, G>>(value);
  if (!converted) return false;
  if constexpr (std::is_same_v<std::invoke_result_t<S, T&, GetterValue<T, G>>, bool>) {
    return std::invoke(s.setter, *static_cast<T*>(object), std::move(*converted));
  } else {
    std::invoke(s.setter, *static_cast<T*>(object), std::move(*converted));
    return true;
  }
}

template <class T, class G>
Value getOnly(const void* object, const unsigned char* state) {
  PairState<G, char> s;
  std::memcpy(&s, state, sizeof s);
  return toValue<GetterValue<T, G>>(std::invoke(s.getter, *static_cast<const T*>(object)));
}

// Converts a literal default (bool, integer, floating, enum, text, Color, Vec) to a Value.
template <class D>
Value looseValue(const D& d) {
  if constexpr (std::is_convertible_v<D, std::string_view> && !std::is_same_v<D, bool>) {
    return std::string(std::string_view(d));
  } else {
    return toValue<std::remove_cvref_t<D>>(d);
  }
}

}  // namespace detail

// ---- the fluent handle ---------------------------------------------------------------------------------

template <class T>
class PropertySet;

// Returned by PropertySet::add; each setter returns the handle so declarations chain. A handle for a
// rejected declaration (invalid()) ignores every call. Use it within the full expression that created it.
template <class T>
class PropertyRef {
 public:
  PropertyRef(PropertySet<T>* set, std::optional<size_t> index) : set_(set), index_(index) {}
  bool valid() const { return index_.has_value(); }

  PropertyRef& category(std::string name) { return edit([&](PropertyDescriptor& d) { d.category = std::move(name); }); }
  PropertyRef& group(std::string name) { return edit([&](PropertyDescriptor& d) { d.group = std::move(name); }); }
  PropertyRef& order(int32_t order) { return edit([&](PropertyDescriptor& d) { d.order = order; }); }
  PropertyRef& tooltip(std::string text) { return edit([&](PropertyDescriptor& d) { d.meta.tooltip = std::move(text); }); }
  PropertyRef& unit(std::string suffix) { return edit([&](PropertyDescriptor& d) { d.meta.unit = std::move(suffix); }); }
  PropertyRef& units(std::vector<UnitConversion> units) { return edit([&](PropertyDescriptor& d) { d.meta.units = std::move(units); }); }
  PropertyRef& precision(int digits) { return edit([&](PropertyDescriptor& d) { d.meta.precision = std::clamp(digits, 0, 9); }); }
  PropertyRef& step(double step) {
    return edit([&](PropertyDescriptor& d) {
      if (std::isfinite(step) && step >= 0.0) d.meta.step = step;
      else note(d, "step must be finite and not negative");
    });
  }
  PropertyRef& readOnly() { return edit([](PropertyDescriptor& d) { d.meta.readOnly = true; }); }
  PropertyRef& hidden() { return edit([](PropertyDescriptor& d) { d.meta.hidden = true; }); }
  PropertyRef& advanced() { return edit([](PropertyDescriptor& d) { d.meta.advanced = true; }); }
  PropertyRef& multiline() { return edit([](PropertyDescriptor& d) { d.meta.multiline = true; }); }
  PropertyRef& password() { return edit([](PropertyDescriptor& d) { d.meta.password = true; }); }
  PropertyRef& asSwitch() { return edit([](PropertyDescriptor& d) { d.meta.asSwitch = true; }); }
  PropertyRef& notResettable() { return edit([](PropertyDescriptor& d) { d.meta.resettable = false; }); }
  PropertyRef& maxLength(size_t bytes) { return edit([&](PropertyDescriptor& d) { d.meta.maxLength = bytes; }); }

  // Hard range: no edit leaves it. Requires a numeric kind and min <= max.
  PropertyRef& range(double min, double max) {
    return edit([&](PropertyDescriptor& d) {
      if (!numericKind(d) || !std::isfinite(min) || !std::isfinite(max) || min > max) return note(d, "range needs a numeric property and finite min <= max");
      d.meta.min = min;
      d.meta.max = max;
    });
  }
  PropertyRef& softRange(double min, double max) {
    return edit([&](PropertyDescriptor& d) {
      if (!numericKind(d) || !std::isfinite(min) || !std::isfinite(max) || min > max) return note(d, "softRange needs a numeric property and finite min <= max");
      d.meta.softMin = min;
      d.meta.softMax = max;
    });
  }
  // Presents a floating-point property as a slider over [min, max] (also its hard range).
  PropertyRef& slider(double min, double max) {
    return edit([&](PropertyDescriptor& d) {
      if (d.kind != ValueKind::Double || !std::isfinite(min) || !std::isfinite(max) || min >= max) return note(d, "slider needs a floating-point property and finite min < max");
      d.kind = ValueKind::Range;
      d.meta.min = min;
      d.meta.max = max;
      d.meta.softMin = min;
      d.meta.softMax = max;
    });
  }
  // An integer property presented as a choice; add entries with value().
  PropertyRef& asEnum() {
    return edit([](PropertyDescriptor& d) {
      if (d.kind == ValueKind::Int) d.kind = ValueKind::Enum;
    });
  }
  // A named entry of an enum property. Names must be unique (ignoring case).
  template <class E>
  PropertyRef& value(std::string name, E enumerator, std::string label = {}) {
    return edit([&](PropertyDescriptor& d) {
      if (d.kind != ValueKind::Enum) return note(d, "value() needs an enum property");
      const Value v = detail::toValue<E>(enumerator);
      if (!std::holds_alternative<int64_t>(v) || d.findEnum(name) != nullptr || d.findEnum(std::get<int64_t>(v)) != nullptr) return note(d, "duplicate or invalid enum entry '" + name + "'");
      d.enumEntries.push_back({std::move(name), std::get<int64_t>(v), std::move(label)});
    });
  }
  // The value the reset affordance restores. The type must fit the property kind.
  template <class D>
  PropertyRef& defaultValue(const D& value) {
    return edit([&](PropertyDescriptor& d) {
      Value v = detail::looseValue<D>(value);
      if (std::holds_alternative<int64_t>(v) && storageOf(d.kind) == Storage::Double) v = static_cast<double>(std::get<int64_t>(v));
      if (d.kind == ValueKind::Enum) {
        // Entries may be declared after the default, so membership is not checked here.
        if (!std::holds_alternative<int64_t>(v)) return note(d, "default value does not fit the property");
        d.meta.defaultValue = v;
        return;
      }
      const Validated checked = validate(d, v);
      if (isError(checked.code)) return note(d, "default value does not fit the property");
      d.meta.defaultValue = checked.value;
    });
  }

  // Edit conditions on another property of the same object (by name), or on the object itself.
  PropertyRef& enableWhenTrue(std::string property) { return condition(&PropertyMeta::enableWhen, std::move(property), CompareOp::Truthy, true); }
  template <class V>
  PropertyRef& enableWhen(std::string property, CompareOp op, const V& operand) { return condition(&PropertyMeta::enableWhen, std::move(property), op, detail::looseValue<V>(operand)); }
  PropertyRef& visibleWhenTrue(std::string property) { return condition(&PropertyMeta::visibleWhen, std::move(property), CompareOp::Truthy, true); }
  template <class V>
  PropertyRef& visibleWhen(std::string property, CompareOp op, const V& operand) { return condition(&PropertyMeta::visibleWhen, std::move(property), op, detail::looseValue<V>(operand)); }
  // Fn is a function pointer or captureless lambda taking `const T&`: enableIf<&isTextured>().
  template <auto Fn>
  PropertyRef& enableIf() {
    return edit([](PropertyDescriptor& d) { d.meta.enableWhen.predicate = &predicate<Fn>; });
  }
  template <auto Fn>
  PropertyRef& visibleIf() {
    return edit([](PropertyDescriptor& d) { d.meta.visibleWhen.predicate = &predicate<Fn>; });
  }

 private:
  template <auto Fn>
  static bool predicate(const void* object) { return static_cast<bool>(Fn(*static_cast<const T*>(object))); }
  static bool numericKind(const PropertyDescriptor& d) { return d.kind == ValueKind::Int || d.kind == ValueKind::Double || d.kind == ValueKind::Range || d.kind == ValueKind::Vec2 || d.kind == ValueKind::Vec3; }
  void note(const PropertyDescriptor& d, const std::string& message) { set_->noteError("property '" + d.name + "': " + message); }

  template <class F>
  PropertyRef& edit(F&& change) {
    if (index_) change(set_->mutableAt(*index_));
    return *this;
  }
  PropertyRef& condition(Condition PropertyMeta::*slot, std::string property, CompareOp op, Value operand) {
    return edit([&](PropertyDescriptor& d) {
      if (property.empty()) return note(d, "condition needs a property name");
      Condition& c = d.meta.*slot;
      c.property = std::move(property);
      c.op = op;
      c.operand = std::move(operand);
    });
  }

  PropertySet<T>* set_;
  std::optional<size_t> index_;
};

// ---- the typed set -------------------------------------------------------------------------------------

template <class T>
class PropertySet : public PropertySetBase {
 public:
  explicit PropertySet(std::string typeName) : PropertySetBase(std::move(typeName), typeid(T)) {}

  // Subsequent add() calls land in this category (and group). An explicit order sorts categories; ties
  // keep first-appearance order.
  PropertySet& category(std::string name) {
    currentCategory_ = std::move(name);
    currentGroup_.clear();
    return *this;
  }
  PropertySet& category(std::string name, int order) {
    setCategoryOrder(name, order);
    return category(std::move(name));
  }
  PropertySet& group(std::string name) {
    currentGroup_ = std::move(name);
    return *this;
  }

  // A data member: add("roughness", "Roughness", &Material::roughness).
  template <class C, class M>
    requires std::is_base_of_v<C, T>
  PropertyRef<T> add(std::string name, std::string label, M C::* member) {
    static_assert(!std::is_function_v<M>, "use the getter/setter overload for member functions");
    static_assert(sizeof(detail::MemberState<T, C, M>) <= Accessor::kStorage, "member pointer too large");
    constexpr ValueKind kind = detail::kindFor<M>();
    const detail::MemberState<T, C, M> state{member};
    return declare(std::move(name), std::move(label), kind, Accessor(&detail::getMember<T, C, M>, &detail::setMember<T, C, M>, &state, sizeof state));
  }

  // A getter and a setter: add("size", "Size", &Light::size, &Light::setSize).
  template <class G, class S>
  PropertyRef<T> add(std::string name, std::string label, G getter, S setter) {
    using V = detail::GetterValue<T, G>;
    static_assert(std::is_invocable_v<S, T&, V>, "the setter must accept (T&, value)");
    static_assert(std::is_trivially_copyable_v<G> && std::is_trivially_copyable_v<S>, "accessors must be trivially copyable (no captures)");
    static_assert(sizeof(detail::PairState<G, S>) <= Accessor::kStorage, "accessor pair too large");
    constexpr ValueKind kind = detail::kindFor<V>();
    const detail::PairState<G, S> state{getter, setter};
    return declare(std::move(name), std::move(label), kind, Accessor(&detail::getPair<T, G, S>, &detail::setPair<T, G, S>, &state, sizeof state));
  }

  // A computed, read-only property.
  template <class G>
  PropertyRef<T> addReadOnly(std::string name, std::string label, G getter) {
    using V = detail::GetterValue<T, G>;
    static_assert(std::is_trivially_copyable_v<G>, "accessors must be trivially copyable (no captures)");
    static_assert(sizeof(detail::PairState<G, char>) <= Accessor::kStorage, "accessor too large");
    constexpr ValueKind kind = detail::kindFor<V>();
    const detail::PairState<G, char> state{getter, 0};
    PropertyRef<T> ref = declare(std::move(name), std::move(label), kind, Accessor(&detail::getOnly<T, G>, nullptr, &state, sizeof state));
    return ref.readOnly();
  }

  // Fills the default of every property that has none from the value `prototype` holds now (the usual
  // way to define "reset": a default-constructed T).
  PropertySet& defaultsFrom(const T& prototype) {
    for (size_t i = 0; i < size(); ++i) {
      PropertyDescriptor& d = mutableAt(i);
      if (d.meta.defaultValue) continue;
      const Validated read = validate(d, d.accessor.get(&prototype));
      if (!isError(read.code)) d.meta.defaultValue = read.value;
    }
    return *this;
  }

 private:
  PropertyRef<T> declare(std::string name, std::string label, ValueKind kind, Accessor accessor) {
    PropertyDescriptor d;
    d.name = std::move(name);
    d.label = std::move(label);
    d.category = currentCategory_;
    d.group = currentGroup_;
    d.kind = kind;
    d.accessor = accessor;
    return PropertyRef<T>(this, addDescriptor(std::move(d)));
  }
};

template <class T>
Target targetOf(T& object, const PropertySet<T>& set) {
  return {&object, &set};
}

}  // namespace r1ui::props
