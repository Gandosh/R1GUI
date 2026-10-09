// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of RenameField.h.
// Invariants: the finish callback is called at most once; the widget is not touched after the callback
//   (it destroys the field); a focus loss with an unchanged text finishes as a cancel.
// Callers: the customize edit displays.
#include "r1ui/widgets/customize/RenameField.h"

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

using core::events::Key;

void RenameField::onAttached() {
  TextInput::onAttached();
  setText(original_);
  setMaxLength(64);
}

void RenameField::finish(bool commit) {
  if (finished_) return;
  finished_ = true;
  const std::string text = this->text();
  if (!commit) setText(original_);  // the focus-out of the destroyed field then has nothing to commit
  if (finish_) {
    auto callback = finish_;
    callback(commit, text);
  }
}

void RenameField::onKeyDown(Event& e) {
  if (e.key == Key::Enter) {
    e.markHandled();
    finish(true);
    return;
  }
  if (e.key == Key::Escape) {
    e.markHandled();
    finish(false);
    return;
  }
  TextInput::onKeyDown(e);
}

void RenameField::onFocusOut(Event& e) {
  TextInput::onFocusOut(e);
  if (!ui().alive(id())) return;
  finish(text() != original_);
}

}  // namespace r1ui::widgets
