// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ToastManager, the transient notifications of one UiContext: show() puts a toast at the top
//   centre of the window (8 px from the top edge), stacks further toasts below it, dismisses each
//   after its lifetime (3 s default, 5 s warning, 10 s error; tunable per toast and per manager),
//   pauses the countdown while the pointer is over a toast, and fades toasts in and out.
// Why: errors and confirmations must reach the user without taking focus or blocking input, in every
//   window, with one look (measured tones: default `accent` with white text, warning from the
//   warning tokens, error red-600 with white text and copy and close buttons) and one stacking rule.
// Callers: application code (show), the gallery. Calls: UiContext timers (lifetime, fade-out),
//   the overlay layer (the toast layer is a child of it, above every popup), TooltipContent's
//   wrapTooltipText, UiHost::writeClipboard for the copy button.
// Look (docs/spec/widgets.md 2.12): padding 10 x 6, gap 6, radius 6, 12 px text on a 16 px line, a
//   12 px leading icon (check, warning triangle), shadow md, maximum width 384 (longer texts wrap).
//   Warning toasts have a 1 px border that does not change the size of the box's content.
// Lifetime: the manager is a handle on shared state; destroying it does not touch the UI (call
//   dismissAll() first when toasts must go); toasts that outlive their manager run out their
//   timers and disappear. Callbacks run on the UI thread after the toast is gone from the tree.
// Not implemented: the 4 px slide of the enter and exit animation (fade only), swipe to dismiss,
//   keyboard focus on the toast buttons (toasts never take focus), tooltips over the toast buttons
//   (the toast layer is above the tooltip layer).
// Boundaries: texts are sanitised and limited to 1024 bytes; at most maxVisible (default 5) toasts
//   exist, the oldest is removed immediately when a new one would exceed it.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::widgets {

class UiContext;
class ToastStack;

enum class ToastTone : uint8_t { Default, Warning, Error };
// Which buttons a toast shows; Auto = none, except error toasts which show copy and close.
enum class ToastControls : uint8_t { Auto, None, Close, CopyAndClose };

inline constexpr uint64_t kToastNeverExpires = UINT64_MAX;
inline constexpr size_t kMaxToastTextBytes = 1024;

struct ToastSpec {
  std::string text;
  ToastTone tone = ToastTone::Default;
  std::string icon;                      // icon name; empty = check (default) or triangle-alert
  uint64_t durationMs = 0;               // 0 = the tone's default; kToastNeverExpires = stays
  ToastControls controls = ToastControls::Auto;
  std::function<void()> onClosed;        // after the toast was removed
};

using ToastId = uint32_t;

struct ToastTiming {
  uint64_t defaultMs = 3000;
  uint64_t warningMs = 5000;
  uint64_t errorMs = 10000;
  uint64_t fadeMs = 150;
};

class ToastManager {
 public:
  explicit ToastManager(UiContext& ui);
  ~ToastManager();
  ToastManager(const ToastManager&) = delete;
  ToastManager& operator=(const ToastManager&) = delete;

  // Shows a toast; 0 when the text is empty after sanitising or the widgets could not be created.
  ToastId show(ToastSpec spec);
  // Starts the fade-out of one toast (instant when animations are off). False for an unknown id.
  bool dismiss(ToastId id);
  void dismissAll();

  size_t count() const;
  core::tree::WidgetId widgetOf(ToastId id) const;
  ToastTiming timing() const;
  void setTiming(const ToastTiming& timing);
  void setMaxVisible(size_t count);

 private:
  std::shared_ptr<ToastStack> stack_;
};

}  // namespace r1ui::widgets
