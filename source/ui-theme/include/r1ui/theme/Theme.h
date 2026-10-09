// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the active theme (dark or light) over a shared, immutable Tokens set.
// Why: switching theme must not reload or re-parse anything; it only changes which colour
//   column is read, and bumps a revision so widgets know to repaint.
// Callers: StyleSheet resolution, widgets, the preview. Calls: Tokens.
// Invariants: the Tokens object is never mutated; set() to the current theme is a no-op and does
//   not change the revision.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "r1ui/theme/Tokens.h"

namespace r1ui::theme {

class Theme {
 public:
  // `tokens` must not be null (std::invalid_argument otherwise).
  explicit Theme(std::shared_ptr<const Tokens> tokens, ThemeId initial = ThemeId::Dark);

  ThemeId id() const { return id_; }
  const Tokens& tokens() const { return *tokens_; }
  // Bumped by every effective theme change; repaint everything that cached resolved styles.
  uint32_t revision() const { return revision_; }

  void set(ThemeId id);
  void toggle() { set(id_ == ThemeId::Dark ? ThemeId::Light : ThemeId::Dark); }
  std::optional<Color> color(std::string_view name) const { return tokens_->color(id_, name); }

 private:
  std::shared_ptr<const Tokens> tokens_;
  ThemeId id_;
  uint32_t revision_ = 1;
};

}  // namespace r1ui::theme
