// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the widgets a dialog is built from: DialogBox (a plain flex container, optionally with a
//   divider line under it), DialogText (title or description, wrapped to its width), DialogButton
//   (the pressable action button), DialogCloseButton (24 x 24 icon button) and DialogContent (the
//   root inside the host: Enter runs the default action), plus their style rows.
// Why: the repo has no generic container or button yet and widget folders must not depend on each
//   other's private pieces, so the dialog carries the few small widgets it needs. They are exposed in
//   a header (not hidden in the .cpp) so tests and the gallery can reach them; applications use
//   Dialog.h.
// Callers: Dialog.cpp. Calls: PaintContext, the style rows dialog.*.
// Keyboard: DialogButton and DialogCloseButton are focusable; Enter and Space activate on key down
//   (repeats ignored) and a click activates on release inside; the focus ring shows only for keyboard
//   focus. A disabled button is skipped by Tab and ignores input.
#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/dialog/Dialog.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class DialogBox final : public WidgetObject {
 public:
  const char* typeName() const override { return "DialogBox"; }
  void paint(PaintContext& ctx) override;
  void setDividerBelow(bool on);

 private:
  bool divider_ = false;
};

class DialogText final : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();
  // `styleKey` is dialog.title or dialog.description.
  DialogText(std::string text, const char* styleKey);
  const char* typeName() const override { return "DialogText"; }
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  void paint(PaintContext& ctx) override;
  std::string_view accessibleName() const override { return text_; }

 private:
  std::vector<std::string> wrapped(double width) const;

  std::string text_;
  const char* key_;
};

// Pressable button base: pointer press, release inside, Enter and Space.
class DialogPressable : public WidgetObject {
 public:
  void setOnActivate(std::function<void()> fn) { onActivate_ = std::move(fn); }
  void onAttached() override;
  void onPointerDown(Event& e) override;
  void onPointerUp(Event& e) override;
  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;
  uint8_t styleState() const override;
  Cursor cursor() const override { return enabled() ? Cursor::Pointer : Cursor::Default; }
  void activate();

 private:
  std::function<void()> onActivate_;
};

class DialogButton final : public DialogPressable {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();
  DialogButton(DialogAction action) : action_(std::move(action)) {}
  const char* typeName() const override { return "DialogButton"; }
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  float paintOpacity() const override;
  std::string_view accessibleName() const override { return action_.label; }
  const DialogAction& action() const { return action_; }
  const char* styleKey() const;

 private:
  DialogAction action_;
};

class DialogCloseButton final : public DialogPressable {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "DialogCloseButton"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  std::string_view accessibleName() const override { return "Close"; }
};

class DialogContent final : public WidgetObject {
 public:
  explicit DialogContent(std::function<void()> runDefault) : runDefault_(std::move(runDefault)) {}
  const char* typeName() const override { return "DialogContent"; }
  void onAttached() override;
  void onKeyDown(Event& e) override;

 private:
  std::function<void()> runDefault_;
};

}  // namespace r1ui::widgets
