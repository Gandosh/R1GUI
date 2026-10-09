// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the generation-checked widget handle.
// Why: widgets are referenced across frames and event handlers; a raw pointer would dangle after
//   a destroy. An id names a slot plus the generation of its occupant, so a stale id can never
//   alias a newer widget that reused the slot.
// Callers: every module that refers to a widget. Calls: nothing.
// Invariants: generation 0 is never issued, so a value-initialised WidgetId is the invalid id.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace r1ui::core::tree {

struct WidgetId {
  uint32_t index = 0;
  uint32_t generation = 0;  // 0 = invalid

  bool valid() const { return generation != 0; }
  friend bool operator==(const WidgetId&, const WidgetId&) = default;
};

inline constexpr WidgetId kNoWidget{};

}  // namespace r1ui::core::tree

template <>
struct std::hash<r1ui::core::tree::WidgetId> {
  size_t operator()(const r1ui::core::tree::WidgetId& id) const noexcept {
    return std::hash<uint64_t>{}((uint64_t{id.generation} << 32) | id.index);
  }
};
