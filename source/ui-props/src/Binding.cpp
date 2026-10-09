// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: BindingStore (see Binding.h): the map from (object, property name) to the bound source text.
// Why: bindings are state that undo steps change; keeping them in one validated store keeps the
//   property context and the undo stack free of map plumbing and enforces the source limits once.
// Callers: PropertyContext*.cpp, UndoStack.cpp, tests.
#include "r1ui/props/Binding.h"

namespace r1ui::props {

const BindingRecord* BindingStore::find(const void* object, std::string_view property) const {
  const auto it = records_.find(Key{object, std::string(property)});
  return it == records_.end() ? nullptr : &it->second;
}

bool BindingStore::set(const void* object, std::string_view property, std::optional<BindingRecord> record) {
  Key key{object, std::string(property)};
  if (!record) {
    records_.erase(key);
    return true;
  }
  if (record->source.empty() || record->source.size() > kMaxBindingSourceBytes || !isValidUtf8(record->source)) return false;
  if (records_.size() >= kMaxBindings && !records_.contains(key)) return false;
  records_[std::move(key)] = std::move(*record);
  return true;
}

void BindingStore::forgetObject(const void* object) {
  for (auto it = records_.begin(); it != records_.end();) {
    it = it->first.object == object ? records_.erase(it) : std::next(it);
  }
}

}  // namespace r1ui::props
