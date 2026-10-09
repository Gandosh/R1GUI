// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the non-template part of PropertySet: name index, declaration validation (collisions, limits,
//   text validity) and the display order of properties.
// Why: validation and ordering are identical for every host type, so they live once outside the
//   templates. A bad declaration is recorded in errors() and skipped; it never throws.
// Callers: PropertySet<T> (through its protected interface), PropertyContext, tests.
#include "r1ui/props/PropertySet.h"

#include <algorithm>
#include <map>

namespace r1ui::props {

namespace {

constexpr size_t kMaxErrors = 64;  // a hostile declaration loop cannot grow the error list without bound

// Names are keys in pasted text, which is tab- and newline-separated: control characters are refused.
bool cleanKey(const std::string& s) {
  if (s.empty() || s.size() > kMaxNameBytes || !isValidUtf8(s)) return false;
  return std::none_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7F; });
}

bool cleanText(const std::string& s) { return s.size() <= kMaxNameBytes * 4 && isValidUtf8(s); }

}  // namespace

void PropertySetBase::noteError(std::string message) {
  if (errors_.size() < kMaxErrors) errors_.push_back(typeName_ + ": " + std::move(message));
}

std::optional<size_t> PropertySetBase::find(std::string_view name) const {
  const auto it = byName_.find(std::string(name));
  if (it == byName_.end()) return std::nullopt;
  return it->second;
}

void PropertySetBase::setCategoryOrder(const std::string& category, int order) { categoryOrder_[category] = order; }

std::optional<size_t> PropertySetBase::addDescriptor(PropertyDescriptor descriptor) {
  if (properties_.size() >= kMaxProperties) {
    noteError("too many properties (limit " + std::to_string(kMaxProperties) + ")");
    return std::nullopt;
  }
  if (!cleanKey(descriptor.name)) {
    noteError("property name is empty, too long, not valid UTF-8 or contains a control character");
    return std::nullopt;
  }
  if (!cleanText(descriptor.label) || !cleanText(descriptor.category) || !cleanText(descriptor.group)) {
    noteError("property '" + descriptor.name + "': label, category or group is too long or not valid UTF-8");
    return std::nullopt;
  }
  if (byName_.contains(descriptor.name)) {
    noteError("duplicate property name '" + descriptor.name + "' (the first declaration is kept)");
    return std::nullopt;
  }
  if (!descriptor.accessor.valid()) {
    noteError("property '" + descriptor.name + "': accessor is invalid");
    return std::nullopt;
  }
  const size_t index = properties_.size();
  byName_.emplace(descriptor.name, index);
  properties_.push_back(std::move(descriptor));
  order_.clear();
  return index;
}

std::span<const size_t> PropertySetBase::displayOrder() const {
  if (order_.size() == properties_.size()) return order_;
  struct Key {
    int categoryOrder = 0;
    size_t categoryFirst = 0;
    size_t groupFirst = 0;  // 0 = ungrouped, then groups by first appearance
  };
  std::map<std::string, size_t> categoryFirst;
  std::map<std::pair<std::string, std::string>, size_t> groupFirst;
  std::vector<Key> keys(properties_.size());
  for (size_t i = 0; i < properties_.size(); ++i) {
    const PropertyDescriptor& d = properties_[i];
    const size_t c = categoryFirst.try_emplace(d.category, categoryFirst.size()).first->second;
    const auto o = categoryOrder_.find(d.category);
    keys[i].categoryOrder = o == categoryOrder_.end() ? 0 : o->second;
    keys[i].categoryFirst = c;
    if (!d.group.empty()) keys[i].groupFirst = 1 + groupFirst.try_emplace({d.category, d.group}, groupFirst.size()).first->second;
  }
  order_.resize(properties_.size());
  for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
  std::stable_sort(order_.begin(), order_.end(), [&](size_t a, size_t b) {
    if (keys[a].categoryOrder != keys[b].categoryOrder) return keys[a].categoryOrder < keys[b].categoryOrder;
    if (keys[a].categoryFirst != keys[b].categoryFirst) return keys[a].categoryFirst < keys[b].categoryFirst;
    if (keys[a].groupFirst != keys[b].groupFirst) return keys[a].groupFirst < keys[b].groupFirst;
    return properties_[a].order < properties_[b].order;
  });
  return order_;
}

}  // namespace r1ui::props
