// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Theme construction and switching.
// Why: see Theme.h.
// Callers: Theme.h consumers.
#include "r1ui/theme/Theme.h"

#include <stdexcept>
#include <utility>

namespace r1ui::theme {

Theme::Theme(std::shared_ptr<const Tokens> tokens, ThemeId initial) : tokens_(std::move(tokens)), id_(initial) {
  if (!tokens_) throw std::invalid_argument("Theme requires tokens");
}

void Theme::set(ThemeId id) {
  if (id == id_) return;
  id_ = id;
  ++revision_;
}

}  // namespace r1ui::theme
