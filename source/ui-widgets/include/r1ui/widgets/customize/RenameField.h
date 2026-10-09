// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: RenameField, the in-place text field of the edit displays: it replaces a label while the user
//   renames a menu, entry, toolbar button or heading.
// Why: spec 06 / decision D5 and spec 08 (inline rename): Enter commits, Escape cancels, losing focus
//   commits a changed text, and an empty text means "restore the default label". The plain TextInput
//   cannot report a cancel, so this thin subclass adds the finish callback.
// Callers: MenuEditor, ToolbarEditStrip, FreeFormPanel. Calls: TextInput.
// Contract: the finish callback runs exactly once, from inside one of this widget's own handlers, with
//   (commit, text); it destroys the field. After Escape the text is reset to the original so the
//   focus-out of the destroyed field commits nothing.
#pragma once

#include <functional>
#include <string>

#include "r1ui/widgets/textinput/TextInput.h"

namespace r1ui::widgets {

class RenameField final : public TextInput {
 public:
  using Finish = std::function<void(bool commit, const std::string& text)>;

  explicit RenameField(std::string original) : TextInput(TextInputTone::Default, TextInputSize::Sm), original_(std::move(original)) {}
  const char* typeName() const override { return "RenameField"; }
  void setOnFinish(Finish finish) { finish_ = std::move(finish); }
  bool finished() const { return finished_; }

  void onAttached() override;
  void onKeyDown(Event& e) override;
  void onFocusOut(Event& e) override;

 private:
  void finish(bool commit);

  std::string original_;
  Finish finish_;
  bool finished_ = false;
};

}  // namespace r1ui::widgets
