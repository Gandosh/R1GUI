// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the widgets of the toast system: ToastLayer (the full-window column at the top centre that
//   stacks toasts, a child of the overlay layer above every popup) and ToastWidget (one toast: icon,
//   wrapped text, optional copy and close buttons, tone look, fade), plus the toast style rows.
// Why: ToastManager decides when toasts exist; these widgets decide how one looks and which part of
//   it was pressed. A toast is a single leaf widget that paints its icon, text and buttons and
//   resolves pointer positions to buttons itself, so a toast costs one tree node.
// Callers: ToastManager (Toast.cpp), the gallery (static previews), tests. Calls: PaintContext,
//   the style rows toast.default | toast.warning | toast.error.
// Layout: padding 10 x 6 (plus the 1 px border of the warning tone), 6 px gaps, 12 px icon, 16 x 18
//   buttons (a 12 px icon with 2 px around it) aligned to the top of the first line (a toast with buttons is 30 px high, one without
//   28), maximum width 384 with the text wrapped inside.
// Opacity: fades in over fadeMs after creation and out after beginClose(); both are instant when
//   animations are not running (tests, offscreen renders).
#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/WidgetObject.h"
#include "r1ui/widgets/toast/Toast.h"

namespace r1ui::widgets {

inline constexpr double kToastMaxWidth = 384.0;
inline constexpr double kToastTopMargin = 8.0;
inline constexpr double kToastGap = 8.0;

class ToastLayer final : public WidgetObject {
 public:
  const char* typeName() const override { return "ToastLayer"; }
  void onAttached() override;
};

class ToastWidget final : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();

  struct Callbacks {
    std::function<void(bool hovered)> onHover;  // pointer entered / left the toast
    std::function<void()> onCopy;
    std::function<void()> onClose;
  };
  ToastWidget(std::string text, ToastTone tone, std::string icon, ToastControls controls, uint64_t fadeMs, Callbacks callbacks);
  const char* typeName() const override { return "Toast"; }
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  Cursor cursor() const override { return hoverButton_ >= 0 ? Cursor::Pointer : Cursor::Default; }
  void onPointerEnter(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onPointerMove(Event& e) override;
  void onClick(Event& e) override;
  std::string_view accessibleName() const override { return text_; }

  // Starts the fade-out (the manager removes the widget when it has run out).
  void beginClose();
  bool closing() const { return closing_; }
  const std::string& text() const { return text_; }
  ToastTone tone() const { return tone_; }
  bool hasCopy() const { return controls_ == ToastControls::CopyAndClose; }
  bool hasClose() const { return controls_ != ToastControls::None; }
  // Rectangle of a button in widget-local logical px (0 = copy, 1 = close); empty when absent.
  core::layout::RectD buttonRect(int button) const;
  const char* styleKey() const;

 private:
  struct Layout {
    std::vector<std::string> lines;
    double textWidth = 0.0;
    float scale = 0.0f;
    double wrapWidth = -1.0;
  };
  const Layout& layoutFor(double wrapWidth) const;
  double textRoom() const;   // wrap width of the text for the maximum toast width
  int buttonAt(double x, double y) const;
  void setHoverButton(int button);

  std::string text_;
  ToastTone tone_;
  std::string icon_;
  ToastControls controls_;
  uint64_t fadeMs_;
  Callbacks callbacks_;
  uint64_t createdMs_ = 0;
  uint64_t closeMs_ = 0;
  bool closing_ = false;
  int hoverButton_ = -1;
  mutable Layout layout_;
};

}  // namespace r1ui::widgets
