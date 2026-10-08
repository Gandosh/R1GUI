// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: checked integer narrowing used at every size/index/API conversion boundary.
// Why: the guard register requires checked sizes, counts and API conversions; silent
//   truncation of a window size, buffer length or index is a defect class we exclude here.
// Callers: ui-platform (Win32 sizes, string lengths), ui-render (Vulkan counts and extents).
// Failure behavior: throws std::range_error; callers at public boundaries keep prior state.
#pragma once

#include <concepts>
#include <stdexcept>
#include <utility>

namespace r1ui::core {

// Converts between integer types, throwing if the value does not fit exactly.
template <std::integral To, std::integral From>
[[nodiscard]] constexpr To checkedCast(From value) {
  if (!std::in_range<To>(value)) {
    throw std::range_error("checkedCast: value does not fit the destination type");
  }
  return static_cast<To>(value);
}

}  // namespace r1ui::core
