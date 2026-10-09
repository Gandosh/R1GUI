// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: value binding (owner decision D16): BindingProvider, the interface a host implements to resolve
//   a binding source (a variable name or an expression string it understands) to a value and to list
//   its variables; BindingRecord (what a bound property holds: the source text); BindingStore (which
//   object's property is bound to which source); and BindingState (the row state the panel shows).
// Why: a property can be driven by a named variable or expression instead of a stored value. The
//   toolkit does not evaluate variables; it asks the host, keeps the source text, derives the state, and
//   makes bind, unbind and rebind undoable like any other edit.
// States: Unbound (a plain stored value); Bound (every selected object is bound to the same source and
//   it resolves to a value of the property's kind); Broken (bound but the host cannot resolve the source
//   or the value does not fit: the variable is missing); Mixed (selected objects differ in binding).
// Display rules (accepted with D16): a bound value is drawn with the component colour and a variable
//   pill instead of a number; a broken binding shows the source with the invalid (danger) look and a
//   tooltip naming the missing variable; unbound rows show the stored value. The widget layer applies
//   these; this header only supplies the state.
// Callers: PropertyContext, UndoStack (binding steps), the property panel widget, host code, tests.
// Lifetime: the store is shared (shared_ptr) so undo steps hold weak references and a destroyed store
//   never leaves a dangling pointer; objects are keyed by address and must be released with
//   forgetObject() (PropertyContext::forgetObject does it with the undo stack) before they are freed.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "r1ui/props/Value.h"

namespace r1ui::props {

inline constexpr size_t kMaxBindingSourceBytes = 4096;
inline constexpr size_t kMaxBindings = 1u << 20;

struct VariableInfo {
  std::string name;
  ValueKind kind = ValueKind::Double;
  std::string description;
};

class BindingProvider {
 public:
  virtual ~BindingProvider() = default;
  // The current value of a variable or expression, or nullopt when the source cannot be resolved.
  virtual std::optional<Value> resolve(std::string_view source) const = 0;
  // The variables a user may pick from, in the order to list them.
  virtual std::vector<VariableInfo> variables() const = 0;
};

enum class BindingState : uint8_t { Unbound, Bound, Broken, Mixed };

struct BindingRecord {
  std::string source;
  friend bool operator==(const BindingRecord&, const BindingRecord&) = default;
};

class BindingStore {
 public:
  const BindingRecord* find(const void* object, std::string_view property) const;
  // Binds (record set) or unbinds (nullopt). False when the source is empty/too long/not UTF-8 or the
  // store is full; the previous binding is then kept.
  bool set(const void* object, std::string_view property, std::optional<BindingRecord> record);
  void forgetObject(const void* object);
  size_t size() const { return records_.size(); }

 private:
  struct Key {
    const void* object;
    std::string property;
    friend bool operator==(const Key&, const Key&) = default;
  };
  struct KeyHash {
    size_t operator()(const Key& k) const { return std::hash<const void*>{}(k.object) * 1099511628211ull ^ std::hash<std::string>{}(k.property); }
  };
  std::unordered_map<Key, BindingRecord, KeyHash> records_;
};

}  // namespace r1ui::props
